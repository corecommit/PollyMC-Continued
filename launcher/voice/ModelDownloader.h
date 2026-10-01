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

// One-time downloader for the intent-model files. Nothing downloads
// without explicit user consent. Not registered in CMake yet; Part 7
// owns build integration alongside ONNX Runtime.

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

   private:
    void startDownload(QWidget* parent);
    bool verifyFiles(const QString& dir, QString& reason) const;

    NetJob::Ptr m_job;
    QString m_baseUrl = QStringLiteral("https://huggingface.co/corecommit/PollyMC-Voice-Models/resolve/main");
};
