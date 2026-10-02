/* Copyright 2026 PollyMC-Continued Contributors
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "ModelDownloader.h"

#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMessageBox>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>
#include <QUrl>

#include "Application.h"
#include "FileSystem.h"
#include "net/Download.h"
#include "net/NetJob.h"
#include "ui/dialogs/ProgressDialog.h"

namespace {
// name on disk -> minimum sane size; catches truncation, not bitrot
const std::pair<const char*, qint64> kRequiredFiles[] = {
    { "intent-model-int8.onnx", 50LL * 1024 * 1024 },
    { "tokenizer.json", 100LL * 1024 },
    { "embeddings.json", 100LL * 1024 },
    { "labels.json", 1 },
};

const char* kETagsFileName = ".etags.json";
const int kHeadTimeoutMs = 5000;

// Hugging Face ETags are the file SHA256 in quotes (sometimes W/"...").
// Normalize so stored-vs-server comparison is stable.
QString normalizeETag(const QString& raw)
{
    QString e = raw.trimmed();
    if (e.startsWith(QStringLiteral("W/")))
        e = e.mid(2);
    if (e.size() >= 2 && e.startsWith('"') && e.endsWith('"'))
        e = e.mid(1, e.size() - 2);
    return e;
}
}  // namespace

ModelDownloader::ModelDownloader(QObject* parent) : QObject(parent) {}

ModelDownloader::~ModelDownloader()
{
    // The palette (our parent) can go away mid-download. Never let the
    // job emit into a destroyed downloader.
    if (m_job) {
        QObject::disconnect(m_job.get(), nullptr, this, nullptr);
        m_job->abort();
        m_job.reset();
    }
}

void ModelDownloader::ensureModelsPresent(QWidget* parent)
{
    const QString dir = FS::PathCombine(APPLICATION->dataRoot(), "models");
    // Cached path: no more blocking HEAD requests this session.
    if (m_checkedThisSession) {
        QString reason;
        if (verifyFiles(dir, reason)) {
            emit ready(dir);
        } else {
            emit failed(reason);
        }
        return;
    }
    QMap<QString, QString> remoteEtags;
    const QStringList stale = checkForUpdates(dir, remoteEtags);
    if (stale.isEmpty()) {
        m_checkedThisSession = true;
        emit ready(dir);
        return;
    }
    // Small metadata refresh: no consent question, still shows the
    // normal progress dialog inside startDownload().
    bool needsConsent = false;
    for (const QString& f : stale) {
        if (isLargeModelFile(f)) {
            needsConsent = true;
            break;
        }
    }
    if (!needsConsent) {
        startDownload(parent, stale, remoteEtags);
        return;
    }
    auto answer = QMessageBox::question(
        parent, tr("Download language model?"),
        tr("Natural language commands need a one-time download of about 130 MB "
           "(the intent model, tokenizer, and command vectors). It is stored on "
            "this device and works offline afterwards. Download now?"),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes);
    if (answer != QMessageBox::Yes) {
        emit failed("declined");
        return;
    }
    startDownload(parent, stale, remoteEtags);
}

bool ModelDownloader::verifyFiles(const QString& dir, QString& reason) const
{
    for (const auto& [name, minSize] : kRequiredFiles) {
        const QFileInfo info(FS::PathCombine(dir, QString::fromLatin1(name)));
        if (!info.exists() || info.size() < minSize) {
            reason = QString("missing or truncated: %1").arg(QString::fromLatin1(name));
            return false;
        }
    }
    return true;
}

QString ModelDownloader::etagsPath(const QString& dir) const
{
    return FS::PathCombine(dir, QString::fromLatin1(kETagsFileName));
}

QMap<QString, QString> ModelDownloader::loadETags(const QString& dir) const
{
    QMap<QString, QString> out;
    QFile f(etagsPath(dir));
    if (!f.open(QIODevice::ReadOnly))
        return out;
    const QJsonObject obj = QJsonDocument::fromJson(f.readAll()).object();
    for (auto it = obj.begin(); it != obj.end(); ++it)
        out.insert(it.key(), it.value().toString());
    return out;
}

void ModelDownloader::saveETags(const QString& dir, const QMap<QString, QString>& etags) const
{
    QJsonObject obj;
    for (auto it = etags.begin(); it != etags.end(); ++it)
        obj.insert(it.key(), it.value());
    QFile f(etagsPath(dir));
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return;
    f.write(QJsonDocument(obj).toJson(QJsonDocument::Compact));
}

bool ModelDownloader::isLargeModelFile(const QString& fileName)
{
    return fileName == QStringLiteral("intent-model-int8.onnx") ||
           fileName == QStringLiteral("tokenizer.json");
}

QString ModelDownloader::fetchRemoteETag(const QString& url)
{
    // Member manager (never APPLICATION->network()): reuses the TLS
    // connection across the 4 HEAD requests.
    QNetworkRequest request{QUrl(url)};
    request.setTransferTimeout(kHeadTimeoutMs);
    QNetworkReply* reply = m_headManager.head(request);
    if (!reply)
        return {};
    QEventLoop loop;
    QTimer timer;
    timer.setSingleShot(true);
    QObject::connect(&timer, &QTimer::timeout, reply, [reply, &loop] {
        reply->abort();
        loop.quit();
    });
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    timer.start(kHeadTimeoutMs);
    loop.exec();
    timer.stop();
    QString etag;
    if (reply->error() == QNetworkReply::NoError)
        etag = normalizeETag(QString::fromLatin1(reply->rawHeader("ETag")));
    reply->deleteLater();
    return etag;  // empty = offline/timeout/server gave none -> "no update"
}

QStringList ModelDownloader::checkForUpdates(const QString& dir, QMap<QString, QString>& remoteEtags)
{
    remoteEtags.clear();
    QStringList stale;
    // 1. Missing or truncated files are always stale.
    for (const auto& [name, minSize] : kRequiredFiles) {
        const QString fileName = QString::fromLatin1(name);
        const QFileInfo info(FS::PathCombine(dir, fileName));
        if (!info.exists() || info.size() < minSize)
            stale.append(fileName);
    }
    const QMap<QString, QString> stored = loadETags(dir);
    // Legacy install without .etags.json: establish a baseline now so
    // existing users are not force-redownloaded; the next run will
    // detect real changes. Missing files above still download.
    if (stored.isEmpty()) {
        QMap<QString, QString> baseline;
        for (const auto& [name, minSize] : kRequiredFiles) {
            Q_UNUSED(minSize);
            const QString fileName = QString::fromLatin1(name);
            const QString etag = fetchRemoteETag(m_baseUrl + "/" + fileName);
            if (!etag.isEmpty())
                baseline.insert(fileName, etag);
        }
        if (!baseline.isEmpty())
            saveETags(dir, baseline);
        remoteEtags = baseline;
        return stale;
    }
    // 2. All files present + baseline exists: HEAD each file.
    for (const auto& [name, minSize] : kRequiredFiles) {
        Q_UNUSED(minSize);
        const QString fileName = QString::fromLatin1(name);
        if (stale.contains(fileName))
            continue;  // already stale (missing), no need to HEAD
        const QString etag = fetchRemoteETag(m_baseUrl + "/" + fileName);
        if (etag.isEmpty())
            continue;  // offline/timeout/no header -> keep local files
        remoteEtags.insert(fileName, etag);
        if (stored.value(fileName) != etag)
            stale.append(fileName);
    }
    return stale;
}

void ModelDownloader::startDownload(QWidget* parent, const QStringList& files,
                                    const QMap<QString, QString>& remoteEtags)
{
    const QString dir = FS::PathCombine(APPLICATION->dataRoot(), "models");
    QDir().mkpath(dir);

    NetJob::Ptr job;
    job.reset(new NetJob(tr("Language model files"), APPLICATION->network()));
    m_job = job;
    for (const QString& fileName : files) {
        auto dl = Net::Download::makeFile(QUrl(m_baseUrl + "/" + fileName), FS::PathCombine(dir, fileName));
        m_job->addNetAction(dl);
    }

    ProgressDialog dialog(parent);
    QObject::connect(m_job.get(), &NetJob::succeeded, &dialog, [&, dir, files, remoteEtags, this] {
        QString reason;
        if (verifyFiles(dir, reason)) {
            // Merge only ETags already fetched by checkForUpdates().
            // No blocking HEAD here; missing entries are backfilled
            // on the next checkForUpdates() call.
            QMap<QString, QString> stored = loadETags(dir);
            for (const QString& fileName : files) {
                const QString etag = remoteEtags.value(fileName);
                if (!etag.isEmpty())
                    stored.insert(fileName, etag);
            }
            saveETags(dir, stored);
            m_checkedThisSession = true;
            emit ready(dir);
        } else {
            emit failed(reason);
        }
    });

    QObject::connect(m_job.get(), &NetJob::failed, this, [this](const QString& reason) {
        emit failed(reason);
    });
    dialog.execWithTask(m_job.get());
    m_job.reset();
}
