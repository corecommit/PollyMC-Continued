// SPDX-License-Identifier: GPL-3.0-only
#include "DiscordRichPresence.h"
#include <QDateTime>
#include <QDir>
#include <QJsonDocument>
#include <QLocalSocket>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

enum DiscordOpcode {
    HANDSHAKE = 0,
    FRAME = 1,
    CLOSE = 2,
    PING = 3,
    PONG = 4,
};

DiscordRichPresence* DiscordRichPresence::instance()
{
    static DiscordRichPresence* inst = new DiscordRichPresence();
    return inst;
}

DiscordRichPresence::DiscordRichPresence(QObject* parent) : QObject(parent)
{
    m_socket = new QLocalSocket(this);
    m_reconnectTimer = new QTimer(this);
    m_reconnectTimer->setInterval(10000);  // Try reconnect every 10 seconds
    connect(m_reconnectTimer, &QTimer::timeout, this, &DiscordRichPresence::connectToDiscord);

    m_heartbeatTimer = new QTimer(this);
    m_heartbeatTimer->setInterval(30000);  // Heartbeat every 30 seconds
    connect(m_heartbeatTimer, &QTimer::timeout, this, [this]() {
        QJsonObject data;
        data["cmd"] = "PING";
        QJsonObject payload;
        payload["nonce"] = QString::number(QDateTime::currentMSecsSinceEpoch());
        data["args"] = payload;
        sendPresence(data);
    });

    connect(m_socket, &QLocalSocket::readyRead, this, &DiscordRichPresence::readResponse);
    connect(m_socket, &QLocalSocket::disconnected, this, &DiscordRichPresence::onDisconnected);
}

DiscordRichPresence::~DiscordRichPresence()
{
    shutdown();
}

void DiscordRichPresence::init()
{
    connectToDiscord();
    m_reconnectTimer->start();
}

void DiscordRichPresence::shutdown()
{
    m_reconnectTimer->stop();
    m_heartbeatTimer->stop();
    if (m_socket->state() == QLocalSocket::ConnectedState) {
        // Send close frame
        QJsonObject data;
        data["cmd"] = "DISPATCH";
        data["evt"] = "CLOSE";
        sendPresence(data);
        m_socket->disconnectFromServer();
    }
    m_connected = false;
}

void DiscordRichPresence::connectToDiscord()
{
    if (m_socket->state() == QLocalSocket::ConnectedState ||
        m_socket->state() == QLocalSocket::ConnectingState) {
        return;
    }

    // Try to connect to Discord's IPC pipe
    for (int i = 0; i < 10; i++) {
        QString pipeName;
#ifdef Q_OS_WIN
        pipeName = QString("\\\\?\\pipe\\discord-ipc-%1").arg(i);
#else
        pipeName = QDir::tempPath() + QString("/discord-ipc-%1").arg(i);
#endif
        m_socket->connectToServer(pipeName);
        if (m_socket->waitForConnected(500)) {
            m_connected = true;
            m_pipe = i;
            sendHandshake();
            m_heartbeatTimer->start();
            return;
        }
    }
    m_connected = false;
}

void DiscordRichPresence::sendHandshake()
{
    QJsonObject handshake;
    handshake["v"] = 1;
    handshake["client_id"] = APP_ID;

    QJsonDocument doc(handshake);
    QByteArray data = doc.toJson(QJsonDocument::Compact);

    // Discord IPC frame: opcode (4 bytes LE) + length (4 bytes LE) + data
    QByteArray frame;
    int op = HANDSHAKE;
    frame.append(reinterpret_cast<const char*>(&op), 4);
    uint32_t len = data.size();
    frame.append(reinterpret_cast<const char*>(&len), 4);
    frame.append(data);

    m_socket->write(frame);
    m_socket->flush();
}

void DiscordRichPresence::sendPresence(const QJsonObject& presence)
{
    QJsonDocument doc(presence);
    QByteArray data = doc.toJson(QJsonDocument::Compact);

    QByteArray frame;
    int op = FRAME;
    frame.append(reinterpret_cast<const char*>(&op), 4);
    uint32_t len = data.size();
    frame.append(reinterpret_cast<const char*>(&len), 4);
    frame.append(data);

    if (m_socket->state() == QLocalSocket::ConnectedState) {
        m_socket->write(frame);
        m_socket->flush();
    }
}

void DiscordRichPresence::updatePresence(const QString& details, const QString& state,
                                          const QString& largeImageKey, const QString& largeImageText,
                                          const QString& smallImageKey, const QString& smallImageText)
{
    if (!m_connected) return;

    QJsonObject assets;
    assets["large_image"] = largeImageKey;
    assets["large_text"] = largeImageText;
    if (!smallImageKey.isEmpty()) {
        assets["small_image"] = smallImageKey;
        assets["small_text"] = smallImageText;
    }

    QJsonObject activity;
    activity["details"] = details;
    activity["state"] = state;
    activity["assets"] = assets;

    QJsonObject args;
    args["activity"] = activity;

    QJsonObject cmd;
    cmd["cmd"] = "SET_ACTIVITY";
    cmd["args"] = args;
    cmd["nonce"] = QString::number(QDateTime::currentMSecsSinceEpoch());

    sendPresence(cmd);
}

void DiscordRichPresence::updatePlayingMinecraft(const QString& instanceName, const QString& mcVersion, qint64 startTime)
{
    updatePresence(
        QString("Playing %1").arg(instanceName),
        QString("Minecraft %1").arg(mcVersion),
        "pollymc", "PollyMC-Continued",
        "minecraft", "Minecraft"
    );
}

void DiscordRichPresence::updateIdle()
{
    updatePresence("Idle", "In launcher", "pollymc", "PollyMC-Continued");
}

void DiscordRichPresence::updateBrowsing()
{
    updatePresence("Browsing", "Looking for mods", "pollymc", "PollyMC-Continued");
}

void DiscordRichPresence::readResponse()
{
    while (m_socket->bytesAvailable() >= 8) {
        QByteArray header = m_socket->read(8);
        if (header.size() < 8) return;

        int opcode = *reinterpret_cast<const int*>(header.constData());
        uint32_t len = *reinterpret_cast<const uint32_t*>(header.constData() + 4);

        if (len > 0 && m_socket->bytesAvailable() >= len) {
            QByteArray payload = m_socket->read(len);
            QJsonDocument doc = QJsonDocument::fromJson(payload);
            QJsonObject obj = doc.object();

            if (opcode == FRAME) {
                QString cmd = obj["cmd"].toString();
                if (cmd == "DISPATCH") {
                    QString evt = obj["evt"].toString();
                    if (evt == "READY") {
                        m_connected = true;
                        updateIdle();
                    }
                }
            }
        }
    }
}

void DiscordRichPresence::onDisconnected()
{
    m_connected = false;
    m_heartbeatTimer->stop();
}
