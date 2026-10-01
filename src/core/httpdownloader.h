#pragma once
#include <QElapsedTimer>
#include <QFile>
#include <QObject>
#include <QSslSocket>
#include <QTcpSocket>
#include <QTimer>
#include <QUrl>

namespace neat {

// Single-connection HTTP/1.1 downloader (replica of the original NeatSocketHttp
// role, seed of the M2 segmented engine): GET with manual redirect following,
// Content-Length and chunked bodies, Range-based pause/resume, progress with
// speed sampling. HTTPS via QSslSocket, plain HTTP via QTcpSocket.
class HttpDownloader : public QObject {
    Q_OBJECT
public:
    enum class State { Idle, Connecting, Downloading, Paused, Completed, Canceled, Failed };

    explicit HttpDownloader(QObject *parent = nullptr);
    ~HttpDownloader() override;

    void start(const QUrl &url, const QString &partFilePath);
    void pause();
    void resume();
    void cancel();

    State state() const { return m_state; }
    qint64 totalBytes() const { return m_total; }
    qint64 downloadedBytes() const { return m_downloaded; }
    qint64 currentSpeed() const { return m_speed; }
    bool resumable() const { return m_resumable; }
    QString finalUrl() const { return m_finalUrl.toString(); }
    QString errorText() const { return m_error; }
    QString partPath() const { return m_partPath; }

    static QString suggestFileName(const QUrl &url);

signals:
    void stateChanged(neat::HttpDownloader::State newState);
    void progress(qint64 done, qint64 total, qint64 bytesPerSec);
    void finished(bool ok, const QString &error);

private:
    void sendRequest();
    void onConnected();
    void onReadyRead();
    void onSocketError();
    void finish(bool ok, const QString &error);
    void setState(State s);
    bool parseHeaders(const QByteArray &raw);
    void consumeBody();
    // chunked transfer state machine
    qint64 chunkStep(qint64 avail);

    QUrl m_url;              // current hop
    QUrl m_finalUrl;
    QString m_partPath;
    QFile m_file;
    QTcpSocket *m_sock = nullptr;   // holds QTcpSocket or QSslSocket
    bool m_tls = false;

    QByteArray m_buf;        // receive buffer (headers + body)
    enum class Phase { Headers, Body, ChunkSize, ChunkData, ChunkTrail, Done } m_phase = Phase::Headers;
    qint64 m_contentLength = -1;   // -1 = unknown until EOF
    qint64 m_downloaded = 0;      // cumulative bytes on disk
    qint64 m_bodyOffset = 0;      // m_downloaded at the start of the current response
    qint64 m_total = -1;
    qint64 m_chunkRemain = 0;
    int m_redirects = 0;
    bool m_resumable = false;
    bool m_startedOnce = false;
    State m_state = State::Idle;
    QString m_error;

    QTimer m_speedTimer;
    QElapsedTimer m_clock;
    qint64 m_speedBase = 0;
    qint64 m_speed = 0;
};

} // namespace neat
