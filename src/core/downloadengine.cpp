#include "downloadengine.h"
#include "auth.h"
#include "netproxy.h"
#include "settings.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSslSocket>
#include <QTcpSocket>

#include <functional>

namespace neat {

static const int kConnectTimeoutMs = 30000;
static const int kMaxRedirects = 20;

static QList<QByteArray> splitCrlf(const QByteArray &raw)
{
    QList<QByteArray> out;
    int start = 0;
    while (true) {
        const int idx = raw.indexOf("\r\n", start);
        if (idx < 0) {
            out.append(raw.mid(start));
            break;
        }
        out.append(raw.mid(start, idx - start));
        start = idx + 2;
    }
    return out;
}

// ---------------------------------------------------------------- Worker ----
// One ranged connection. Writes into the shared part file through its own
// unbuffered handle at disjoint offsets. As a nested class it can touch the
// engine's private members directly.

class DownloadEngine::Worker : public QObject {
public:
    enum class Phase { Connecting, Headers, Body, Done, Failed };

    Worker(DownloadEngine *engine, int segIndex, int workerId)
        : QObject(engine)
        , eng(engine)
        , segIndex(segIndex)
        , id(workerId)
    {
    }

    ~Worker() override
    {
        if (sock) {
            sock->abort();
            sock->deleteLater();
        }
        if (file.isOpen())
            file.close();
    }

    void retire()
    {
        if (retired)
            return;
        if (phase == Phase::Body && counted) {
            // hand back the bookkeeping so the segment can be retried later
            eng->m_table.segments()[size_t(segIndex)].active = false;
            --eng->m_activeWorkers;
            counted = false;
        }
        retired = true;
        phase = Phase::Done;
        if (sock)
            sock->abort();
        if (file.isOpen())
            file.close();
    }

    void connectTo(const QUrl &u);
    void pump();

    qint64 segRemain() const { return rangeEnd + 1 - pos; }

    DownloadEngine *eng;
    int segIndex;          // -1 for the probe worker
    int id;
    qint64 rangeStart = 0;
    qint64 rangeEnd = -1;
    qint64 pos = 0;
    qint64 contentRemain = -1;
    Phase phase = Phase::Connecting;
    bool probeMode = false;
    bool retired = false;
    bool counted = false;  // accounted in eng->m_activeWorkers

    QTcpSocket *sock = nullptr;
    QFile file;
    QByteArray buf;
    QUrl url;
    int redirects = 0;
    int retries = 0;
    bool chunked = false;
    bool sawFinalChunk = false;
    qint64 chunkRemain = 0;

    // feed raw chunked bytes through the chunk state machine; decoded body
    // bytes are written through writeBody()
    void dechunk(QByteArray &raw, qint64 budget)
    {
        Q_UNUSED(budget);
        buf += raw;
        chunkLoop();
    }

    // advance the chunk state machine over whatever sits in buf
    void chunkLoop()
    {
        while (!buf.isEmpty()) {
            if (chunkRemain == 0) {
                while (buf.startsWith("\r\n"))
                    buf.remove(0, 2);
                const int idx = buf.indexOf("\r\n");
                if (idx < 0)
                    return;   // need more bytes for the size line
                bool ok = false;
                chunkRemain = buf.left(idx).toLongLong(&ok, 16);
                buf.remove(0, idx + 2);
                if (!ok)
                    return;
                if (chunkRemain == 0) {
                    // final chunk: skip trailers, response complete
                    sawFinalChunk = true;
                    buf.clear();
                    if (eng->m_total < 0)
                        eng->streamComplete(this);
                    else if (segRemain() <= 0)
                        eng->workerDone(this);
                    else
                        peerClosed();
                    return;
                }
                continue;
            }
            const qint64 take = qMin<qint64>(qMin<qint64>(chunkRemain, buf.size()), segRemain());
            if (take <= 0)
                return;
            const qint64 allowed = eng->takeBudget(take);
            if (allowed <= 0)
                return;
            const QByteArray body = buf.left(int(allowed));
            buf.remove(0, int(allowed));
            chunkRemain -= allowed;
            writeBody(body);
            if (retired)
                return;
        }
    }

    // write decoded body bytes at pos, updating segment bookkeeping
    void writeBody(const QByteArray &body)
    {
        if (segRemain() <= 0) {
            eng->workerDone(this);
            return;
        }
        const QByteArray part = body.left(int(qMin<qint64>(body.size(), segRemain())));
        file.seek(pos);
        file.write(part);
        pos += part.size();
        if (contentRemain > 0)
            contentRemain -= part.size();
        if (segIndex >= 0)
            eng->m_table.segments()[size_t(segIndex)].downloaded = pos
                - eng->m_table.segments()[size_t(segIndex)].start;
        if (segRemain() <= 0) {
            eng->workerDone(this);
            return;
        }
    }

    void peerClosed()
    {
        // Qt may have buffered response bytes before the FIN: drain what the
        // budget allows, stopping when no progress is possible.
        qint64 prev = -1;
        while (sock->bytesAvailable() > 0) {
            pump();
            if (retired)
                return;
            const qint64 now = sock->bytesAvailable();
            if (now == prev)
                break;   // budget exhausted; onTick will keep pumping
            prev = now;
        }
        if (segRemain() <= 0) {
            eng->workerDone(this);
            return;
        }
        if (sock->bytesAvailable() > 0 || !buf.isEmpty())
            return;   // still has unread data; completion happens in pump()
        // reconnect and continue from the current position; bounded retries.
        // A non-Range server restarts this span from zero.
        if (++retries <= 5) {
            if (!eng->resumable())
                pos = 0;
            buf.clear();
            connectTo(url);
        } else {
            eng->workerFailed(this, QStringLiteral("Server Closed Connection Suddenly."));
        }
    }
};

void DownloadEngine::Worker::connectTo(const QUrl &u)
{
    url = u;
    if (sock) {
        sock->abort();
        sock->deleteLater();
        sock = nullptr;
    }
    buf.clear();
    phase = Phase::Connecting;
    const bool tls = url.scheme().compare(QStringLiteral("https"), Qt::CaseInsensitive) == 0;
    const int port = url.port(tls ? 443 : 80);

    sock = tls ? static_cast<QTcpSocket *>(new QSslSocket(this)) : new QTcpSocket(this);
    const QNetworkProxy proxy = NetProxy::proxyFor(url);
    sock->setProxy(proxy);
    connect(sock, &QTcpSocket::proxyAuthenticationRequired, this,
            [this](const QNetworkProxy &p, QAuthenticator *a) {
                NetProxy::fillAuthenticator(p, a);
            });

    connect(sock, &QTcpSocket::connected, this, [this] {
        QString path = url.path();
        if (path.isEmpty())
            path = QStringLiteral("/");
        if (url.hasQuery())
            path += QLatin1Char('?') + url.query();
        const bool post = eng->m_method == QLatin1String("POST");
        QByteArray req;
        req += (post ? "POST " : "GET ") + path.toUtf8() + " HTTP/1.1\r\n";
        req += "Host: " + url.host(QUrl::FullyEncoded).toUtf8() + "\r\n";
        req += "User-Agent: " + Settings::instance().userAgent().toUtf8() + "\r\n";
        req += "Accept: */*\r\n";
        req += "Connection: close\r\n";
        if (post) {
            if (!eng->m_postContentType.isEmpty())
                req += "Content-Type: " + eng->m_postContentType.toUtf8() + "\r\n";
            req += "Content-Length: " + QByteArray::number(eng->m_postBody.size()) + "\r\n";
        }
        if (!eng->m_authHeader.isEmpty())
            req += "Authorization: " + eng->m_authHeader.toUtf8() + "\r\n";
        if (rangeEnd >= std::numeric_limits<qint64>::max() / 2)
            req += "Range: bytes=" + QByteArray::number(pos) + "-\r\n";
        else
            req += "Range: bytes=" + QByteArray::number(pos) + "-" + QByteArray::number(rangeEnd)
                   + "\r\n";
        for (const QString &h : eng->m_extraHeaders)
            req += h.toUtf8() + "\r\n";
        req += "\r\n";
        if (post)
            req += eng->m_postBody;
        sock->write(req);
        phase = Phase::Headers;
    });
    connect(sock, &QTcpSocket::readyRead, this, [this] { pump(); });
    connect(sock, &QTcpSocket::errorOccurred, this, [this](QAbstractSocket::SocketError e) {
        if (e == QAbstractSocket::RemoteHostClosedError || retired)
            return;
        if (phase == Phase::Headers || phase == Phase::Body)
            eng->workerFailed(this, QStringLiteral("No Internet Connection or DNS Failed."));
    });
    connect(sock, &QTcpSocket::disconnected, this, [this] {
        if (retired || phase != Phase::Body)
            return;
        peerClosed();
    });

    if (tls)
        static_cast<QSslSocket *>(sock)->connectToHostEncrypted(url.host(), port);
    else
        sock->connectToHost(url.host(), port);

    QTimer::singleShot(kConnectTimeoutMs, this, [this] {
        if (phase == Phase::Connecting && !retired)
            eng->workerFailed(this, QStringLiteral("Connection TimeOut."));
    });
}

void DownloadEngine::Worker::pump()
{
    if (retired)
        return;

    if (phase == Phase::Headers) {
        buf += sock->readAll();
        const int idx = buf.indexOf("\r\n\r\n");
        if (idx < 0)
            return;
        const QByteArray head = buf.left(idx);
        buf.remove(0, idx + 4);

        const QList<QByteArray> lines = splitCrlf(head);
        const QByteArray sl = lines.value(0);
        const int sp1 = sl.indexOf(' ');
        const int sp2 = sl.indexOf(' ', sp1 + 1);
        const int code = sl.mid(sp1 + 1, sp2 - sp1 - 1).toInt();

        QByteArray location;
        qint64 contentLength = -1;
        chunked = false;
        for (int i = 1; i < lines.size(); ++i) {
            const int colon = lines[i].indexOf(':');
            if (colon < 0)
                continue;
            const QByteArray name = lines[i].left(colon).trimmed().toLower();
            const QByteArray value = lines[i].mid(colon + 1).trimmed();
            if (name == "location")
                location = value;
            else if (name == "content-length")
                contentLength = value.toLongLong();
            else if (name == "accept-ranges" && value.toLower().contains("bytes"))
                eng->m_resumable = true;
            else if (name == "transfer-encoding" && value.toLower().contains("chunked"))
                chunked = true;
        }

        if ((code >= 301 && code <= 303) || code == 307 || code == 308) {
            if (++redirects > kMaxRedirects) {
                eng->workerFailed(
                    this, QStringLiteral("Server Redirected to an unsupported  protocol/Invalid URL."));
                return;
            }
            const QUrl next = url.resolved(QUrl::fromEncoded(location, QUrl::StrictMode));
            if (next.scheme() != QLatin1String("http")
                && next.scheme() != QLatin1String("https")) {
                eng->workerFailed(
                    this, QStringLiteral("Server Redirected to an unsupported  protocol/Invalid URL."));
                return;
            }
            connectTo(next);
            return;
        }

        if (probeMode) {
            qint64 total = -1;
            if (code == 206) {
                for (const QByteArray &l : lines) {
                    if (QString::fromLatin1(l).startsWith(QStringLiteral("content-range"),
                                                        Qt::CaseInsensitive)) {
                        const int slash = l.lastIndexOf('/');
                        if (slash >= 0)
                            total = l.mid(slash + 1).trimmed().toLongLong();
                    }
                }
                eng->m_resumable = true;
            } else if (code >= 200 && code < 300) {
                total = contentLength;   // -1 with chunked/EOF bodies: stream mode
                eng->m_resumable = false;
            } else if (code == 401) {
                if (!eng->handleAuthChallenge(this, head))
                    eng->workerFailed(this, QStringLiteral("401 Unauthorized."));
                return;
            } else {
                eng->workerFailed(this, QStringLiteral("HTTP %1.").arg(code));
                return;
            }
            sock->abort();
            retired = true;
            phase = Phase::Done;
            eng->probeFinished(total);
            return;
        }

        if (code == 206) {
            qint64 rangeTotal = -1;
            for (const QByteArray &l : lines) {
                if (QString::fromLatin1(l).startsWith(QStringLiteral("content-range"),
                                                    Qt::CaseInsensitive)) {
                    const int slash = l.lastIndexOf('/');
                    if (slash >= 0)
                        rangeTotal = l.mid(slash + 1).trimmed().toLongLong();
                }
            }
            // a single flaky mirror can answer a 206 with a different
            // total; never let it overwrite the size learned at probe time
            if (rangeTotal > 0 && eng->m_total < 0)
                eng->m_total = rangeTotal;
            contentRemain = contentLength;
        } else if (code == 200) {
            if (pos != 0) {
                eng->workerFailed(
                    this,
                    QStringLiteral("Server Didn't allow File to Be Resumed. Please Redownload File."));
                return;
            }
            contentRemain = contentLength;
            eng->m_resumable = false;
        } else if (code == 401) {
            if (eng->handleAuthChallenge(this, head))
                return;
            eng->workerFailed(this, QStringLiteral("401 Unauthorized."));
            return;
        } else if (code == 416) {
            eng->workerFailed(
                this,
                QStringLiteral(
                    "File Has been Changed on Server.You must Redownload it from beggining.(Reply-Code=416)"));
            return;
        } else {
            eng->workerFailed(this, QStringLiteral("HTTP %1.").arg(code));
            return;
        }

        eng->m_finalUrl = url;
        phase = Phase::Body;
        pump();
        return;
    }

    if (phase != Phase::Body)
        return;

    while (true) {
        // drain bytes that arrived together with the response headers first
        QByteArray chunk;
        if (!buf.isEmpty()) {
            if (chunked) {
                // partial chunk frames must continue through the state machine
                chunkLoop();
                if (retired)
                    return;
                continue;
            }
            const qint64 allowed = eng->takeBudget(qMin<qint64>(buf.size(), segRemain()));
            if (allowed <= 0)
                return;
            chunk = buf.left(int(allowed));
            buf.remove(0, int(allowed));
        } else if (chunked) {
            const qint64 avail = sock->bytesAvailable();
            if (avail <= 0) {
                if (sock->state() == QAbstractSocket::UnconnectedState)
                    peerClosed();
                return;
            }
            const qint64 allowed = eng->takeBudget(avail);
            if (allowed <= 0)
                return;
            chunk = sock->read(int(qMin<qint64>(allowed, 1 << 20)));
            if (chunk.isEmpty())
                return;
            dechunk(chunk, allowed);
            continue;
        } else {
            const qint64 avail = sock->bytesAvailable();
            if (avail <= 0) {
                if (sock->state() == QAbstractSocket::UnconnectedState)
                    peerClosed();
                return;
            }
            const qint64 allowed = eng->takeBudget(qMin<qint64>(avail, segRemain()));
            if (allowed <= 0)
                return;   // throttled; the budget timer will pump() again
            chunk = sock->read(int(qMin<qint64>(allowed, 1 << 20)));
            if (chunk.isEmpty())
                return;
        }
        if (segRemain() <= 0) {
            eng->workerDone(this);
            return;
        }
        file.seek(pos);
        file.write(chunk);
        pos += chunk.size();
        if (contentRemain > 0)
            contentRemain -= chunk.size();
        if (segIndex >= 0)
            eng->m_table.segments()[size_t(segIndex)].downloaded = pos
                - eng->m_table.segments()[size_t(segIndex)].start;
        if (segRemain() <= 0) {
            eng->workerDone(this);
            return;
        }
    }
}

// --------------------------------------------------------------- Engine ----

DownloadEngine::DownloadEngine(qint64 id, const QUrl &url, const QString &partPath, QObject *parent)
    : QObject(parent)
    , m_id(id)
    , m_url(url)
    , m_partPath(partPath)
{
    m_tick.setInterval(100);
    connect(&m_tick, &QTimer::timeout, this, &DownloadEngine::onTick);
}

DownloadEngine::~DownloadEngine()
{
    for (Worker *w : m_workers)
        w->retire();
    qDeleteAll(m_workers);
}

void DownloadEngine::start()
{
    m_targetConnections = qBound(1, Settings::instance().maxConnections(), kMaxConnections);
    QDir().mkpath(QFileInfo(m_partPath).absolutePath());
    setState(State::Probing);
    probe();
}

void DownloadEngine::probe()
{
    Worker *w = new Worker(this, -1, 0);
    w->probeMode = true;
    w->rangeStart = 0;
    w->rangeEnd = 0;
    w->pos = 0;
    m_workers.push_back(w);
    w->connectTo(m_url);
}

void DownloadEngine::probeFinished(qint64 total)
{
    if (total == 0) {
        finish(false, tr("File is Empty. (FileSize=0)"));
        return;
    }
    if (total < 0) {
        // unknown size (chunked / EOF-delimited): single-stream mode
        m_total = -1;
        m_targetConnections = 1;
        m_resumable = false;
        QFile part(m_partPath);
        if (!part.open(QIODevice::ReadWrite)) {
            finish(false, tr("Error on Creating Temp Directory  ( disk full or read-only )"));
            return;
        }
        part.resize(0);
        part.close();
        m_table.segments().clear();
        m_table.segments().push_back(Segment{0, std::numeric_limits<qint64>::max() / 2, 0, false});
        setState(State::Downloading);
        m_clock.restart();
        m_speedBase = 0;
        m_tick.start();
        launchWorkers();
        return;
    }
    m_total = total;

    // resume if a matching part file + segment table exists
    if (QFile::exists(m_partPath)
        && QFileInfo(m_partPath).size() == total
        && m_table.load(segPath(), total)) {
        setState(State::Downloading);
    } else {
        QFile part(m_partPath);
        if (!part.open(QIODevice::ReadWrite) || !part.resize(total)) {
            finish(false, tr("Error on Creating Temp Directory  ( disk full or read-only )"));
            return;
        }
        part.close();
        m_table.initialize(total, m_resumable ? m_targetConnections : 1);
        setState(State::Downloading);
    }
    m_clock.restart();
    m_speedBase = 0;
    m_tick.start();
    launchWorkers();
}

void DownloadEngine::launchWorkers()
{
    if (m_total < 0)
        m_targetConnections = 1;
    m_table.reflow(m_targetConnections);
    auto &segs = m_table.segments();
    int spawned = 0;
    for (size_t i = 0; i < segs.size() && spawned < m_targetConnections; ++i) {
        Segment &s = segs[i];
        if (s.active || s.downloaded >= s.end - s.start + 1)
            continue;
        spawnWorker(int(i));
        ++spawned;
    }
    if (spawned == 0)
        checkCompletion();
}

void DownloadEngine::spawnWorker(int segIndex)
{
    Segment &s = m_table.segments()[size_t(segIndex)];
    Worker *w = new Worker(this, segIndex, ++m_workerSeq);
    s.active = true;
    w->rangeStart = s.start + s.downloaded;
    w->rangeEnd = s.end;
    w->pos = w->rangeStart;
    w->file.setFileName(m_partPath);
    if (!w->file.open(QIODevice::ReadWrite | QIODevice::Unbuffered)) {
        s.active = false;
        delete w;
        finish(false, tr("Failed To Open Segment-File."));
        return;
    }
    m_workers.push_back(w);
    ++m_activeWorkers;
    w->counted = true;
    w->connectTo(m_finalUrl.isEmpty() ? m_url : m_finalUrl);
}

void DownloadEngine::workerDone(Worker *w)
{
    if (w->retired)
        return;
    if (w->counted) {
        --m_activeWorkers;
        w->counted = false;
    }
    w->retired = true;
    w->phase = Worker::Phase::Done;
    if (w->sock)
        w->sock->abort();
    if (w->file.isOpen())
        w->file.close();
    if (w->segIndex >= 0) {
        Segment &s = m_table.segments()[size_t(w->segIndex)];
        s.downloaded = s.end - s.start + 1;
        s.active = false;
    }
    saveState();
    checkCompletion();

    // borrow the tail of the slowest segment for the freed connection
    if (m_state == State::Downloading && m_activeWorkers < m_targetConnections) {
        int idx = m_table.slowestIndex(true);
        if (idx < 0)
            idx = m_table.largestRemainingIndex();
        if (idx >= 0) {
            Segment &s = m_table.segments()[size_t(idx)];
            if (!s.active && s.downloaded < s.end - s.start + 1) {
                spawnWorker(idx);
                return;
            }
            const int tail = m_table.borrowTail(idx);
            if (tail >= 0)
                spawnWorker(tail);
        }
    }
}

void DownloadEngine::workerFailed(Worker *w, const QString &err)
{
    if (w->retired)
        return;
    if (w->counted) {
        --m_activeWorkers;
        w->counted = false;
    }
    w->retire();
    if (w->segIndex >= 0)
        m_table.segments()[size_t(w->segIndex)].active = false;
    finish(false, err);
}

void DownloadEngine::pause()
{
    if (m_state != State::Downloading && m_state != State::Probing)
        return;
    setState(State::Paused);
    for (Worker *w : m_workers)
        w->retire();
    saveState();
    m_tick.stop();
}

void DownloadEngine::resume()
{
    if (m_state != State::Paused)
        return;
    if (m_total > 0 && QFile::exists(m_partPath)) {
        setState(State::Downloading);
        m_clock.restart();
        m_speedBase = downloadedBytes();
        m_tick.start();
        launchWorkers();
    } else {
        setState(State::Probing);
        probe();
    }
}

void DownloadEngine::cancel()
{
    if (m_state == State::Completed || m_state == State::Canceled)
        return;
    setState(State::Canceled);
    m_tick.stop();
    for (Worker *w : m_workers)
        w->retire();
    QFile::remove(m_partPath);
    QFile::remove(segPath());
    QFile::remove(segPath() + QStringLiteral(".bak"));
    emit finished(false, tr("Download Canceled By User."));
}

void DownloadEngine::setPostData(const QByteArray &body, const QString &contentType)
{
    m_method = body.isEmpty() ? QStringLiteral("GET") : QStringLiteral("POST");
    m_postBody = body;
    m_postContentType = contentType;
}

void DownloadEngine::provideCredentials(const QString &user, const QString &pass)
{
    m_authUser = user;
    m_authPass = pass;
    m_authHeader.clear();
    if (m_authWorker) {
        Worker *w = m_authWorker;
        m_authWorker = nullptr;
        if (!w->retired) {
            w->buf.clear();
            w->connectTo(w->url);
        }
    }
}

bool DownloadEngine::handleAuthChallenge(Worker *w, const QByteArray &head)
{
    QString www;
    for (const QByteArray &l : splitCrlf(head)) {
        if (QString::fromLatin1(l).startsWith(QStringLiteral("www-authenticate"),
                                            Qt::CaseInsensitive)) {
            www = QString::fromLatin1(l.mid(l.indexOf(':') + 1).trimmed());
            break;
        }
    }
    if (www.isEmpty() || ++m_authAttempts > 3)
        return false;

    const Auth::Challenge ch = Auth::parseChallenge(www);
    QString user = m_authUser, pass = m_authPass;
    if (user.isEmpty() && m_credentialLookup) {
        const QString proto = m_url.scheme().toLower();
        if (!m_credentialLookup(m_finalUrl.isEmpty() ? m_url.host() : m_finalUrl.host(),
                                proto, user, pass)) {
            m_authWorker = w;
            emit authRequired(m_url.host(), ch.params.value(QStringLiteral("realm")), ch.scheme);
            return true;   // paused until provideCredentials / UI cancel
        }
    }
    if (user.isEmpty()) {
        m_authWorker = w;
        emit authRequired(m_url.host(), ch.params.value(QStringLiteral("realm")), ch.scheme);
        return true;
    }
    m_authHeader = Auth::buildHeader(ch, w->url, m_method, user, pass);
    w->buf.clear();
    w->connectTo(w->url);   // retry with Authorization
    return true;
}

void DownloadEngine::setBandwidthLimitKb(qint64 kb)
{
    m_limitBytesPerSec = qMax<qint64>(0, kb) * 1024;
    if (m_limitBytesPerSec == 0)
        m_budget = 0;
}

void DownloadEngine::setConnectionCount(int n)
{
    m_targetConnections = qBound(1, n, kMaxConnections);
    if (m_state != State::Downloading)
        return;

    if (m_activeWorkers < m_targetConnections)
        launchWorkers();

    // shrink: retire the newest workers
    for (auto it = m_workers.rbegin();
         it != m_workers.rend() && m_activeWorkers > m_targetConnections; ++it) {
        Worker *w = *it;
        if (!w->retired && w->counted) {
            if (w->segIndex >= 0)
                m_table.segments()[size_t(w->segIndex)].active = false;
            w->retire();
        }
    }
}

qint64 DownloadEngine::takeBudget(qint64 wanted)
{
    if (m_limitBytesPerSec <= 0)
        return wanted;
    const qint64 granted = qMin(wanted, m_budget);
    m_budget -= granted;
    return granted;
}

void DownloadEngine::replenishBudget()
{
    if (m_limitBytesPerSec <= 0)
        return;
    const qint64 slice = m_limitBytesPerSec / 10;
    m_budget = qMin(m_budget + slice, slice * 3);
    for (Worker *w : m_workers)
        if (!w->retired && w->phase == Worker::Phase::Body)
            w->pump();
}

void DownloadEngine::onTick()
{
    replenishBudget();
    const qint64 elapsed = m_clock.elapsed();
    if (elapsed >= 1000) {
        m_speed = (downloadedBytes() - m_speedBase) * 1000 / elapsed;
        m_speedBase = downloadedBytes();
        m_clock.restart();
        emit progress(downloadedBytes(), m_total, m_speed);

        QVector<ConnInfo> infos;
        for (const Worker *w : m_workers) {
            if (w->retired || w->segIndex < 0)
                continue;
            ConnInfo ci;
            ci.id = w->id;
            ci.start = w->rangeStart;
            ci.end = w->rangeEnd;
            ci.downloaded = w->pos - w->rangeStart;
            switch (w->phase) {
            case Worker::Phase::Connecting: ci.state = tr("Connecting"); break;
            case Worker::Phase::Headers: ci.state = tr("Starting..."); break;
            case Worker::Phase::Body: ci.state = tr("Downloading..."); break;
            case Worker::Phase::Done: ci.state = tr("Completed"); break;
            case Worker::Phase::Failed: ci.state = tr("Error"); break;
            }
            infos.append(ci);
        }
        emit connectionsInfo(infos);
    }
    if (++m_tickCount % 20 == 0 && m_state == State::Downloading)
        saveState();
}

void DownloadEngine::streamComplete(Worker *w)
{
    m_total = downloadedBytes();
    if (w->segIndex >= 0) {
        Segment &s = m_table.segments()[size_t(w->segIndex)];
        s.downloaded = m_total;
        s.end = m_total - 1;
        s.active = false;
    }
    if (w->counted) {
        --m_activeWorkers;
        w->counted = false;
    }
    w->retire();
    saveState();
    checkCompletion();
}

void DownloadEngine::checkCompletion()
{
    if (m_state != State::Downloading)
        return;
    if (m_total > 0 && m_table.remainingCount() == 0) {
        saveState();
        finish(true, QString());
    }
}

bool DownloadEngine::finishToFile(const QString &finalPath)
{
    QFile::remove(finalPath);
    const QString cleanup[] = {segPath(), segPath() + QStringLiteral(".bak")};
    if (QFile::rename(m_partPath, finalPath)) {
        for (const QString &f : cleanup)
            QFile::remove(f);
        return true;
    }
    if (QFile::copy(m_partPath, finalPath)) {
        QFile::remove(m_partPath);
        for (const QString &f : cleanup)
            QFile::remove(f);
        return true;
    }
    return false;
}

void DownloadEngine::finish(bool ok, const QString &err)
{
    if (m_state == State::Completed || m_state == State::Failed || m_state == State::Canceled)
        return;
    m_tick.stop();
    for (Worker *w : m_workers)
        w->retire();
    m_error = err;
    setState(ok ? State::Completed : State::Failed);
    emit finished(ok, err);
}

void DownloadEngine::setState(State s)
{
    if (m_state == s)
        return;
    m_state = s;
    emit stateChanged(s);
}

void DownloadEngine::saveState()
{
    m_table.save(segPath());
}

} // namespace neat
