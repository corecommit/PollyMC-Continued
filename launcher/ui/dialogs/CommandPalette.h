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

#include <QKeySequence>
#include <QList>

#include <functional>

class QAction;
class QLineEdit;
class QListView;
class QShowEvent;

// One searchable command. v1 fills these from QActions; v2 can add
// dynamic entries (e.g. per-instance) without touching the palette.
struct CommandEntry {
    QString text;
    QKeySequence shortcut;
    std::function<bool()> isEnabled;
    std::function<void()> trigger;
    QAction* sourceAction = nullptr;
    int weight = 0;
    bool closeAfterTrigger = true;

    // v1 static set adapter. The palette never owns the action.
    static CommandEntry fromAction(QAction* action);
};

// VS Code style command palette over a CommandEntry list.
// The palette owns the entries but never the underlying actions.
class CommandPalette : public QDialog {
    Q_OBJECT

   public:
    explicit CommandPalette(QList<CommandEntry> entries, QWidget* parent);
    ~CommandPalette() override;

   protected:
    void showEvent(QShowEvent* event) override;
    bool eventFilter(QObject* obj, QEvent* event) override;

   private slots:
    void onQueryChanged(const QString& query);
    void runSelected();

   private:
    void selectFirstRow();

    class Model;
    class FuzzyFilter;

    QLineEdit* m_search = nullptr;
    QListView* m_list = nullptr;
    Model* m_model = nullptr;
    FuzzyFilter* m_filter = nullptr;
};
