// SPDX-License-Identifier: GPL-3.0-only
#include "DiscordRichPresence.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QJsonDocument>
#include <QLoggingCategory>
#include <QWebSocket>
#include <QWebSocketServer>

Q_LOGGING_CATEGORY(drp, "launcher.discord")

// ============================================================================
// Discord IPC wire format
//   [opcode: uint32 LE][length: uint32 LE][JSON payload]
// ============================================================================
enum DiscordOpcode : uint32_t { HANDSHAKE = 0, FRAME = 1, CLOSE = 2, PING = 3, PONG = 4 };
static constexpr qint64 MAX_FRAME_BYTES = 1024 * 1024;

static QByteArray buildIpcFrame(DiscordOpcode opcode, const QByteArray& json)
{
    QByteArray frame;
    frame.reserve(8 + json.size());
    const uint32_t op  = static_cast<uint32_t>(opcode);
    const uint32_t len = static_cast<uint32_t>(json.size());
    frame.append(reinterpret_cast<const char*>(&op),  sizeof(op));
    frame.append(reinterpret_cast<const char*>(&len), sizeof(len));
    frame.append(json);
    return frame;
}

// ============================================================================
// Singleton
// ============================================================================
DiscordRichPresence* DiscordRichPresence::instance()
{
    static DiscordRichPresence* inst = nullptr;
    if (!inst)
        inst = new DiscordRichPresence(qApp);
    return inst;
}

// ============================================================================
// Construction / destruction
// ============================================================================
DiscordRichPresence::DiscordRichPresence(QObject* parent) : QObject(parent)
{
    // ── IPC socket ──────────────────────────────────────────────────────────
    m_ipcSocket = new QLocalSocket(this);
    connect(m_ipcSocket, &QLocalSocket::readyRead,
            this, &DiscordRichPresence::ipcOnReadyRead);
    connect(m_ipcSocket, &QLocalSocket::disconnected,
            this, &DiscordRichPresence::ipcOnDisconnected);
    connect(m_ipcSocket, &QLocalSocket::errorOccurred,
            this, &DiscordRichPresence::ipcOnError);

    m_ipcReconnectTimer = new QTimer(this);
    m_ipcReconnectTimer->setSingleShot(true);
    connect(m_ipcReconnectTimer, &QTimer::timeout,
            this, &DiscordRichPresence::ipcConnect);
}

DiscordRichPresence::~DiscordRichPresence()
{
    if (!m_shuttingDown)
        shutdown();
}

// ============================================================================
// Public API
// ============================================================================
void DiscordRichPresence::init()
{
    m_shuttingDown = false;
    ipcConnect();
    wsStart();
}

void DiscordRichPresence::shutdown()
{
    m_shuttingDown = true;
    m_ipcReconnectTimer->stop();

    if (m_ipcSocket->state() == QLocalSocket::ConnectedState) {
        ipcSendClose();
        m_ipcSocket->flush();
        m_ipcSocket->disconnectFromServer();
        m_ipcSocket->waitForDisconnected(500);
    }
    ipcSetConnected(false);

    wsStop();
}

void DiscordRichPresence::updatePresence(const QString& details,
                                          const QString& state,
                                          const QString& largeImageKey,
                                          const QString& largeImageText,
                                          const QString& smallImageKey,
                                          const QString& smallImageText,
                                          qint64         startTimeSecs)
{
    m_lastPresence = { details, state, largeImageKey, largeImageText,
                       smallImageKey, smallImageText, startTimeSecs, true };

    // Send over IPC if desktop client is connected
    if (m_ipcConnected) {
        QJsonObject args;
        args["activity"] = buildActivityJson();
        args["pid"]      = static_cast<int>(QCoreApplication::applicationPid());

        QJsonObject cmd;
        cmd["cmd"]   = QStringLiteral("SET_ACTIVITY");
        cmd["args"]  = args;
        cmd["nonce"] = QString::number(QDateTime::currentMSecsSinceEpoch());
        ipcSendFrame(cmd);
    }

    // Push to any connected arRPC bridge WebSocket clients
    wsBroadcastPresence();
}

void DiscordRichPresence::updatePlayingMinecraft(const QString& instanceName,
                                                  const QString& mcVersion,
                                                  qint64         startTime)
{
    updatePresence(
        QStringLiteral("Playing %1").arg(instanceName),
        QStringLiteral("Minecraft %1").arg(mcVersion),
        QStringLiteral("pollymc"),
        QStringLiteral("PollyMC-Continued"),
        QStringLiteral("minecraft"),
        QStringLiteral("Minecraft"),
        startTime);
}

void DiscordRichPresence::updateIdle()
{
    updatePresence(QStringLiteral("Idle"), QStringLiteral("In launcher"),
                   QStringLiteral("pollymc"), QStringLiteral("PollyMC-Continued"));
}

void DiscordRichPresence::updateBrowsing()
{
    updatePresence(QStringLiteral("Browsing"), QStringLiteral("Looking for mods"),
                   QStringLiteral("pollymc"), QStringLiteral("PollyMC-Continued"));
}

// ============================================================================
// Shared helper — builds the Discord "activity" JSON object from cached state
// ============================================================================
QJsonObject DiscordRichPresence::buildActivityJson() const
{
    const auto& p = m_lastPresence;

    QJsonObject assets;
    assets["large_image"] = p.largeImageKey;
    assets["large_text"]  = p.largeImageText;
    if (!p.smallImageKey.isEmpty()) {
        assets["small_image"] = p.smallImageKey;
        assets["small_text"]  = p.smallImageText;
    }

    QJsonObject activity;
    activity["details"] = p.details;
    activity["state"]   = p.state;
    activity["assets"]  = assets;
    activity["type"]    = 0;  // "Playing"

    if (p.startTimeSecs > 0) {
        QJsonObject timestamps;
        timestamps["start"] = p.startTimeSecs;
        activity["timestamps"] = timestamps;
    }

    return activity;
}

// ============================================================================
// IPC transport — connect
// ============================================================================
void DiscordRichPresence::ipcConnect()
{
    if (m_shuttingDown)
        return;
    if (m_ipcSocket->state() == QLocalSocket::ConnectedState ||
        m_ipcSocket->state() == QLocalSocket::ConnectingState)
        return;

    // Search order matches the official Discord SDK:
    // Windows: named pipe \\.\pipe\discord-ipc-N
    // Unix:    XDG_RUNTIME_DIR → TMPDIR → TMP → TEMP → /tmp
    auto candidateDirs = [&]() -> QStringList {
#ifdef Q_OS_WIN
        return {};
#else
        QStringList dirs;
        for (const char* var : { "XDG_RUNTIME_DIR", "TMPDIR", "TMP", "TEMP" })
        {
            QString v = qEnvironmentVariable(var);
            if (!v.isEmpty() && !dirs.contains(v))
                dirs << v;
        }
        dirs << QStringLiteral("/tmp");
        return dirs;
#endif
    }();

    for (int i = 0; i < 10; ++i) {
#ifdef Q_OS_WIN
        const QString pipe = QStringLiteral("\\\\.\\pipe\\discord-ipc-%1").arg(i);
        m_ipcSocket->connectToServer(pipe);
        if (m_ipcSocket->waitForConnected(200)) {
            qCDebug(drp) << "IPC connected:" << pipe;
            ipcSendHandshake();
            return;
        }
        if (m_ipcSocket->state() != QLocalSocket::UnconnectedState)
            m_ipcSocket->abort();
#else
        for (const QString& dir : candidateDirs) {
            const QString sock = QStringLiteral("%1/discord-ipc-%2").arg(dir).arg(i);
            m_ipcSocket->connectToServer(sock);
            if (m_ipcSocket->waitForConnected(200)) {
                qCDebug(drp) << "IPC connected:" << sock;
                ipcSendHandshake();
                return;
            }
            if (m_ipcSocket->state() != QLocalSocket::UnconnectedState)
                m_ipcSocket->abort();
        }
#endif
    }

    qCDebug(drp) << "Discord IPC not found; retry in" << m_ipcReconnectMs / 1000 << "s";
    ipcScheduleReconnect();
}

void DiscordRichPresence::ipcScheduleReconnect()
{
    if (m_shuttingDown) return;
    m_ipcReconnectTimer->start(m_ipcReconnectMs);
    m_ipcReconnectMs = qMin(m_ipcReconnectMs * 2, IPC_RECONNECT_MAX_MS);
}

// ============================================================================
// IPC transport — wire helpers
// ============================================================================
void DiscordRichPresence::ipcSendHandshake()
{
    QJsonObject hs;
    hs["v"]         = 1;
    hs["client_id"] = QLatin1String(APP_ID);
    m_ipcSocket->write(buildIpcFrame(HANDSHAKE,
                                     QJsonDocument(hs).toJson(QJsonDocument::Compact)));
    m_ipcSocket->flush();
}

void DiscordRichPresence::ipcSendClose()
{
    m_ipcSocket->write(buildIpcFrame(CLOSE, QByteArray()));
}

void DiscordRichPresence::ipcSendFrame(const QJsonObject& payload)
{
    if (m_ipcSocket->state() != QLocalSocket::ConnectedState)
        return;
    m_ipcSocket->write(buildIpcFrame(FRAME,
                                     QJsonDocument(payload).toJson(QJsonDocument::Compact)));
    m_ipcSocket->flush();
}

void DiscordRichPresence::ipcSetConnected(bool connected)
{
    if (m_ipcConnected == connected) return;
    m_ipcConnected = connected;
    emit ipcConnectedChanged(connected);
}

// ============================================================================
// IPC transport — socket slots
// ============================================================================
void DiscordRichPresence::ipcOnReadyRead()
{
    while (m_ipcSocket->bytesAvailable() >= 8) {
        const QByteArray header = m_ipcSocket->peek(8);
        if (header.size() < 8) return;

        const uint32_t opcode = *reinterpret_cast<const uint32_t*>(header.constData());
        const uint32_t len    = *reinterpret_cast<const uint32_t*>(header.constData() + 4);

        if (len > static_cast<uint32_t>(MAX_FRAME_BYTES)) {
            qCWarning(drp) << "Oversized IPC frame, aborting";
            m_ipcSocket->abort();
            return;
        }
        if (m_ipcSocket->bytesAvailable() < static_cast<qint64>(8 + len))
            return;

        m_ipcSocket->read(8);
        QByteArray payload;
        if (len > 0)
            payload = m_ipcSocket->read(static_cast<qint64>(len));

        switch (static_cast<DiscordOpcode>(opcode)) {
            case FRAME: {
                const QJsonObject obj = QJsonDocument::fromJson(payload).object();
                if (obj["cmd"].toString() == QLatin1String("DISPATCH") &&
                    obj["evt"].toString() == QLatin1String("READY"))
                {
                    m_ipcReconnectMs = IPC_RECONNECT_MIN_MS;
                    ipcSetConnected(true);
                    qCInfo(drp) << "Discord IPC ready";
                    // Re-send whatever presence was last set
                    if (m_lastPresence.valid)
                        updatePresence(m_lastPresence.details, m_lastPresence.state,
                                       m_lastPresence.largeImageKey, m_lastPresence.largeImageText,
                                       m_lastPresence.smallImageKey, m_lastPresence.smallImageText,
                                       m_lastPresence.startTimeSecs);
                    else
                        updateIdle();
                }
                break;
            }
            case CLOSE:
                m_ipcSocket->disconnectFromServer();
                return;
            case PING:
                m_ipcSocket->write(buildIpcFrame(PONG, payload));
                m_ipcSocket->flush();
                break;
            default:
                break;
        }
    }
}

void DiscordRichPresence::ipcOnDisconnected()
{
    qCInfo(drp) << "Discord IPC disconnected";
    ipcSetConnected(false);
    if (!m_shuttingDown)
        ipcScheduleReconnect();
}

void DiscordRichPresence::ipcOnError(QLocalSocket::LocalSocketError error)
{
    if (error == QLocalSocket::ServerNotFoundError ||
        error == QLocalSocket::ConnectionRefusedError)
        qCDebug(drp) << "IPC:" << m_ipcSocket->errorString();
    else
        qCWarning(drp) << "IPC error:" << m_ipcSocket->errorString();
}

// ============================================================================
// WebSocket transport — arRPC bridge server
//
// Protocol (arRPC bridge_mod.js / Vencord WebRichPresence plugin):
//
//   1. Discord Web connects to ws://127.0.0.1:<port> (tries 6463-6472).
//   2. We immediately send a "connected" handshake frame so the browser
//      knows we are the presence source.
//   3. Whenever presence changes we broadcast the SET_ACTIVITY payload.
//      The bridge script / plugin reads the "activity" field and calls
//      Discord's internal setActivity() on behalf of the user.
//   4. On shutdown we send a null activity to clear the status.
//
// The JSON shape expected by the arRPC bridge is exactly what arRPC's own
// server emits — a top-level "activity" key containing the Discord activity
// object, plus an "application_id" key.
// ============================================================================
void DiscordRichPresence::wsStart()
{
    // Try to bind to one of Discord's reserved RPC ports (6463-6472).
    // The launcher acts as a drop-in arRPC server on whichever port is free.
    m_wsServer = new QWebSocketServer(
        QStringLiteral("PollyMC-arRPC"),
        QWebSocketServer::NonSecureMode,
        this);

    for (quint16 port = 6463; port <= 6472; ++port) {
        if (m_wsServer->listen(QHostAddress::LocalHost, port)) {
            m_wsPort = port;
            qCInfo(drp) << "arRPC WebSocket server listening on port" << port;
            connect(m_wsServer, &QWebSocketServer::newConnection,
                    this, &DiscordRichPresence::wsOnNewConnection);
            return;
        }
    }

    // All ports taken — likely the real Discord desktop app is running and
    // already holding these ports.  IPC will handle it; WS is optional.
    qCDebug(drp) << "arRPC WS: all ports 6463-6472 busy (Discord desktop running?)";
    delete m_wsServer;
    m_wsServer = nullptr;
}

void DiscordRichPresence::wsStop()
{
    if (!m_wsServer) return;

    // Broadcast a null activity so Discord Web clears the status cleanly.
    const QString clearMsg = QJsonDocument(
        QJsonObject {
            { "activity",       QJsonValue::Null },
            { "application_id", QLatin1String(APP_ID) }
        }
    ).toJson(QJsonDocument::Compact);

    for (QWebSocket* client : m_wsClients) {
        client->sendTextMessage(clearMsg);
        client->close();
    }
    m_wsClients.clear();

    m_wsServer->close();
    m_wsServer->deleteLater();
    m_wsServer = nullptr;
    m_wsPort   = 0;
}

void DiscordRichPresence::wsOnNewConnection()
{
    while (m_wsServer->hasPendingConnections()) {
        QWebSocket* client = m_wsServer->nextPendingConnection();
        if (!client) continue;

        qCInfo(drp) << "arRPC WS: new client from" << client->peerAddress().toString();

        connect(client, &QWebSocket::textMessageReceived, this,
                [this, client](const QString& msg) { wsOnMessage(msg, client); });
        connect(client, &QWebSocket::disconnected, this,
                &DiscordRichPresence::wsOnClientDisconnected);

        m_wsClients.append(client);

        // Immediately push the current presence (or idle) to the new client.
        if (m_lastPresence.valid) {
            const QString msg = QJsonDocument(
                QJsonObject {
                    { "activity",       buildActivityJson() },
                    { "application_id", QLatin1String(APP_ID) }
                }
            ).toJson(QJsonDocument::Compact);
            client->sendTextMessage(msg);
        }
    }
}

void DiscordRichPresence::wsOnMessage(const QString& message, QWebSocket* /*client*/)
{
    // The arRPC bridge only listens; it doesn't send commands back to us.
    // Log at debug level in case a future protocol version does something here.
    qCDebug(drp) << "arRPC WS message received:" << message.left(120);
}

void DiscordRichPresence::wsOnClientDisconnected()
{
    QWebSocket* client = qobject_cast<QWebSocket*>(sender());
    if (!client) return;
    qCInfo(drp) << "arRPC WS: client disconnected";
    m_wsClients.removeAll(client);
    client->deleteLater();
}

void DiscordRichPresence::wsBroadcastPresence()
{
    if (!m_wsServer || m_wsClients.isEmpty())
        return;
    if (!m_lastPresence.valid)
        return;

    const QString msg = QJsonDocument(
        QJsonObject {
            { "activity",       buildActivityJson() },
            { "application_id", QLatin1String(APP_ID) }
        }
    ).toJson(QJsonDocument::Compact);

    for (QWebSocket* client : m_wsClients)
        client->sendTextMessage(msg);

    qCDebug(drp) << "arRPC WS: broadcasted presence to" << m_wsClients.size() << "client(s)";
}
