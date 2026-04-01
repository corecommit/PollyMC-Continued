// SPDX-License-Identifier: GPL-3.0-only
#include "DiscordRichPresence.h"
#include <QDateTime>
#include <QDir>
#include <QJsonDocument>
#include <QLocalSocket>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

enum DiscordOpcode : uint32_t {
    HANDSHAKE = 0,
    FRAME = 1,
    CLOSE = 2,
    PING = 3,
    PONG = 4,
};

DiscordRichPresence* DiscordRichPresence::instance()
{
    static DiscordRichPresence* inst = nullptr;
    if (!inst) {
        try {
            inst = new DiscordRichPresence();
        } catch (...) {
            // DRP not available
        }
    }
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
        sendPayload(data);
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
        sendPayload(data);
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
        pipeName = QString("\\\\.\\pipe\\discord-ipc-%1").arg(i);
#else
        // Discord may place its socket in XDG_RUNTIME_DIR, TMPDIR, or /tmp
        QString runtimeDir = qEnvironmentVariable("XDG_RUNTIME_DIR");
        if (runtimeDir.isEmpty())
            runtimeDir = QDir::tempPath();
        pipeName = runtimeDir + QString("/discord-ipc-%1").arg(i);
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

static QByteArray buildFrame(uint32_t opcode, const QByteArray& data)
{
    QByteArray frame;
    frame.reserve(8 + data.size());
    // Both fields are 4-byte little-endian as per Discord IPC spec
    frame.append(reinterpret_cast<const char*>(&opcode), sizeof(opcode));
    uint32_t len = static_cast<uint32_t>(data.size());
    frame.append(reinterpret_cast<const char*>(&len), sizeof(len));
    frame.append(data);
    return frame;
}

void DiscordRichPresence::sendHandshake()
{
    QJsonObject handshake;
    handshake["v"] = 1;
    handshake["client_id"] = APP_ID;

    QByteArray data = QJsonDocument(handshake).toJson(QJsonDocument::Compact);
    m_socket->write(buildFrame(HANDSHAKE, data));
    m_socket->flush();
}

void DiscordRichPresence::sendPayload(const QJsonObject& presence)
{
    if (m_socket->state() != QLocalSocket::ConnectedState)
        return;

    QByteArray data = QJsonDocument(presence).toJson(QJsonDocument::Compact);
    m_socket->write(buildFrame(FRAME, data));
    m_socket->flush();
}

void DiscordRichPresence::updatePresence(const QString& details, const QString& state,
                                          const QString& largeImageKey, const QString& largeImageText,
                                          const QString& smallImageKey, const QString& smallImageText,
                                          qint64 startTimeSecs)
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

    if (startTimeSecs > 0) {
        QJsonObject timestamps;
        timestamps["start"] = startTimeSecs;
        activity["timestamps"] = timestamps;
    }

    QJsonObject args;
    args["activity"] = activity;
    // pid is required by Discord so it can clean up presence on crash
    args["pid"] = static_cast<int>(QCoreApplication::applicationPid());

    QJsonObject cmd;
    cmd["cmd"] = "SET_ACTIVITY";
    cmd["args"] = args;
    cmd["nonce"] = QString::number(QDateTime::currentMSecsSinceEpoch());

    sendPayload(cmd);
}

void DiscordRichPresence::updatePlayingMinecraft(const QString& instanceName, const QString& mcVersion, qint64 startTime)
{
    updatePresence(
        QString("Playing %1").arg(instanceName),
        QString("Minecraft %1").arg(mcVersion),
        "pollymc", "PollyMC-Continued",
        "minecraft", "Minecraft",
        startTime  // now forwarded correctly
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
    // Loop only while we have a complete header AND the full payload in the buffer.
    // Previously the header was consumed before checking if the payload had arrived,
    // which permanently lost those 8 bytes and corrupted the stream on the next read.
    while (m_socket->bytesAvailable() >= 8) {
        // Peek at the header without removing it from the buffer
        QByteArray header = m_socket->peek(8);
        if (header.size() < 8)
            return;

        uint32_t opcode = *reinterpret_cast<const uint32_t*>(header.constData());
        uint32_t len    = *reinterpret_cast<const uint32_t*>(header.constData() + 4);

        // Guard against absurdly large frames to prevent OOM
        if (len > 1024 * 1024) {
            m_socket->disconnectFromServer();
            return;
        }

        // Only consume the header once we know the full payload is available
        if (m_socket->bytesAvailable() < static_cast<qint64>(8 + len))
            return;

        m_socket->read(8);  // discard header now that we're committed

        QByteArray payload;
        if (len > 0)
            payload = m_socket->read(len);

        QJsonDocument doc = QJsonDocument::fromJson(payload);
        if (doc.isNull())
            continue;

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
        } else if (opcode == CLOSE) {
            m_socket->disconnectFromServer();
            return;
        }
    }
}

void DiscordRichPresence::onDisconnected()
{
    m_connected = false;
    m_heartbeatTimer->stop();
}