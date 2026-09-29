// SPDX-License-Identifier: GPL-3.0-only
/*
 *  PollyMC-Continued - Minecraft Launcher
 *  Copyright (c) 2026 PollyMC-Continued Contributors
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, version 3.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#include "CraftySkinAPI.h"

#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <QTimer>
#include <QUrl>

#include "Application.h"
#include "FileSystem.h"
#include "QObjectPtr.h"
#include "minecraft/skins/SkinModel.h"
#include "net/ByteArraySink.h"
#include "net/Download.h"
#include "net/NetJob.h"

static QString baseUrl()
{
    return QStringLiteral("https://api.crafty.gg/api/v2");
}

static QString encode(const QString& text)
{
    return QString::fromUtf8(QUrl::toPercentEncoding(text));
}

namespace {

// One budget for every crafty.gg request, so the 150/minute API limit is never reached.
class RateGate {
   public:
    RateGate() { m_clock.start(); }

    // Milliseconds to wait, or 0 when the request may start right now (its start is then reserved).
    int acquire()
    {
        const qint64 now = m_clock.elapsed();
        while (!m_starts.isEmpty() && now - m_starts.first() >= kWindowMs)
            m_starts.removeFirst();
        qint64 ready = now;
        if (!m_starts.isEmpty())
            ready = qMax(ready, m_starts.last() + kSpacingMs);
        if (m_starts.size() >= kBudget)
            ready = qMax(ready, m_starts.first() + kWindowMs);
        if (ready > now) {
            // the slot is reserved even when it lies in the future, so waiters stagger instead of firing together
            m_starts.append(ready);
            return int(ready - now);
        }
        m_starts.append(now);
        return 0;
    }

   private:
    static constexpr int kBudget = 120;         // rolling budget, the API allows 150
    static constexpr qint64 kWindowMs = 60000;  // rate limit window
    static constexpr qint64 kSpacingMs = 500;   // minimum gap between two request starts
    QElapsedTimer m_clock;
    QList<qint64> m_starts;
};

RateGate& gate()
{
    static RateGate instance;
    return instance;
}

// A crafty.gg download that waits for its turn in the rate gate instead of firing immediately.
class PacedDownload : public Net::Download {
   public:
    static std::pair<Net::Download::Ptr, QByteArray*> makeByteArray(const QUrl& url);

   protected:
    void executeTask() override;
};

std::pair<Net::Download::Ptr, QByteArray*> PacedDownload::makeByteArray(const QUrl& url)
{
    auto dl = makeShared<PacedDownload>();
    dl->setUrl(url);
    dl->setObjectName(QString("BYTES:") + url.toString());
    // a rate limited reply is retried after the wait the server asks for instead of failing the page
    dl->enableAutoRetry(true);

    auto sink = std::make_unique<Net::ByteArraySink>();
    QByteArray* response = sink->output();
    dl->m_sink = std::move(sink);

    return { dl, response };
}

void PacedDownload::executeTask()
{
    const auto wait = getState() == Task::State::Running ? gate().acquire() : 0;
    if (wait > 0) {
        // the task stays running while it waits, the base class still emits the final signals
        QTimer::singleShot(wait, this, [this] { Net::Download::executeTask(); });
        return;
    }
    Net::Download::executeTask();
}

}  // namespace

namespace Crafty {

API::API(QObject* parent) : QObject(parent) {}

QString API::pageUrl(const QString& hash)
{
    return QStringLiteral("https://crafty.gg/skins/%1").arg(hash);
}

static QString textureCachePath(const QString& hash)
{
    return FS::PathCombine(APPLICATION->dataRoot(), "cache", "skins", hash + ".png");
}

bool API::loadCached(const SkinPtr& skin)
{
    auto path = textureCachePath(skin->hash);
    if (!QFileInfo::exists(path))
        return false;
    QImage image(path);
    if (image.isNull())
        return false;
    auto texture = SkinModel::normalizeTexture(image);
    if (texture.width() != 64 || (texture.height() != 32 && texture.height() != 64))
        return false;
    skin->texture = texture;
    skin->preview = SkinModel::previewFor(texture, skin->slim);
    skin->texturePath = path;
    return true;
}

bool API::cacheTexture(const SkinPtr& skin)
{
    auto path = textureCachePath(skin->hash);
    FS::ensureFolderPathExists(QFileInfo(path).absolutePath());
    if (skin->texture.save(path, "PNG")) {
        skin->texturePath = path;
        return true;
    }
    return false;
}

bool API::fillTexture(const SkinPtr& skin, const QByteArray& json)
{
    QJsonParseError parseError{};
    auto doc = QJsonDocument::fromJson(json, &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        qWarning() << "Crafty: bad skin response at" << parseError.offset << parseError.errorString();
        return false;
    }
    const auto data = doc.object()["data"].toObject();
    const auto encoded = data["texture"].toString().toLatin1();
    if (encoded.isEmpty())
        return false;

    QImage image;
    if (!image.loadFromData(QByteArray::fromBase64(encoded), "PNG") || image.isNull())
        return false;
    auto texture = SkinModel::normalizeTexture(image);
    if (texture.width() != 64 || (texture.height() != 32 && texture.height() != 64)) {
        qWarning() << "Crafty: texture for" << skin->hash << "is not a minecraft skin";
        return false;
    }

    skin->slim = data["slim"].toBool(skin->slim);
    skin->texture = texture;
    skin->preview = SkinModel::previewFor(texture, skin->slim);
    cacheTexture(skin);
    return true;
}

static QList<SkinPtr> parseSkins(const QJsonArray& data, const QString& fallbackUser)
{
    QList<SkinPtr> result;
    for (const auto& raw : data) {
        const auto object = raw.toObject();
        if (object["banned"].toBool() || !object["deleted_at"].isNull())
            continue;
        auto skin = QSharedPointer<Skin>::create();
        skin->hash = object["hash"].toString();
        if (skin->hash.isEmpty())
            continue;
        skin->username = object["first_player"].toObject()["username"].toString(fallbackUser);
        skin->slim = object["slim"].toBool();
        skin->playersCount = object["players_count"].toInt();
        skin->upvotes = object["upvotes_lifetime"].toInt();
        skin->views = object["views_lifetime"].toInt();
        skin->createdAt = object["created_at"].toString();
        result << skin;
    }
    return result;
}

static bool parsePage(const QByteArray& response, int page, const QString& fallbackUser, SkinPage& out, QString& error)
{
    QJsonParseError parseError{};
    auto doc = QJsonDocument::fromJson(response, &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        error = parseError.errorString();
        return false;
    }
    const auto root = doc.object();
    if (!root["success"].toBool()) {
        error = root["message"].toString(QStringLiteral("The skin catalog rejected the request"));
        return false;
    }
    out.skins = parseSkins(root["data"].toArray(), fallbackUser);
    out.page = page;
    const auto meta = root["meta"].toObject();
    // searches carry no pagination, so the next page would just come back empty
    out.hasMore = !meta.isEmpty() && meta["current_page"].toInt() < meta["last_page"].toInt();
    return true;
}

NetJob::Ptr API::catalogPage(int page, const QString& search, PageCallback callback)
{
    auto url = QStringLiteral("%1/skins?limit=20&page=%2&include=first_player").arg(baseUrl()).arg(page);
    if (!search.isEmpty())
        url += QStringLiteral("&search=") + encode(search);

    auto job = makeShared<NetJob>(tr("Browse skins"), APPLICATION->network());
    job->setAskRetry(false);
    auto [action, response] = PacedDownload::makeByteArray(QUrl(url));
    job->addNetAction(action);

    connect(job.get(), &NetJob::succeeded, this, [response, page, callback] {
        SkinPage result;
        QString error;
        if (!parsePage(*response, page, {}, result, error))
            callback({}, error);
        else
            callback(result, {});
    });
    connect(job.get(), &NetJob::failed, this, [callback](const QString& reason) { callback({}, reason); });
    job->start();
    return job;
}

NetJob::Ptr API::playerPage(int page, const QString& playerId, const QString& playerName, PageCallback callback)
{
    auto url = QStringLiteral("%1/players/%2/skins?limit=20&page=%3&include=first_player")
                   .arg(baseUrl(), playerId)
                   .arg(page);

    auto job = makeShared<NetJob>(tr("Download player skins"), APPLICATION->network());
    job->setAskRetry(false);
    auto [action, response] = PacedDownload::makeByteArray(QUrl(url));
    job->addNetAction(action);

    connect(job.get(), &NetJob::succeeded, this, [response, page, playerName, callback] {
        SkinPage result;
        QString error;
        if (!parsePage(*response, page, playerName, result, error))
            callback({}, error);
        else {
            result.playerName = playerName;
            callback(result, {});
        }
    });
    connect(job.get(), &NetJob::failed, this, [callback](const QString& reason) { callback({}, reason); });
    job->start();
    return job;
}

// waits for both halves of a search (text matches and the matched player's skins) and reports them as one page
namespace {
class SearchMerger {
   public:
    SearchMerger(int page, API::PageCallback callback) : m_page(page), m_callback(std::move(callback)) {}

    void setCatalog(const SkinPage& page, const QString& error)
    {
        if (m_catalogDone)
            return;
        m_catalogDone = true;
        m_catalog = page;
        m_catalogError = error;
        report();
    }

    void setPlayer(const SkinPage& page, const QString& error)
    {
        if (m_playerDone)
            return;
        m_playerDone = true;
        m_player = page;
        m_playerError = error;
        report();
    }

   private:
    static void append(QList<SkinPtr>& merged, QSet<QString>& seen, const QList<SkinPtr>& skins)
    {
        for (const auto& skin : skins)
            if (!skin->hash.isEmpty() && !seen.contains(skin->hash)) {
                seen.insert(skin->hash);
                merged << skin;
            }
    }

    void report()
    {
        if (!m_catalogDone || !m_playerDone)
            return;

        // the keyword matches lead the first page, the player's own skins paginate behind them
        QList<SkinPtr> merged;
        QSet<QString> seen;
        if (m_page == 1)
            append(merged, seen, m_catalog.skins);
        append(merged, seen, m_player.skins);

        SkinPage result;
        result.skins = merged;
        result.page = m_page;
        result.hasMore = m_player.hasMore;
        result.playerName = m_player.playerName;
        if (!merged.isEmpty() || (m_catalogError.isEmpty() && m_playerError.isEmpty())) {
            m_callback(result, {});
            return;
        }
        m_callback({}, m_catalogError.isEmpty() ? m_playerError : m_catalogError);
    }

    int m_page;
    API::PageCallback m_callback;
    SkinPage m_catalog;
    SkinPage m_player;
    QString m_catalogError;
    QString m_playerError;
    bool m_catalogDone = false;
    bool m_playerDone = false;
};
}  // namespace

NetJob::Ptr API::fetchSkins(int page, const QString& query, PageCallback callback)
{
    if (m_pageJob && m_pageJob->isRunning())
        m_pageJob->abort();
    if (m_nestedJob && m_nestedJob->isRunning())
        m_nestedJob->abort();
    if (m_searchJob && m_searchJob->isRunning())
        m_searchJob->abort();
    m_pageJob = m_nestedJob = m_searchJob = nullptr;

    if (query.isEmpty()) {
        m_pageJob = catalogPage(page, {}, callback);
        return m_pageJob;
    }

    auto merger = std::make_shared<SearchMerger>(page, callback);

    // crafty's own text search covers player names, tags and styles, it never has more than one page
    if (page == 1)
        m_searchJob = catalogPage(1, query, [merger](const SkinPage& found, const QString& error) { merger->setCatalog(found, error); });
    else
        merger->setCatalog({}, {});

    // and the player the text names exactly, so a name search still lists all of that player's skins
    auto job = makeShared<NetJob>(tr("Find player"), APPLICATION->network());
    job->setAskRetry(false);
    auto [action, response] = PacedDownload::makeByteArray(QUrl(QStringLiteral("%1/players?search=%2").arg(baseUrl(), encode(query))));
    job->addNetAction(action);

    connect(job.get(), &NetJob::succeeded, this, [this, response, page, merger] {
        QJsonParseError parseError{};
        auto doc = QJsonDocument::fromJson(*response, &parseError);
        const auto data = parseError.error == QJsonParseError::NoError ? doc.object()["data"] : QJsonValue();
        const auto player = data.isObject() ? data.toObject() : QJsonObject{};
        const auto id = player["id"].toString();
        if (id.isEmpty()) {
            // nobody by that name, the keyword matches stand on their own
            merger->setPlayer({}, {});
            return;
        }
        m_nestedJob = playerPage(page, id, player["username"].toString(),
                                 [merger](const SkinPage& found, const QString& error) { merger->setPlayer(found, error); });
    });
    connect(job.get(), &NetJob::failed, this, [merger](const QString&) {
        // crafty replies 404 when the player is unknown, the keyword search still has the results
        merger->setPlayer({}, {});
    });

    m_pageJob = job;
    m_pageJob->start();
    return m_pageJob;
}

NetJob::Ptr API::fetchTextures(const QList<SkinPtr>& skins, EachCallback each, DoneCallback done)
{
    // finished requests are released here, never while one of them is reporting back
    m_textureJobs.removeIf([](const NetJob::Ptr& job) { return job->isFinished(); });

    auto job = makeShared<NetJob>(tr("Download skin textures"), APPLICATION->network());
    job->setAskRetry(false);
    int requested = 0;

    for (const auto& skin : skins) {
        if (loadCached(skin)) {
            if (each)
                each(skin);
            continue;
        }
        auto [action, response] = PacedDownload::makeByteArray(QUrl(QStringLiteral("%1/skins/%2").arg(baseUrl(), skin->hash)));
        job->addNetAction(action);
        requested++;
        connect(action.get(), &Task::succeeded, this, [skin, response, each] {
            if (fillTexture(skin, *response) && each)
                each(skin);
        });
    }

    if (requested == 0) {
        if (done)
            done({});
        return nullptr;
    }

    connect(job.get(), &NetJob::succeeded, this, [done] { done({}); });
    connect(job.get(), &NetJob::failed, this, [done](const QString& reason) { done(reason); });
    // an aborted job means the dialog went away, nobody is waiting for that answer anymore
    connect(job.get(), &NetJob::aborted, this, [] {});

    m_textureJobs << job;
    job->start();
    return job;
}

void API::abortAll()
{
    if (m_pageJob && m_pageJob->isRunning())
        m_pageJob->abort();
    if (m_searchJob && m_searchJob->isRunning())
        m_searchJob->abort();
    if (m_nestedJob && m_nestedJob->isRunning())
        m_nestedJob->abort();
    for (const auto& job : m_textureJobs)
        if (job && job->isRunning())
            job->abort();
    m_pageJob = m_nestedJob = m_searchJob = nullptr;
    m_textureJobs.clear();
}

}  // namespace Crafty
