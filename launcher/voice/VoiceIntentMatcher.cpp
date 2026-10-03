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

#include "VoiceIntentMatcher.h"

#include <algorithm>
#include <cmath>

#include <QDebug>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTextStream>
#include <QtConcurrentRun>

#include <onnxruntime_cxx_api.h>

// Runtime WordPiece tokenizer for distilbert-base-multilingual-cased.
// Cased: no lowercasing anywhere. Train/serve skew concentrates in rare
// subwords; the confidence threshold absorbs most of it. If match
// quality disappoints, this is the first place to look.
namespace {
constexpr int kMaxLength = 64;

bool isWordChar(QChar c)
{
    return c.isLetterOrNumber() || c == '_';
}
}  // namespace

struct VoiceIntentMatcher::OrtState {
    Ort::Env env{ ORT_LOGGING_LEVEL_WARNING, "voice-intent" };
    Ort::SessionOptions options;
    std::unique_ptr<Ort::Session> session;
};

VoiceIntentMatcher::VoiceIntentMatcher(QObject* parent) : QObject(parent) {}

VoiceIntentMatcher::~VoiceIntentMatcher()
{
    delete m_ort;
}

bool VoiceIntentMatcher::isLoaded() const
{
    return m_labelsLoaded && m_sessionReady;
}

void VoiceIntentMatcher::setConfidenceThreshold(float t)
{
    m_confidenceThreshold = t;
}

std::vector<int64_t> VoiceIntentMatcher::wordpieceTokenize(const QString& text,
                                                           const QHash<QString, int>& vocab)
{
    // Ids verified against training/export/vocab.txt ([PAD]=0,
    // [UNK]=100, [CLS]=101, [SEP]=102); fallbacks only matter when the
    // vocab file failed to load.
    const int unkId = vocab.value(QStringLiteral("[UNK]"), 100);
    const int clsId = vocab.value(QStringLiteral("[CLS]"), 101);
    const int sepId = vocab.value(QStringLiteral("[SEP]"), 102);

    // Basic tokenization: whitespace separates words; any char that is
    // not letter/digit/underscore and not whitespace is its own token.
    // NOTE: no toLowerCase() — the model is cased.
    std::vector<QString> words;
    QString current;
    for (const QChar c : text) {
        if (c.isSpace()) {
            if (!current.isEmpty()) {
                words.push_back(current);
                current.clear();
            }
        } else if (isWordChar(c)) {
            current += c;
        } else {
            if (!current.isEmpty()) {
                words.push_back(current);
                current.clear();
            }
            words.push_back(QString(c));  // punctuation as its own token
        }
    }
    if (!current.isEmpty())
        words.push_back(current);

    std::vector<int64_t> body;
    for (const QString& word : words) {
        if (word.size() == 1 && !isWordChar(word.at(0))) {
            // Punctuation token: direct lookup, no "##" splitting.
            auto it = vocab.find(word);
            body.push_back(it != vocab.end() ? it.value() : unkId);
            continue;
        }
        // WordPiece: longest-match loop. Continuation pieces carry ##.
        // If any split fails, the whole word becomes [UNK] (HF behavior).
        int pos = 0;
        const int n = word.size();
        std::vector<int> wordIds;
        bool ok = true;
        while (pos < n) {
            int end = n;
            auto it = vocab.end();
            while (end > pos) {
                const QString piece = pos == 0 ? word.mid(pos, end - pos)
                                               : QStringLiteral("##") + word.mid(pos, end - pos);
                it = vocab.find(piece);
                if (it != vocab.end())
                    break;
                --end;
            }
            if (it == vocab.end()) {
                ok = false;
                break;
            }
            wordIds.push_back(it.value());
            pos = end;
        }
        if (!ok)
            body.push_back(unkId);
        else
            body.insert(body.end(), wordIds.begin(), wordIds.end());
    }
    if ((int)body.size() > kMaxLength - 2)
        body.resize(kMaxLength - 2);

    std::vector<int64_t> ids;
    ids.reserve(body.size() + 2);
    ids.push_back(clsId);
    ids.insert(ids.end(), body.begin(), body.end());
    ids.push_back(sepId);
    return ids;
}

void VoiceIntentMatcher::load(const QString& modelPath, const QString& labelsPath)
{
    m_modelPath = modelPath;
    m_labelsLoaded = false;
    m_sessionReady = false;
    m_vocab.clear();
    m_labelIds.clear();
    m_labelTexts.clear();
    m_numLabels = 0;

    // vocab.txt: one WordPiece token per line, line index = token id.
    // readLine() preserves indices exactly (no skipped lines); only a
    // missing file is tolerated (as the old tokenizer.json was).
    QFile vocabFile(QFileInfo(modelPath).dir().filePath(QStringLiteral("vocab.txt")));
    if (vocabFile.open(QIODevice::ReadOnly)) {
        QTextStream stream(&vocabFile);
        int id = 0;
        while (!stream.atEnd())
            m_vocab.insert(stream.readLine(), id++);
    } else {
        qWarning() << "VoiceIntentMatcher::load cannot open vocab file, continuing with empty vocabulary";
    }

    QFile labelsFile(labelsPath);
    if (!labelsFile.open(QIODevice::ReadOnly)) {
        emit loadFailed("cannot open labels file");
        return;
    }
    const QJsonObject root = QJsonDocument::fromJson(labelsFile.readAll()).object();
    if (root["version"].toInt(-1) != 2) {
        emit loadFailed("unsupported labels version");
        return;
    }
    const QJsonArray labels = root["labels"].toArray();
    for (const auto& entry : labels) {
        const QJsonObject obj = entry.toObject();
        m_labelIds.push_back(obj["id"].toString());
        m_labelTexts.push_back(obj["text"].toString());
    }
    if (m_labelIds.isEmpty()) {
        emit loadFailed("labels list is empty");
        return;
    }
    m_numLabels = m_labelIds.size();
    m_labelsLoaded = true;

    (void)QtConcurrent::run([this] {
        try {
            if (!m_ort)
                m_ort = new OrtState();
            m_ort->options.SetIntraOpNumThreads(1);
            m_ort->options.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
            // ORTCHAR_T is wchar_t on Windows, char elsewhere; there is
            // no const char* overload, so convert per platform. Named
            // locals: the pointer must outlive the constructor call.
#ifdef Q_OS_WIN
            const std::wstring modelPathNative = m_modelPath.toStdWString();
#else
            const std::string modelPathNative = m_modelPath.toStdString();
#endif
            m_ort->session = std::make_unique<Ort::Session>(m_ort->env, modelPathNative.c_str(),
                                                            m_ort->options);
            m_sessionReady = true;
            emit loaded();
        } catch (const Ort::Exception& e) {
            emit loadFailed(QString::fromUtf8(e.what()));
        } catch (...) {
            emit loadFailed("onnx session failed with unknown exception");
        }
    });
}

VoiceIntentMatcher::Result VoiceIntentMatcher::classify(const QString& text) const
{
    Result result;
    if (!isLoaded() || text.trimmed().isEmpty())
        return result;
    if (!m_ort || !m_ort->session) {
        qWarning() << "VoiceIntentMatcher::classify called without a session";
        return result;
    }
    try {
        std::vector<int64_t> tokenIds = wordpieceTokenize(text, m_vocab);
        if (tokenIds.empty() || m_numLabels <= 0) {
            qWarning() << "VoiceIntentMatcher::classify has no input or labels";
            return result;
        }
        std::vector<int64_t> mask(tokenIds.size(), 1);
        std::vector<int64_t> shape = { 1, (int64_t)tokenIds.size() };
        Ort::MemoryInfo mem = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
        std::vector<Ort::Value> inputs;
        inputs.push_back(Ort::Value::CreateTensor<int64_t>(mem, tokenIds.data(), tokenIds.size(), shape.data(),
                                                           shape.size()));
        inputs.push_back(Ort::Value::CreateTensor<int64_t>(mem, mask.data(), mask.size(), shape.data(), shape.size()));

        const char* inputNames[] = { "input_ids", "attention_mask" };
        const char* outputNames[] = { "logits" };
        std::vector<Ort::Value> outputs =
            m_ort->session->Run(Ort::RunOptions{ nullptr }, inputNames, inputs.data(), 2, outputNames, 1);

        float* logits = outputs[0].GetTensorMutableData<float>();
        // Softmax with max subtraction for numerical stability.
        float maxLogit = logits[0];
        for (int i = 1; i < m_numLabels; ++i)
            maxLogit = std::max(maxLogit, logits[i]);
        std::vector<float> probs(m_numLabels);
        float sum = 0.0f;
        for (int i = 0; i < m_numLabels; ++i) {
            probs[i] = std::exp(logits[i] - maxLogit);
            sum += probs[i];
        }
        int best = 0;
        for (int i = 0; i < m_numLabels; ++i) {
            probs[i] /= sum;
            if (probs[i] > probs[best])
                best = i;
        }
        result.id = m_labelIds.value(best);
        result.confidence = probs[best];
        result.matched = result.confidence >= m_confidenceThreshold && result.id != QStringLiteral("__no_match__");
        return result;
    } catch (const Ort::Exception& e) {
        qWarning() << "VoiceIntentMatcher::classify ONNX error:" << e.what();
        return Result();
    } catch (...) {
        qWarning() << "VoiceIntentMatcher::classify failed with unknown exception";
        return Result();
    }
}
