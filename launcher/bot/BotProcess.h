#pragma once

#include <QObject>
#include <QProcess>
#include <QJsonObject>

class BotProcess : public QObject {
    Q_OBJECT
public:
    explicit BotProcess(QObject* parent = nullptr);
    ~BotProcess() override;

    void start();
    void stop();
    bool isRunning() const;

    bool hasDependencies() const;
    bool hasNode() const;
    void installDependencies();

    void sendCommand(const QString& cmd, const QJsonObject& params = {});

signals:
    void logMessage(const QString& text);
    void botConnected(const QString& username, const QString& server);
    void botChat(const QString& bot, const QString& from, const QString& message);
    void errorMessage(const QString& text);
    void ready();
    void processExited(int code);
    void dependenciesInstalled(bool ok);

private slots:
    void onStdoutReady();
    void onStderrReady();
    void onProcessError(QProcess::ProcessError error);
    void onProcessFinished(int exitCode, QProcess::ExitStatus status);
    void onInstallFinished(int exitCode, QProcess::ExitStatus status);

private:
    void handleMessage(const QJsonObject& msg);
    QString findNodePath() const;
    QString findBotServerDir() const;

    QProcess* m_process = nullptr;
    QProcess* m_installProcess = nullptr;
    QString m_buffer;
};
