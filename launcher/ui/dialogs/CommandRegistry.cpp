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

#include "CommandRegistry.h"

#include <QAction>
#include <QMainWindow>
#include <QSet>

namespace {
QString paletteCategory(const QString& name)
{
    // instance-scoped verbs stay under Instance even though they are
    // also plain actions; keeps per-instance behavior discoverable
    if (name == "actionLaunchInstance" || name == "actionEditInstance" || name == "actionDeleteInstance" ||
        name == "actionCopyInstance" || name == "actionKillInstance" || name == "actionExportInstanceZip" ||
        name == "actionExportInstanceMrPack" || name == "actionExportInstanceFlamePack")
        return "Instance";
    if (name == "actionAddInstance" || name == "actionSettings" || name == "actionAbout" ||
        name == "actionCheckUpdate")
        return "Action";
    if (name.contains("Theme"))
        return "Theme";
    if (name.contains("Toolbar") || name.contains("StatusBar") || name == "actionLockToolbars")
        return "Toolbar";
    if (name.contains("Account"))
        return "Account";
    if (name.contains("DISCORD") || name.contains("MATRIX") || name.contains("REDDIT") || name.contains("Wiki") ||
        name.contains("ReportBug") || name.contains("BugTracker"))
        return "External";
    if (name.contains("Folder"))
        return "Folder";
    return "Other";
}

int palettePriority(const QString& name)
{
    if (name == "actionAddInstance" || name == "actionSettings" || name == "actionLaunchInstance")
        return 100;
    if (name == "actionEditInstance" || name == "actionDeleteInstance" || name == "actionAbout")
        return 80;
    if (name == "actionExportInstanceZip" || name == "actionExportInstanceMrPack" ||
        name == "actionExportInstanceFlamePack" || name == "actionViewInstanceFolder" ||
        name == "actionViewLogsFolder" || name == "actionViewCentralModsFolder")
        return 60;
    return 0;
}
}  // namespace

CommandDescriptor CommandDescriptor::fromAction(QAction* action)
{
    CommandDescriptor entry;
    entry.id = action->objectName();
    entry.text = QString(action->text()).remove('&');
    if (entry.text.isEmpty())
        entry.text = entry.id;
    if (entry.id.isEmpty())
        entry.id = entry.text;
    entry.shortcut = action->shortcut();
    entry.sourceAction = action;
    entry.isEnabled = [action] { return action->isEnabled(); };
    entry.trigger = [action] { action->trigger(); };
    return entry;
}

static QList<CommandDescriptor> s_lastCollected;

QList<CommandDescriptor> CommandRegistry::collect(QMainWindow* window)
{
    QList<CommandDescriptor> entries;
    QSet<QAction*> seen;
    auto consider = [&entries, &seen](QAction* action) {
        if (!action || seen.contains(action) || action->isSeparator() || action->menu())
            return;
        if (action->text().isEmpty() && action->objectName().isEmpty())
            return;
        if (!action->isVisible())
            return;
        seen.insert(action);
        CommandDescriptor entry = CommandDescriptor::fromAction(action);
        const QString name = action->objectName();
        entry.category = paletteCategory(name);
        entry.priority = palettePriority(name);
        entry.hideByDefault = entry.category == "Theme" || entry.category == "Toolbar";
        entries.append(entry);
    };
    // findChildren on the window covers menus and toolbars alike
    for (QAction* action : window->findChildren<QAction*>())
        consider(action);

    s_lastCollected = entries;
    return entries;
}

CommandDescriptor CommandRegistry::byId(const QString& id)
{
    for (const auto& entry : s_lastCollected) {
        if (entry.id == id)
            return entry;
    }
    return {};
}
