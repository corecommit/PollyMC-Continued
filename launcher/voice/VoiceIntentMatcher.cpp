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

#include <cmath>

#include <string>

#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QtConcurrentRun>

#include <onnxruntime_cxx_api.h>

// Tokenizer caveat: build time uses the real XLM-R SentencePiece
// tokenizer (HF tokenizers). This runtime tokenizer is intentionally
// simplified (lowercase, punctuation split, vocab lookup, ids clipped
// to 128) so it needs no sentencepiece dependency. Train/serve skew
// concentrates in rare subwords; the threshold absorbs most of it.
// If match quality disappoints, this is the first place to look.
namespace {
constexpr int kMaxLength = 128;
constexpr int kDim = 384;

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
    return m_tableLoaded && m_sessionReady;
}

void VoiceIntentMatcher::setThreshold(float threshold)
{
    m_threshold = threshold;
}

QString VoiceIntentMatcher::stripParaphraseSuffix(const QString& id)
{
    const int tilde = id.indexOf('~');
    return tilde == -1 ? id : id.left(tilde);
}

std::vector<QString> VoiceIntentMatcher::tokenizeText(const QString& text, const QHash<QString, int>& vocab,
                                                      int unkId, int maxLength)
{
    Q_UNUSED(unkId);
    Q_UNUSED(vocab);
    // word pieces carry the sentencepiece ▁ marker. Pieces missing from
    // the vocab (including whole unknown words) are kept as-is and mapped
    // to the [UNK] id by embed(), so no word is ever silently dropped.
    std::vector<QString> pieces;
    QString current;
    const QString lowered = text.toLower();
    auto flush = [&] {
        if (current.isEmpty())
            return;
        // kept unconditionally; embed() maps vocab misses to UNK
        pieces.push_back(QString(QChar(0x2581)) + current);
        current.clear();
    };
    for (const QChar c : lowered) {
        if (c.isSpace()) {
            flush();
        } else if (isWordChar(c)) {
            current += c;
        } else {
            flush();
            pieces.push_back(QString(c));  // punctuation as its own piece
        }
    }
    flush();
    // only empty markers are filtered; every other piece goes to embed(),
    // which maps vocabulary misses (including whole unknown words) to UNK
    std::vector<QString> resolved;
    for (const auto& piece : pieces) {
        if (piece.isEmpty())
            continue;
        resolved.push_back(piece);
    }
    if ((int)resolved.size() > maxLength)
        resolved.resize(maxLength);
    return resolved;
}

std::vector<float> VoiceIntentMatcher::meanPool(const std::vector<std::vector<float>>& tokenVectors,
                                               const std::vector<int>& mask)
{
    std::vector<float> out(kDim, 0.0f);
    float total = 0.0f;
    for (size_t i = 0; i < tokenVectors.size() && i < mask.size(); i++) {
        if (!mask[i])
            continue;
        total += 1.0f;
        for (int d = 0; d < kDim && d < (int)tokenVectors[i].size(); d++)
            out[d] += tokenVectors[i][d];
    }
    if (total > 0) {
        for (auto& v : out)
            v /= total;
    }
    float norm = 0.0f;
    for (auto v : out)
        norm += v * v;
    norm = std::sqrt(norm);
    if (norm > 0) {
        for (auto& v : out)
            v /= norm;
    }
    return out;
}

float VoiceIntentMatcher::cosineSimilarity(const std::vector<float>& a, const std::vector<float>& b)
{
    float dot = 0.0f;
    const size_t n = std::min(a.size(), b.size());
    for (size_t i = 0; i < n; i++)
        dot += a[i] * b[i];
    return dot;  // inputs are L2-normalized, so dot == cosine
}

void VoiceIntentMatcher::load(const QString& modelPath, const QString& embeddingsPath)
{
    m_modelPath = modelPath;
    m_tableLoaded = false;
    m_sessionReady = false;
    m_table.clear();

    QFile embFile(embeddingsPath);
    if (!embFile.open(QIODevice::ReadOnly)) {
        emit loadFailed("cannot open embeddings file");
        return;
    }
    const QJsonObject root = QJsonDocument::fromJson(embFile.readAll()).object();
    if (root["version"].toInt(-1) != 1) {
        emit loadFailed("unsupported embeddings version");
        return;
    }
    const QJsonObject entries = root["entries"].toObject();
    for (auto it = entries.begin(); it != entries.end(); ++it) {
        const QString id = stripParaphraseSuffix(it.key());
        const QJsonObject langs = it.value().toObject();
        for (auto lt = langs.begin(); lt != langs.end(); ++lt) {
            StoredVector stored;
            stored.id = id;
            const QJsonArray arr = lt.value().toArray();
            for (const auto& v : arr)
                stored.vec.push_back(float(v.toDouble()));
            if ((int)stored.vec.size() == kDim)
                m_table.push_back(std::move(stored));
        }
    }
    if (m_table.empty()) {
        emit loadFailed("embeddings table is empty");
        return;
    }
    m_tableLoaded = true;

    // Tokenizer vocab lives next to the model as tokenizer.json
    // (HF format, model.vocab list of [token, score] pairs).
    QFile tokFile(QFileInfo(modelPath).dir().filePath("tokenizer.json"));
    if (tokFile.open(QIODevice::ReadOnly)) {
        const QJsonObject tok = QJsonDocument::fromJson(tokFile.readAll()).object();
        const QJsonArray vocab = tok["model"].toObject()["vocab"].toArray();
        for (const auto& entry : vocab) {
            const QJsonArray pair = entry.toArray();
            if (pair.size() == 2)
                m_vocab[pair[0].toString()] = pair[1].toInt();
        }
    }

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
        }
    });
}

std::vector<float> VoiceIntentMatcher::embed(const QString& text) const
{
    if (!m_ort || !m_ort->session)
        return std::vector<float>(kDim, 0.0f);

    try {
        std::vector<QString> pieces = tokenizeText(text, m_vocab, m_unkId, kMaxLength);
        std::vector<int64_t> ids;
        std::vector<int64_t> mask;
        for (const auto& piece : pieces) {
            if (piece.isEmpty())
                continue;
            auto it = m_vocab.find(piece);
            ids.push_back(it != m_vocab.end() ? *it : m_unkId);
            mask.push_back(1);
        }
        if (ids.empty())
            return std::vector<float>(kDim, 0.0f);

        std::vector<int64_t> shape = { 1, (int64_t)ids.size() };
        
        std::vector<int64_t> tokenTypeIds(ids.size(), 0);

        Ort::MemoryInfo mem = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
        std::vector<Ort::Value> inputs;
        inputs.push_back(Ort::Value::CreateTensor<int64_t>(mem, ids.data(), ids.size(), shape.data(), shape.size()));
        inputs.push_back(Ort::Value::CreateTensor<int64_t>(mem, mask.data(), mask.size(), shape.data(), shape.size()));
        inputs.push_back(Ort::Value::CreateTensor<int64_t>(mem, tokenTypeIds.data(), tokenTypeIds.size(), shape.data(), shape.size()));

        const char* inputNames[] = { "input_ids", "attention_mask", "token_type_ids" };
        const char* outputNames[] = { "last_hidden_state" };
        
        std::vector<Ort::Value> outputs =
            m_ort->session->Run(Ort::RunOptions{ nullptr }, inputNames, inputs.data(), 3, outputNames, 1);

        float* data = outputs[0].GetTensorMutableData<float>();
        const size_t seq = ids.size();
        std::vector<std::vector<float>> tokenVectors(seq, std::vector<float>(kDim));
        for (size_t i = 0; i < seq; i++)
            for (int d = 0; d < kDim; d++)
                tokenVectors[i][d] = data[i * kDim + d];
        std::vector<int> intMask(mask.begin(), mask.end());
        return meanPool(tokenVectors, intMask);
    }
    catch (const Ort::Exception& e) {
        qWarning() << "VoiceIntentMatcher::embed ONNX error:" << e.what();
        return std::vector<float>(kDim, 0.0f);
    }
    catch (...) {
        qWarning() << "VoiceIntentMatcher::embed failed with unknown exception";
        return std::vector<float>(kDim, 0.0f);
    }
}
VoiceIntentMatcher::Result VoiceIntentMatcher::match(const QString& text) const
{
    Result result;
    if (!isLoaded() || text.trimmed().isEmpty())
        return result;
    const std::vector<float> query = embed(text);
    float best = -2.0f;
    QString bestId;
    for (const auto& stored : m_table) {
        const float score = cosineSimilarity(query, stored.vec);
        if (score > best) {
            best = score;
            bestId = stored.id;
        }
    }
    result.id = bestId;
    result.similarity = best;
    result.matched = best >= m_threshold && bestId != "__no_match__";
    return result;
}
