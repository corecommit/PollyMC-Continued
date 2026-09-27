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
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QUrl>

#include "Application.h"
#include "FileSystem.h"
#include "QObjectPtr.h"
#include "minecraft/skins/SkinModel.h"
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
    auto [action, response] = Net::Download::makeByteArray(QUrl(url));
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
    auto [action, response] = Net::Download::makeByteArray(QUrl(url));
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

NetJob::Ptr API::fetchSkins(int page, const QString& query, PageCallback callback)
{
    if (m_pageJob && m_pageJob->isRunning())
        m_pageJob->abort();
    if (m_nestedJob && m_nestedJob->isRunning())
        m_nestedJob->abort();

    if (query.isEmpty()) {
        m_pageJob = catalogPage(page, {}, callback);
        return m_pageJob;
    }

    auto job = makeShared<NetJob>(tr("Find player"), APPLICATION->network());
    job->setAskRetry(false);
    auto [action, response] = Net::Download::makeByteArray(QUrl(QStringLiteral("%1/players?search=%2").arg(baseUrl(), encode(query))));
    job->addNetAction(action);

    connect(job.get(), &NetJob::succeeded, this, [this, response, page, query, callback] {
        QJsonParseError parseError{};
        auto doc = QJsonDocument::fromJson(*response, &parseError);
        const auto data = parseError.error == QJsonParseError::NoError ? doc.object()["data"] : QJsonValue();
        if (data.isObject()) {
            const auto player = data.toObject();
            const auto id = player["id"].toString();
            if (!id.isEmpty()) {
                m_nestedJob = playerPage(page, id, player["username"].toString(), callback);
                return;
            }
        }
        // no such player, fall back to searching the catalog itself
        m_nestedJob = catalogPage(page, query, callback);
    });
    connect(job.get(), &NetJob::failed, this, [this, page, query, callback](const QString&) {
        // crafty replies 404 when the player is unknown, the catalog search decides what to show
        m_nestedJob = catalogPage(page, query, callback);
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
        auto [action, response] = Net::Download::makeByteArray(QUrl(QStringLiteral("%1/skins/%2").arg(baseUrl(), skin->hash)));
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
    if (m_nestedJob && m_nestedJob->isRunning())
        m_nestedJob->abort();
    for (const auto& job : m_textureJobs)
        if (job && job->isRunning())
            job->abort();
    m_pageJob = m_nestedJob = nullptr;
    m_textureJobs.clear();
}

}  // namespace Crafty
