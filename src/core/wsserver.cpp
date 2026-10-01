#include "wsserver.h"

#include <QCryptographicHash>
#include <QHostAddress>
#include <QTcpSocket>

namespace neat {

const char *WsServer::kSubprotocol = "neatextension.v1";
WsServer *WsServer::s_instance = nullptr;

// ---------------------------------------------------------------- server ----

WsServer::WsServer(QObject *parent)
    : QObject(parent)
{
    connect(&m_server, &QTcpServer::newConnection, this, &WsServer::onNewConnection);
}

bool WsServer::start()
{
    if (m_server.isListening())
        return true;
    quint16 port = quint16(qEnvironmentVariableIntValue("NEATDM_WS_PORT"));
    if (port == 0)
        port = kPort;
    if (!m_server.listen(QHostAddress::LocalHost, port))
        return false;
    s_instance = this;
    return true;
}

void WsServer::onNewConnection()
{
    while (QTcpSocket *sock = m_server.nextPendingConnection()) {
        auto *conn = new WsConnection(sock, this);
        m_clients.append(conn);
        // the extension may only be spoken to after its handshake completes
        connect(conn, &WsConnection::handshakeCompleted, this,
                [this, conn] { emit clientConnected(conn); });
        connect(conn, &WsConnection::textMessage, this,
                [this, conn](const QString &m) { emit messageReceived(conn, m); });
        connect(conn, &WsConnection::closed, this, [this, conn] {
            m_clients.removeOne(conn);
            conn->deleteLater();
            emit clientDisconnected(conn);
        });
    }
}

void WsServer::broadcastText(const QString &message)
{
    for (WsConnection *c : m_clients)
        c->sendText(message);
}

// ------------------------------------------------------------- connection ----

static const char *kWsGuid = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";

WsConnection::WsConnection(QTcpSocket *sock, QObject *parent)
    : QObject(parent)
    , m_sock(sock)
{
    m_sock->setParent(this);
    connect(m_sock, &QTcpSocket::readyRead, this, &WsConnection::onReadyRead);
    connect(m_sock, &QTcpSocket::disconnected, this, [this] { emit closed(this); });
}

WsConnection::~WsConnection()
{
    if (m_sock && m_sock->state() != QAbstractSocket::UnconnectedState)
        m_sock->abort();
}

void WsConnection::onReadyRead()
{
    m_buf += m_sock->readAll();
    if (!m_upgraded) {
        if (!tryHandshake())
            return;
    }
    consumeFrames();
}

bool WsConnection::tryHandshake()
{
    const int idx = m_buf.indexOf("\r\n\r\n");
    if (idx < 0)
        return true;   // need more bytes (keep connection)

    const QByteArray head = m_buf.left(idx);
    m_buf.remove(0, idx + 4);

    QByteArray key;
    QString path;
    const QList<QByteArray> lines = head.split('\n');
    for (int i = 0; i < lines.size(); ++i) {
        QByteArray l = lines[i].trimmed();
        if (i == 0) {
            const QList<QByteArray> parts = l.split(' ');
            if (parts.size() > 1)
                path = QString::fromLatin1(parts[1]);
            continue;
        }
        const int colon = l.indexOf(':');
        if (colon < 0)
            continue;
        const QByteArray name = l.left(colon).trimmed().toLower();
        if (name == "sec-websocket-key")
            key = l.mid(colon + 1).trimmed();
    }
    if (key.isEmpty()) {
        m_sock->abort();
        emit closed(this);
        return false;
    }

    const QByteArray accept = QCryptographicHash::hash(
        key + QByteArray(kWsGuid), QCryptographicHash::Sha1).toBase64();
    QByteArray resp;
    resp += "HTTP/1.1 101 Switching Protocols\r\n";
    resp += "Upgrade: websocket\r\n";
    resp += "Connection: Upgrade\r\n";
    resp += "Sec-WebSocket-Accept: " + accept + "\r\n";
    resp += "Sec-WebSocket-Protocol: " + QByteArray(WsServer::kSubprotocol) + "\r\n";
    resp += "\r\n";
    m_sock->write(resp);
    m_upgraded = true;
    emit handshakeCompleted();
    return true;
}

void WsConnection::consumeFrames()
{
    while (true) {
        if (m_buf.size() < 2)
            return;
        const quint8 b0 = quint8(m_buf[0]);
        const quint8 b1 = quint8(m_buf[1]);
        const bool fin = b0 & 0x80;
        const quint8 opcode = b0 & 0x0F;
        const bool masked = b1 & 0x80;
        quint64 len = b1 & 0x7F;
        int off = 2;
        if (len == 126) {
            if (m_buf.size() < off + 2)
                return;
            len = (quint8(m_buf[off]) << 8) | quint8(m_buf[off + 1]);
            off += 2;
        } else if (len == 127) {
            if (m_buf.size() < off + 8)
                return;
            len = 0;
            for (int i = 0; i < 8; ++i)
                len = (len << 8) | quint8(m_buf[off + i]);
            off += 8;
        }
        quint8 mask[4] = {0, 0, 0, 0};
        if (masked) {
            if (m_buf.size() < off + 4)
                return;
            for (int i = 0; i < 4; ++i)
                mask[i] = quint8(m_buf[off + i]);
            off += 4;
        }
        if (quint64(m_buf.size() - off) < len)
            return;   // wait for the full payload

        QByteArray payload = m_buf.mid(off, int(len));
        m_buf.remove(0, off + int(len));
        if (masked)
            for (int i = 0; i < payload.size(); ++i)
                payload[i] = char(quint8(payload[i]) ^ mask[i % 4]);

        switch (opcode) {
        case 0x1:   // text
        case 0x2:   // binary
            if (fin) {
                emit textMessage(QString::fromUtf8(payload));
            } else {
                m_fragOpcodeText = (opcode == 0x1);
                m_pendingFrag = payload;
            }
            break;
        case 0x0:   // continuation
            m_pendingFrag += payload;
            if (fin) {
                emit textMessage(QString::fromUtf8(m_pendingFrag));
                m_pendingFrag.clear();
            }
            break;
        case 0x8:   // close
            sendFrame(0x8, QByteArray());
            m_sock->flush();
            m_sock->disconnectFromHost();
            return;
        case 0x9:   // ping -> pong
            sendFrame(0xA, payload);
            break;
        default:
            break;
        }
    }
}

void WsConnection::sendFrame(quint8 opcode, const QByteArray &payload)
{
    QByteArray f;
    f.append(char(0x80 | opcode));
    if (payload.size() < 126) {
        f.append(char(payload.size()));
    } else if (payload.size() <= 0xFFFF) {
        f.append(char(126));
        f.append(char((payload.size() >> 8) & 0xFF));
        f.append(char(payload.size() & 0xFF));
    } else {
        f.append(char(127));
        const quint64 n = quint64(payload.size());
        for (int i = 7; i >= 0; --i)
            f.append(char((n >> (i * 8)) & 0xFF));
    }
    f += payload;   // server frames are unmasked
    m_sock->write(f);
}

void WsConnection::sendText(const QString &text)
{
    sendFrame(0x1, text.toUtf8());
}

} // namespace neat
