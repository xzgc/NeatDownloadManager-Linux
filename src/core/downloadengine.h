#pragma once
#include <QElapsedTimer>
#include <QObject>
#include <QTimer>
#include <QUrl>
#include <QStringList>
#include <vector>

#include "segment.h"
#include <functional>

namespace neat {

// Multi-connection segmented download engine (replica of NeatDownloadEngine +
// NeatSegmentManager): a probe connection resolves the final URL and size, the
// range is split across N workers writing into one preallocated part file at
// disjoint offsets; workers that finish borrow the tail of the slowest
// segment; state persists to <part>.seg; an engine-wide token bucket
// throttles all reads.
class DownloadEngine : public QObject {
    Q_OBJECT
public:
    struct ConnInfo {
        int id = 0;
        qint64 start = 0, end = -1, downloaded = 0;
        QString state;
    };

    enum class State { Idle, Probing, Downloading, Paused, Completed, Canceled, Failed };

    DownloadEngine(qint64 id, const QUrl &url, const QString &partPath, QObject *parent = nullptr);
    ~DownloadEngine() override;

    void start();               // fresh, or resume if <part>.seg matches
    void pause();
    void resume();
    void cancel();
    void setBandwidthLimitKb(qint64 kb);
    void setExtraRequestHeaders(const QStringList &lines) { m_extraHeaders = lines; }
    void setPostData(const QByteArray &body, const QString &contentType);
    void setCredentialLookup(const std::function<bool(const QString &host, const QString &proto,
                                                      QString &user, QString &pass)> &fn)
    {
        m_credentialLookup = fn;
    }
    void provideCredentials(const QString &user, const QString &pass);
    void setConnectionCount(int n);
    bool finishToFile(const QString &finalPath);   // part -> final (same dir, rename)

    State state() const { return m_state; }
    qint64 totalBytes() const { return m_total; }
    qint64 downloadedBytes() const { return m_table.doneBytes(); }
    qint64 speed() const { return m_speed; }
    int activeConnections() const { return m_activeWorkers; }
    int targetConnections() const { return m_targetConnections; }
    const QString &errorText() const { return m_error; }
    bool resumable() const { return m_resumable; }
    QUrl finalUrl() const { return m_finalUrl; }
    const std::vector<Segment> &segments() const { return m_table.segments(); }

    static constexpr int kMaxConnections = 32;

signals:
    void stateChanged(neat::DownloadEngine::State s);
    void progress(qint64 done, qint64 total, qint64 bytesPerSec);
    void connectionsInfo(const QVector<neat::DownloadEngine::ConnInfo> &infos);
    void authRequired(const QString &host, const QString &realm, const QString &scheme);
    void finished(bool ok, const QString &error);

private:
    class Worker;

    void probe();
    void probeFinished(qint64 total);
    void launchWorkers();
    void spawnWorker(int segIndex);
    void workerDone(Worker *w);
    void workerFailed(Worker *w, const QString &err);
    bool handleAuthChallenge(Worker *w, const QByteArray &head);
    void streamComplete(Worker *w);
    qint64 takeBudget(qint64 wanted);
    void onTick();
    void replenishBudget();
    void checkCompletion();
    void finish(bool ok, const QString &err);
    void setState(State s);
    void saveState();
    QString segPath() const { return m_partPath + QStringLiteral(".seg"); }

    qint64 m_id;
    QUrl m_url;
    QString m_method = QStringLiteral("GET");
    Worker *m_authWorker = nullptr;
    QUrl m_finalUrl;
    QString m_partPath;

    State m_state = State::Idle;
    QString m_error;
    qint64 m_total = -1;
    bool m_resumable = false;

    SegmentTable m_table;
    std::vector<Worker *> m_workers;
    int m_activeWorkers = 0;
    int m_targetConnections = 1;
    int m_workerSeq = 0;

    QStringList m_extraHeaders;       // Cookie/Referer/x-* passthrough
    QByteArray m_postBody;            // POST replay (extension downloads)
    QString m_postContentType;
    QString m_authHeader;             // Authorization once negotiated
    QString m_authUser, m_authPass;   // credentials supplied via UI
    std::function<bool(const QString &, const QString &, QString &, QString &)> m_credentialLookup;
    int m_authAttempts = 0;
    qint64 m_limitBytesPerSec = 0;   // 0 = unlimited
    qint64 m_budget = 0;

    QTimer m_tick;                   // 100ms: budget refill, speed, persistence
    QElapsedTimer m_clock;
    qint64 m_speedBase = 0;
    qint64 m_speed = 0;
    int m_tickCount = 0;
};

} // namespace neat
