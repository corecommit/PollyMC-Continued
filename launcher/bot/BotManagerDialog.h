#pragma once

#include <QWidget>
#include <QTableWidget>
#include <QPlainTextEdit>
#include <QLineEdit>
#include <QPushButton>
#include <QLabel>
#include <QJsonObject>
#include <QVector>

#include "BotProcess.h"
#include "AddBotDialog.h"

class QCompleter;

struct BotEntry {
    BotConfig config;
    bool connected = false;
    int colorIndex = 0;
};

class BotManagerDialog : public QWidget {
    Q_OBJECT
public:
    explicit BotManagerDialog(QWidget* parent = nullptr);
    ~BotManagerDialog() override;

private slots:
    void onAddBot();
    void onEditBot();
    void onRemoveBot();
    void onStart();
    void onSelectAll();
    void onStop();
    void onTableDoubleClicked(int row, int column);
    void onSelectionChanged();
    void onSendCommand();
    void appendLog(const QString& text);
    void appendError(const QString& text);
    void onBotReady();
    void onBotConnected(const QString& username, const QString& server);
    void onBotChat(const QString& bot, const QString& from, const QString& message);
    void onProcessExited(int code);
    void onDependenciesInstalled(bool ok);

private:
    void startBotServer();
    void ensureBotDependencies();
    void showHelp();
    void saveConfigs();
    void loadConfigs();
    void refreshTable();
    void connectBot(int index);
    void disconnectBot(int index);
    BotEntry* currentBot();
    QVector<BotEntry*> selectedBots();

    BotProcess* m_bot = nullptr;
    QVector<BotEntry> m_bots;

    QTableWidget* m_table;
    QPlainTextEdit* m_log;
    QLineEdit* m_input;
    QCompleter* m_completer;
    QPushButton* m_sendBtn;
    QLabel* m_statusLabel;
    QPushButton* m_addBtn;
    QPushButton* m_editBtn;
    QPushButton* m_removeBtn;
    QPushButton* m_startBtn;
    QPushButton* m_selectAllBtn;
    QPushButton* m_stopAllBtn;
    QPushButton* m_commandsBtn;

    QString m_configPath;
};
