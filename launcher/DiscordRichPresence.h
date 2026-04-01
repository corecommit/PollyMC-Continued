// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <QCoreApplication>
#include <QJsonObject>
#include <QLocalSocket>
#include <QObject>
#include <QTimer>

class DiscordRichPresence : public QObject {
    Q_OBJECT

   public:
    static DiscordRichPresence* instance();

    void init();
    void shutdown();

    // startTimeSecs: Unix timestamp (seconds) for the "elapsed" timer; pass 0 to omit.
    void updatePresence(const QString& details, const QString& state,
                        const QString& largeImageKey = "pollymc",
                        const QString& largeImageText = "PollyMC-Continued",
                        const QString& smallImageKey = "",
                        const QString& smallImageText = "",
                        qint64 startTimeSecs = 0);

    void updatePlayingMinecraft(const QString& instanceName, const QString& mcVersion, qint64 startTime);
    void updateIdle();
    void updateBrowsing();

    bool isConnected() const { return m_connected; }

   private:
    explicit DiscordRichPresence(QObject* parent = nullptr);
    ~DiscordRichPresence();

    void connectToDiscord();
    void sendHandshake();
    void sendPayload(const QJsonObject& presence);  // renamed from sendPresence to avoid confusion
    void readResponse();
    void onDisconnected();

    QLocalSocket* m_socket = nullptr;
    QTimer* m_reconnectTimer = nullptr;
    QTimer* m_heartbeatTimer = nullptr;
    bool m_connected = false;
    int m_pipe = -1;

    static constexpr const char* APP_ID = "1356656498988818502";
};