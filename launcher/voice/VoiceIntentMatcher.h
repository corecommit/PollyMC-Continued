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

// Intent classifier: DistilBERT WordPiece + argmax over command labels.
// Text in, command id out. Needs ONNX Runtime.

#include <QObject>
#include <QHash>
#include <QString>
#include <QVector>

#include <vector>
#include <cstdint>

class VoiceIntentMatcher : public QObject {
    Q_OBJECT

   public:
    struct Result {
        QString id;
        float confidence = 0.0f;  // softmax prob of winning class
        bool matched = false;     // confidence >= threshold && id != "__no_match__"
    };

    explicit VoiceIntentMatcher(QObject* parent = nullptr);
    ~VoiceIntentMatcher() override;

    // Loads labels.json and prepares the ONNX session.
    // The ONNX model itself loads in a worker thread; loaded() or
    // loadFailed() fires when done. classify() before that returns no_match.
    void load(const QString& modelPath, const QString& labelsPath);

    // Argmax over classifier logits. No language detection.
    Result classify(const QString& text) const;

    bool isLoaded() const;
    void setConfidenceThreshold(float t);   // default 0.6f

    // WordPiece tokenizer for distilbert-base-multilingual-cased,
    // exposed for unit tests. Returns token ids including [CLS]/[SEP],
    // truncated to 64 total. Cased: input is never lowercased.
    static std::vector<int64_t> wordpieceTokenize(const QString& text,
                                                  const QHash<QString, int>& vocab);

   signals:
    void loaded();
    void loadFailed(const QString& reason);

   private:
    QString m_modelPath;
    float m_confidenceThreshold = 0.6f;
    bool m_labelsLoaded = false;
    bool m_sessionReady = false;

    QHash<QString, int> m_vocab;     // WordPiece token -> id
    QVector<QString> m_labelIds;     // class index -> command id
    QVector<QString> m_labelTexts;   // class index -> display text
    int m_numLabels = 0;

    struct OrtState;
    OrtState* m_ort = nullptr;
};
