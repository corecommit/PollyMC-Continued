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

#include <QDialog>

#include <QList>

#include "CommandRegistry.h"

class QLineEdit;
class QListView;
class QShowEvent;
class QLabel;
class QTimer;
class ShortcutDelegate;
class VoiceIntentMatcher;
class ModelDownloader;
class MainWindow;

// VS Code style command palette over a CommandDescriptor list.
// Entries come from CommandRegistry; the palette owns display and
// filtering only. Actions must outlive the palette.
class CommandPalette : public QDialog {
    Q_OBJECT

   public:
    explicit CommandPalette(QList<CommandDescriptor> entries, MainWindow* mainWindow, QWidget* parent);
    ~CommandPalette() override;

   protected:
    void showEvent(QShowEvent* event) override;
    bool eventFilter(QObject* obj, QEvent* event) override;

   private slots:
    void onQueryChanged(const QString& query);
    void runSelected();
    void runIntentMatcher();
    void onModelsReady(const QString& modelDir);
    void onModelsFailed(const QString& reason);
    void onMatcherReady();
    void onMatcherFailed(const QString& reason);

   private:
    void selectFirstRow();
    void placeOverParent();
    void setSuggestionRow(const CommandDescriptor& entry);
    void matchAndSuggest(const QString& query);

    class Model;
    class FuzzyFilter;

    QLineEdit* m_search = nullptr;
    QLabel* m_badge = nullptr;
    QListView* m_list = nullptr;
    QLabel* m_count = nullptr;
    ShortcutDelegate* m_delegate = nullptr;
    Model* m_model = nullptr;
    FuzzyFilter* m_filter = nullptr;
    QTimer* m_debounce = nullptr;
    bool m_naturalLanguageMode = false;
    QString m_strippedQuery;
    VoiceIntentMatcher* m_matcher = nullptr;
    ModelDownloader* m_downloader = nullptr;
    MainWindow* m_mainWindow = nullptr;
    bool m_modelsReady = false;
    QString m_modelDir;
};
