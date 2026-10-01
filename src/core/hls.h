#pragma once
#include <QByteArray>
#include <QFile>
#include <QElapsedTimer>
#include <QObject>
#include <QStringList>
#include <QTimer>
#include <QUrl>
#include <QVector>

namespace neat {

// HLS download backend (replica of the original NeatSocketHlsMaster flow):
// fetch + parse the m3u8 (master playlists resolve to the highest-bandwidth
// variant), then download TS segments with a small sliding window of parallel
// fetchers, writing them to the part file strictly in order.
class HlsDownloader : public QObject {
    Q_OBJECT
public:
    enum class State { Idle, Probing, Downloading, Paused, Completed, Canceled, Failed };

    struct FetchInfo {
        int index = 0;
        qint64 bytes = 0;
        QString state;
    };

    HlsDownloader(qint64 id, const QUrl &playlistUrl, const QString &partPath,
                  QObject *parent = nullptr);

    void setExtraRequestHeaders(const QStringList &lines) { m_extraHeaders = lines; }
    void start();
    void pause();
    void resume();
    void cancel();
    void setBandwidthLimitKb(qint64 kb) { m_limitBytesPerSec = qMax<qint64>(0, kb) * 1024; }
    bool finishToFile(const QString &finalPath);

    State state() const { return m_state; }
    qint64 totalBytes() const { return m_bytes; }          // bytes fetched so far
    int segmentsDone() const { return m_nextIndex; }
    int segmentsCount() const { return int(m_segments.size()); }
    const QString &errorText() const { return m_error; }
    QUrl finalUrl() const { return m_playlistUrl; }

signals:
    void stateChanged(neat::HlsDownloader::State s);
    void progress(qint64 done, qint64 total, qint64 bytesPerSec);
    void segmentsInfo(int done, int count);
    void fetchersInfo(const QVector<neat::HlsDownloader::FetchInfo> &infos);
    void finished(bool ok, const QString &error);

private:
    class Fetcher;
    friend class Fetcher;

    struct SegmentItem {
        QUrl url;
        double duration = 0.0;
    };

    void fetchPlaylist(const QUrl &url, bool master);
    void parsePlaylist(const QByteArray &data, const QUrl &baseUrl, bool master);
    void pumpFetchers();
    void flushInOrder();
    void fetcherDone(class Fetcher *f);
    void fetcherFailed(class Fetcher *f, const QString &err);
    void finish(bool ok, const QString &err);
    void setState(State s);

    qint64 m_id;
    QUrl m_playlistUrl;
    QString m_partPath;
    QStringList m_extraHeaders;
    QString m_error;

    State m_state = State::Idle;
    QVector<SegmentItem> m_segments;
    QVector<QByteArray> m_ready;       // fetched segment payloads by index
    QVector<bool> m_inFlight;
    int m_nextIndex = 0;
    qint64 m_bytes = 0;
    qint64 m_limitBytesPerSec = 0;

    QFile m_part;
    QTimer m_tick;
    QElapsedTimer m_clock;
    qint64 m_speedBase = 0;
    qint64 m_speed = 0;
    int m_activeFetchers = 0;
    int m_redirects = 0;
    bool m_masterFetch = true;
    QVector<Fetcher *> m_fetchers;
    static constexpr int kWindow = 4;
};

} // namespace neat
