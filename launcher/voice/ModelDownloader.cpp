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
#include <QFileInfo>
#include <QMessageBox>

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
    QString reason;
    if (verifyFiles(dir, reason)) {
        emit ready(dir);
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
    startDownload(parent);
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

void ModelDownloader::startDownload(QWidget* parent)
{
    const QString dir = FS::PathCombine(APPLICATION->dataRoot(), "models");
    QDir().mkpath(dir);

    m_job = new NetJob(tr("Language model files"), APPLICATION->network());
    for (const auto& [name, minSize] : kRequiredFiles) {
        Q_UNUSED(minSize);
        const QString fileName = QString::fromLatin1(name);
        auto dl = Net::Download::makeFile(QUrl(m_baseUrl + "/" + fileName), FS::PathCombine(dir, fileName));
        m_job->addNetAction(dl);
    }

    ProgressDialog dialog(parent);
    QObject::connect(m_job.get(), &NetJob::succeeded, &dialog, [&, dir, this] {
        QString reason;
        if (verifyFiles(dir, reason)) {
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
