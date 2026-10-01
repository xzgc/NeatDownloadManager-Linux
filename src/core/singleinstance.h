#pragma once
#include <QLocalServer>
#include <QObject>

namespace neat {

// Single-instance guard: first process listens on a local socket; a second
// process connects, sends "raise", and exits. The first one shows its window.
class SingleInstance : public QObject {
    Q_OBJECT
public:
    explicit SingleInstance(QObject *parent = nullptr);

    // Returns false if another instance is already running (this process must exit).
    bool tryLock();

signals:
    void raiseRequested();

private:
    void onNewConnection();

    QLocalServer m_server;
};

} // namespace neat
