// Command-registry dump helper for the embedding pipeline.
// Links against Launcher_logic, inflates MainWindow.ui onto a bare
// window (no Application singleton needed), calls
// CommandRegistry::collect(), and writes training/export/commands.json.
// Runnable in CI (QT_QPA_PLATFORM=offscreen) so embeddings regenerate
// automatically when translations change. Runtime-only entries
// (account list, theme switchers added in code) are not visible to a
// headless window and are absent from the dump.

#include <QApplication>
#include <QMainWindow>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTextStream>

#include "ui_MainWindow.h"
#include "ui/dialogs/CommandRegistry.h"

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);

    QMainWindow window;
    Ui::MainWindow ui;
    ui.setupUi(&window);
    const QList<CommandDescriptor> commands = CommandRegistry::collect(&window);

    QJsonArray arr;
    for (const auto& cmd : commands) {
        QJsonObject obj;
        obj["id"] = cmd.id;
        obj["text"] = cmd.text;
        obj["category"] = cmd.category;
        arr.append(obj);
    }
    QJsonObject root;
    root["version"] = 1;
    root["commands"] = arr;

    QFile out("training/export/commands.json");
    if (!out.open(QIODevice::WriteOnly)) {
        QTextStream(stderr) << "cannot write training/export/commands.json\n";
        return 1;
    }
    out.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    QTextStream(stdout) << "wrote training/export/commands.json (" << arr.size() << " commands)\n";
    return 0;
}
