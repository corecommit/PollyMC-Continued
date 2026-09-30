#pragma once

#include <QWidget>

#include "BotScript.h"

class BotManagerDialog;

namespace Ui {
class BotScriptEditor;
}

// List editor for one bot script. Model/view over BotScriptStep;
// the tree model lives in the .cpp and is not exposed.
class BotScriptEditor : public QWidget {
    Q_OBJECT
   public:
    explicit BotScriptEditor(BotManagerDialog* manager, QWidget* parent = nullptr);
    ~BotScriptEditor() override;

    // Show this bot's assigned script, or a blank one if none is assigned.
    void setBot(const QString& botName, const QString& scriptName, bool connected);
    // Refresh run/stop gating without rebuilding the edited script.
    void setConnected(const QString& botName, bool connected);

   private slots:
    void onSelectionChanged();
    void onAddStep();
    void onRemoveStep();
    void onMoveStep(int direction);
    void onFieldEdited();
    void onSave();
    void onRevert();
    void onRun();
    void onStop();
    void onStoreChanged();

   private:
    void rebuildFromScript(const BotScript& script);
    BotScript currentScript() const;
    bool validate(QString& error) const;
    void refreshButtons();
    void showPropertiesFor();
    QModelIndex findFirstInvalidStep(const QModelIndex& parent, int depth, QString& error) const;

    BotManagerDialog* m_manager;
    QString m_botName;
    QString m_assignedScript;
    bool m_connected = false;
    bool m_saved = false;
    int m_nameCounter = 1;
    QList<BotScriptStep> m_steps;

    Ui::BotScriptEditor* m_ui;
};
