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

// Multilingual intent matcher: MiniLM-L12 embeddings + nearest neighbor
// over the precomputed table. Text in, command id out. Needs ONNX
// Runtime (wired up in Part 7); this file is not yet in the build.

#include <QObject>
#include <QHash>
#include <QString>

#include <vector>

class VoiceIntentMatcher : public QObject {
    Q_OBJECT

   public:
    struct Result {
        QString id;
        float similarity = 0.0f;  // cosine, query and table vectors normalized
        bool matched = false;     // similarity >= threshold && id != "__no_match__"
    };

    explicit VoiceIntentMatcher(QObject* parent = nullptr);
    ~VoiceIntentMatcher() override;

    // Loads embeddings.json and prepares the ONNX session.
    // The ONNX model itself loads in a worker thread; loaded() or
    // loadFailed() fires when done. match() before that returns no_match.
    void load(const QString& modelPath, const QString& embeddingsPath);

    // Nearest stored vector wins, any language. No language detection.
    Result match(const QString& text) const;

    bool isLoaded() const;
    void setThreshold(float threshold);  // default 0.68

    // Pure helpers, exposed for unit tests.
    static std::vector<QString> tokenizeText(const QString& text, const QHash<QString, int>& vocab,
                                             int unkId, int maxLength);
    static std::vector<float> meanPool(const std::vector<std::vector<float>>& tokenVectors,
                                       const std::vector<int>& mask);
    static float cosineSimilarity(const std::vector<float>& a, const std::vector<float>& b);
    static QString stripParaphraseSuffix(const QString& id);

   signals:
    void loaded();
    void loadFailed(const QString& reason);

   private:
    struct StoredVector {
        QString id;  // suffix-stripped command id
        std::vector<float> vec;
    };

    std::vector<float> embed(const QString& text) const;

    QString m_modelPath;
    float m_threshold = 0.68f;
    bool m_tableLoaded = false;
    bool m_sessionReady = false;

    QHash<QString, int> m_vocab;
    int m_unkId = 3;
    std::vector<StoredVector> m_table;

    struct OrtState;
    OrtState* m_ort = nullptr;
};
