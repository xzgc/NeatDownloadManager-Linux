#include "singleinstance.h"

#include <QLocalSocket>

namespace neat {

static QString serverName()
{
    // scope the lock to the (test) HOME so parallel instances don't fight
    const QString home = qEnvironmentVariable("HOME");
    return QStringLiteral("neatdm-singleton-%1").arg(
        QString::number(qHash(home), 16));
}

SingleInstance::SingleInstance(QObject *parent)
    : QObject(parent)
{
    connect(&m_server, &QLocalServer::newConnection, this, &SingleInstance::onNewConnection);
}

bool SingleInstance::tryLock()
{
    QLocalSocket probe;
    probe.connectToServer(serverName());
    if (probe.waitForConnected(200)) {
        probe.write("raise\n");
        probe.waitForBytesWritten(200);
        return false;
    }
    // Stale socket from a crashed run.
    QLocalServer::removeServer(serverName());
    return m_server.listen(serverName());
}

void SingleInstance::onNewConnection()
{
    while (QLocalSocket *conn = m_server.nextPendingConnection()) {
        conn->waitForReadyRead(200);
        conn->readAll();
        conn->disconnectFromServer();
        conn->deleteLater();
        emit raiseRequested();
    }
}

} // namespace neat
