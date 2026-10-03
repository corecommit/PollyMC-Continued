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

#pragma once

// Downloader for the intent-model files with ETag-based refresh.
// Large files (classifier onnx) never download without explicit user
// consent. Small metadata files (vocab/labels) refresh silently
// when the server ETag differs. Offline or HEAD failure falls back to
// whatever local files exist and never blocks the launcher.

#include <QNetworkAccessManager>
#include <QObject>
#include <QString>

#include "net/NetJob.h"

class ModelDownloader : public QObject {
    Q_OBJECT

   public:
    explicit ModelDownloader(QObject* parent = nullptr);
    ~ModelDownloader() override;

    // Emits ready(modelDir) immediately if all files are present,
    // otherwise asks for consent and downloads. Never prefetches.
    void ensureModelsPresent(QWidget* parent);

   signals:
    void ready(const QString& modelDir);
    void failed(const QString& reason);
    void checking();
    void downloading(qint64 bytesReceived, qint64 bytesTotal);

    private:
    // Full download of the given file names. Only overwrites local
    // files after the NetJob succeeds; never deletes on failure.
    // On success merges remoteEtags into .etags.json (best effort).
    void startDownload(QWidget* parent, const QStringList& files,
                       const QMap<QString, QString>& remoteEtags);
    bool verifyFiles(const QString& dir, QString& reason) const;
    // Returns names that are missing/truncated or whose server ETag
    // differs from .etags.json. Fills remoteEtags with the HEAD
    // results (empty value = HEAD failed, treat as "no update").
    // Never marks a present file stale on network failure.
    QStringList checkForUpdates(const QString& dir, QMap<QString, QString>& remoteEtags);
    // Synchronous HTTP HEAD via the member manager (never
    // APPLICATION->network()). Empty on error/timeout.
    QString fetchRemoteETag(const QString& url);
    QString etagsPath(const QString& dir) const;
    QMap<QString, QString> loadETags(const QString& dir) const;
    void saveETags(const QString& dir, const QMap<QString, QString>& etags) const;
    static bool isLargeModelFile(const QString& fileName);

    NetJob::Ptr m_job;
    QString m_baseUrl = QStringLiteral("https://huggingface.co/corecommit/PollyMC-Voice-Models/resolve/main");
    // Per-session cache: only one blocking HEAD round per successful
    // resolution. Set just before emit ready() (fresh check or verified
    // download), so declined consent or failed downloads retry next time.
    bool m_checkedThisSession = false;
    // Member manager reuses the TLS connection across the 4 HEADs.
    QNetworkAccessManager m_headManager;
    // Set once the user approves the large download this session, so a
    // failed verification retries without asking again. Lives and dies
    // with this downloader (i.e. the palette session).
    bool m_consentGiven = false;
};
