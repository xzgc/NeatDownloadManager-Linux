#pragma once
#include <QObject>
#include <QTcpServer>
#include <QList>

class QTcpSocket;

namespace neat {

class WsConnection;

// Minimal hand-written WebSocket server (replica of the original
// NeatWebSocketServer): listens on 127.0.0.1:10007, speaks subprotocol
// "neatextension.v1", decodes/encodes text frames. The browser extension
// connects here and pushes download requests as line-based text frames.
class WsServer : public QObject {
    Q_OBJECT
public:
    static const char *kSubprotocol;
    static const quint16 kPort = 10007;   // overridable via NEATDM_WS_PORT

    explicit WsServer(QObject *parent = nullptr);

    bool start();   // false if the port is taken (another instance)

    static WsServer *instance() { return s_instance; }
    static void broadcastStatic(const QString &msg)
    {
        if (s_instance)
            s_instance->broadcastText(msg);
    }

    void broadcastText(const QString &message);

signals:
    void clientConnected(WsConnection *conn);
    void messageReceived(WsConnection *conn, const QString &message);
    void clientDisconnected(WsConnection *conn);

private:
    void onNewConnection();

    QTcpServer m_server;
    static WsServer *s_instance;
    QList<WsConnection *> m_clients;
};

// One upgraded WebSocket connection.
class WsConnection : public QObject {
    Q_OBJECT
public:
    explicit WsConnection(QTcpSocket *sock, QObject *parent = nullptr);
    ~WsConnection() override;

    void sendText(const QString &text);

signals:
    void textMessage(const QString &message);
    void closed(WsConnection *conn);
    void handshakeCompleted();

private:
    void onReadyRead();
    bool tryHandshake();
    void consumeFrames();
    void sendFrame(quint8 opcode, const QByteArray &payload);

    QTcpSocket *m_sock;
    QByteArray m_buf;
    bool m_upgraded = false;
    // reassembly of fragmented messages
    QByteArray m_pendingFrag;
    bool m_fragOpcodeText = true;
};

} // namespace neat
