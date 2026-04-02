// SPDX-License-Identifier: GPL-3.0-only
#pragma once

// Discord Rich Presence — dual transport implementation
//
// Transport 1: IPC socket  (discord-ipc-N)
//   Works with the Discord desktop app on Windows, macOS and Linux.
//   This is the standard RPC channel the desktop client exposes.
//
// Transport 2: WebSocket server  (ws://127.0.0.1:6463 … 6472)
//   Implements the arRPC bridge protocol so that Discord Web users
//   can receive Rich Presence without installing any extra software,
//   as long as they have the Vencord "WebRichPresence (arRPC)" plugin
//   enabled, or paste the arRPC bridge_mod.js snippet into DevTools.
//   See: https://github.com/OpenAsar/arrpc

#include <QCoreApplication>
#include <QJsonObject>
#include <QLocalSocket>
#include <QObject>
#include <QTimer>
#include <QWebSocketServer>
#include <QWebSocket>

class DiscordRichPresence : public QObject {
    Q_OBJECT

   public:
    static DiscordRichPresence* instance();

    void init();
    void shutdown();

    // Low-level update — all helpers call this.
    void updatePresence(const QString& details,
                        const QString& state,
                        const QString& largeImageKey  = QStringLiteral("pollymc"),
                        const QString& largeImageText = QStringLiteral("PollyMC-Continued"),
                        const QString& smallImageKey  = QString(),
                        const QString& smallImageText = QString(),
                        qint64         startTimeSecs  = 0);

    void updatePlayingMinecraft(const QString& instanceName,
                                const QString& mcVersion,
                                qint64         startTime);
    void updateIdle();
    void updateBrowsing();

    bool isIpcConnected() const { return m_ipcConnected; }
    bool isWsActive()     const { return m_wsServer && m_wsServer->isListening(); }

   signals:
    void ipcConnectedChanged(bool connected);

   private:
    explicit DiscordRichPresence(QObject* parent = nullptr);
    ~DiscordRichPresence() override;

    // ── IPC transport ────────────────────────────────────────────────────────
    void ipcConnect();
    void ipcSendHandshake();
    void ipcSendClose();
    void ipcSendFrame(const QJsonObject& payload);
    void ipcOnReadyRead();
    void ipcOnDisconnected();
    void ipcOnError(QLocalSocket::LocalSocketError error);
    void ipcSetConnected(bool connected);
    void ipcScheduleReconnect();

    QLocalSocket* m_ipcSocket         = nullptr;
    QTimer*       m_ipcReconnectTimer = nullptr;
    bool          m_ipcConnected      = false;

    static constexpr int IPC_RECONNECT_MIN_MS = 5'000;
    static constexpr int IPC_RECONNECT_MAX_MS = 60'000;
    int m_ipcReconnectMs = IPC_RECONNECT_MIN_MS;

    // ── WebSocket transport (arRPC bridge) ───────────────────────────────────
    void wsStart();
    void wsStop();
    void wsOnNewConnection();
    void wsOnMessage(const QString& message, QWebSocket* client);
    void wsOnClientDisconnected();
    void wsBroadcastPresence();

    QWebSocketServer*  m_wsServer  = nullptr;
    QList<QWebSocket*> m_wsClients;
    quint16            m_wsPort    = 0;

    // ── Shared state ─────────────────────────────────────────────────────────
    bool m_shuttingDown = false;

    struct PresenceCache {
        QString details;
        QString state;
        QString largeImageKey  = QStringLiteral("pollymc");
        QString largeImageText = QStringLiteral("PollyMC-Continued");
        QString smallImageKey;
        QString smallImageText;
        qint64  startTimeSecs  = 0;
        bool    valid          = false;
    } m_lastPresence;

    QJsonObject buildActivityJson() const;

    static constexpr const char* APP_ID = "1356656498988818502";
};
