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

// Canonical command description, shared by the palette, the embedding
// builder, and the runtime matcher. The palette never owns actions.

#include <QKeySequence>
#include <QList>
#include <QString>

#include <functional>

class QAction;
class QMainWindow;

struct CommandDescriptor {
    QString id;  // stable, unique: the QAction objectName (or text fallback)
    QString text;
    QKeySequence shortcut;
    QString category = QStringLiteral("Other");
    QAction* sourceAction = nullptr;
    std::function<bool()> isEnabled;
    std::function<void()> trigger;
    int weight = 0;
    int priority = 0;
    bool hideByDefault = false;
    bool closeAfterTrigger = true;

    static CommandDescriptor fromAction(QAction* action);
};

class CommandRegistry {
   public:
    // Every command the palette would show right now. Same filter
    // logic (skip separators, submenus, hidden, empty names).
    // Takes a QMainWindow (not MainWindow) so the headless dump tool
    // can inflate MainWindow.ui without the Application singleton.
    static QList<CommandDescriptor> collect(QMainWindow* window);

    // Look up by ID in the most recent collect() result.
    static CommandDescriptor byId(const QString& id);
};
