#include "hls.h"
#include "netproxy.h"
#include "settings.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSslSocket>
#include <QTcpSocket>

namespace neat {

static const int kConnectTimeoutMs = 30000;
static const int kMaxRedirects = 10;

// --------------------------------------------------------------- Fetcher ----
// One HTTP GET into memory (TS segments are small relative to ranges).
class HlsDownloader::Fetcher : public QObject {
public:
    enum class Phase { Connecting, Headers, Body, Done };

    Fetcher(HlsDownloader *owner, int index, const QUrl &url)
        : QObject(owner)
        , eng(owner)
        , index(index)
        , url(url)
    {
    }

    ~Fetcher() override
    {
        if (sock) {
            sock->abort();
            sock->deleteLater();
        }
    }

    void start()
    {
        const bool tls = url.scheme().compare(QStringLiteral("https"), Qt::CaseInsensitive) == 0;
        sock = tls ? static_cast<QTcpSocket *>(new QSslSocket(this)) : new QTcpSocket(this);
        sock->setProxy(NetProxy::proxyFor(url));
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
            QByteArray req;
            req += "GET " + path.toUtf8() + " HTTP/1.1\r\n";
            req += "Host: " + url.host(QUrl::FullyEncoded).toUtf8() + "\r\n";
            req += "User-Agent: " + Settings::instance().userAgent().toUtf8() + "\r\n";
            req += "Accept: */*\r\n";
            req += "Connection: close\r\n";
            for (const QString &h : eng->m_extraHeaders)
                req += h.toUtf8() + "\r\n";
            req += "\r\n";
            sock->write(req);
            phase = Phase::Headers;
        });
        connect(sock, &QTcpSocket::readyRead, this, [this] { pump(); });
        connect(sock, &QTcpSocket::disconnected, this, [this] {
            if (phase == Phase::Body && (contentRemain < 0 || body.size() >= contentRemain))
                complete();
            else if (phase == Phase::Body)
                fail(QStringLiteral("Server Closed Connection Suddenly."));
        });
        connect(sock, &QTcpSocket::errorOccurred, this, [this](QAbstractSocket::SocketError e) {
            if (e != QAbstractSocket::RemoteHostClosedError && phase != Phase::Done)
                fail(QStringLiteral("No Internet Connection or DNS Failed."));
        });
        sock->connectToHost(url.host(), url.port(tls ? 443 : 80));
        QTimer::singleShot(kConnectTimeoutMs, this, [this] {
            if (phase == Phase::Connecting)
                fail(QStringLiteral("Connection TimeOut."));
        });
    }

    void abort()
    {
        phase = Phase::Done;
        if (sock)
            sock->abort();
    }

    int index;
    QByteArray body;

private:
    void pump()
    {
        if (phase == Phase::Headers) {
            buf += sock->readAll();
            const int idx = buf.indexOf("\r\n\r\n");
            if (idx < 0)
                return;
            const QByteArray head = buf.left(idx);
            buf.remove(0, idx + 4);
            body = buf;
            buf.clear();

            const QList<QByteArray> lines = head.split('\n');
            const QByteArray sl = lines.value(0).trimmed();
            const int sp1 = sl.indexOf(' ');
            const int sp2 = sl.indexOf(' ', sp1 + 1);
            const int code = sl.mid(sp1 + 1, sp2 - sp1 - 1).toInt();
            if (code >= 301 && code <= 308) {
                for (const QByteArray &l : lines) {
                    if (QString::fromLatin1(l).startsWith(QStringLiteral("location"), Qt::CaseInsensitive)) {
                        url = url.resolved(
                            QUrl::fromEncoded(l.mid(l.indexOf(':') + 1).trimmed(), QUrl::StrictMode));
                        break;
                    }
                }
                if (++redirects > kMaxRedirects) {
                    fail(QStringLiteral("Server Redirected to an unsupported  protocol/Invalid URL."));
                    return;
                }
                start();
                return;
            }
            if (code >= 200 && code < 300) {
                for (const QByteArray &l : lines) {
                    if (QString::fromLatin1(l).startsWith(QStringLiteral("content-length"), Qt::CaseInsensitive))
                        contentRemain = l.mid(l.indexOf(':') + 1).trimmed().toLongLong();
                }
                phase = Phase::Body;
                if (!body.isEmpty() && contentRemain > 0 && body.size() >= contentRemain)
                    complete();
                return;
            }
            fail(QStringLiteral("HTTP %1.").arg(code));
            return;
        }
        if (phase != Phase::Body)
            return;
        body += sock->readAll();
        if (contentRemain > 0 && body.size() >= contentRemain)
            complete();
    }

    void complete()
    {
        phase = Phase::Done;
        if (contentRemain > 0 && body.size() > contentRemain)
            body = body.left(int(contentRemain));
        eng->fetcherDone(this);
    }

    void fail(const QString &err)
    {
        if (phase == Phase::Done)
            return;
        phase = Phase::Done;
        eng->fetcherFailed(this, err);
    }

    HlsDownloader *eng;
    QUrl url;
    QTcpSocket *sock = nullptr;
    Phase phase = Phase::Connecting;
    QByteArray buf;
    qint64 contentRemain = -1;
    int redirects = 0;
};

// ------------------------------------------------------------ HlsDownloader --

HlsDownloader::HlsDownloader(qint64 id, const QUrl &playlistUrl, const QString &partPath,
                             QObject *parent)
    : QObject(parent)
    , m_id(id)
    , m_playlistUrl(playlistUrl)
    , m_partPath(partPath)
{
    m_tick.setInterval(1000);
    connect(&m_tick, &QTimer::timeout, this, [this] {
        const qint64 el = m_clock.elapsed();
        if (el >= 1000) {
            m_speed = (m_bytes - m_speedBase) * 1000 / el;
            m_speedBase = m_bytes;
            m_clock.restart();
            emit progress(m_bytes, -1, m_speed);
            emit segmentsInfo(m_nextIndex, int(m_segments.size()));
            QVector<FetchInfo> infos;
            for (int i = 0; i < m_inFlight.size(); ++i) {
                if (!m_inFlight[int(i)])
                    continue;
                FetchInfo fi;
                fi.index = int(i);
                fi.state = QStringLiteral("Downloading...");
                infos.append(fi);
            }
            emit fetchersInfo(infos);
        }
    });
}

void HlsDownloader::start()
{
    QDir().mkpath(QFileInfo(m_partPath).absolutePath());
    m_part.setFileName(m_partPath);
    if (!m_part.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        finish(false, QStringLiteral("Failed To Open Segment-File."));
        return;
    }
    setState(State::Probing);
    m_clock.restart();
    m_speedBase = m_bytes;
    m_tick.start();
    fetchPlaylist(m_playlistUrl, true);
}

void HlsDownloader::fetchPlaylist(const QUrl &url, bool master)
{
    m_masterFetch = master;
    m_playlistUrl = url;
    auto *f = new Fetcher(this, -1, url);
    f->start();
}

void HlsDownloader::pause()
{
    if (m_state != State::Downloading && m_state != State::Probing)
        return;
    setState(State::Paused);
    for (int i = 0; i < m_inFlight.size(); ++i) {
        if (m_inFlight[int(i)] && m_fetchers.value(int(i)))
            m_fetchers[int(i)]->abort();
        m_fetchers[int(i)] = nullptr;
    }
    m_inFlight.fill(false);
    m_activeFetchers = 0;
}

void HlsDownloader::resume()
{
    if (m_state != State::Paused || !m_part.isOpen())
        return;
    setState(State::Downloading);
    m_clock.restart();
    m_speedBase = m_bytes;
    pumpFetchers();
}

void HlsDownloader::cancel()
{
    if (m_state == State::Completed || m_state == State::Canceled)
        return;
    setState(State::Canceled);
    m_tick.stop();
    m_part.close();
    QFile::remove(m_partPath);
    emit finished(false, QStringLiteral("HLS-File  Download Canceled By User."));
}

bool HlsDownloader::finishToFile(const QString &finalPath)
{
    m_part.close();
    QFile::remove(finalPath);
    if (QFile::rename(m_partPath, finalPath))
        return true;
    if (QFile::copy(m_partPath, finalPath)) {
        QFile::remove(m_partPath);
        return true;
    }
    return false;
}

void HlsDownloader::parsePlaylist(const QByteArray &data, const QUrl &baseUrl, bool master)
{
    const QString text = QString::fromUtf8(data);
    const QStringList lines = text.split(QLatin1Char('\n'));
    if (!lines.isEmpty() && !lines[0].trimmed().startsWith(QLatin1String("#EXTM3U"))) {
        finish(false, QStringLiteral("Problem on Receiving HlsMaster file"));
        return;
    }

    if (master) {
        // variant list: pick the highest BANDWIDTH
        QUrl best;
        qint64 bestBw = -1;
        for (int i = 0; i < lines.size(); ++i) {
            const QString l = lines[i].trimmed();
            if (!l.startsWith(QLatin1String("#EXT-X-STREAM-INF")))
                continue;
            qint64 bw = -1;
            const int b = l.indexOf(QLatin1String("BANDWIDTH="), 0, Qt::CaseInsensitive);
            if (b >= 0)
                bw = l.mid(b + 10).split(QLatin1Char(',')).value(0).toLongLong();
            for (int j = i + 1; j < lines.size(); ++j) {
                const QString cand = lines[j].trimmed();
                if (cand.isEmpty() || cand.startsWith(QLatin1Char('#')))
                    continue;
                if (bw >= bestBw) {
                    bestBw = bw;
                    best = baseUrl.resolved(QUrl(cand));
                }
                break;
            }
        }
        if (best.isEmpty()) {
            // not a master after all — treat as media playlist
            parsePlaylist(data, baseUrl, false);
            return;
        }
        fetchPlaylist(best, false);
        return;
    }

    m_segments.clear();
    double pendingDuration = 0.0;
    for (const QString &raw : lines) {
        const QString l = raw.trimmed();
        if (l.isEmpty())
            continue;
        if (l.startsWith(QLatin1String("#EXTINF"))) {
            const int colon = l.indexOf(QLatin1Char(':'));
            pendingDuration = l.mid(colon + 1).section(QLatin1Char(','), 0, 0).toDouble();
        } else if (!l.startsWith(QLatin1Char('#'))) {
            SegmentItem item;
            item.url = baseUrl.resolved(QUrl(l));
            item.duration = pendingDuration;
            pendingDuration = 0.0;
            m_segments.append(item);
        }
    }
    if (m_segments.isEmpty()) {
        finish(false, QStringLiteral("Too many HLS Segments."));
        return;
    }
    m_ready = QVector<QByteArray>(m_segments.size());
    m_inFlight = QVector<bool>(m_segments.size(), false);
    m_fetchers = QVector<Fetcher *>(m_segments.size(), nullptr);
    setState(State::Downloading);
    pumpFetchers();
}

void HlsDownloader::pumpFetchers()
{
    if (m_state != State::Downloading)
        return;
    int inFlightCount = 0;
    for (bool b : m_inFlight)
        if (b)
            ++inFlightCount;
    for (int i = m_nextIndex; i < m_segments.size() && inFlightCount < kWindow; ++i) {
        if (m_inFlight[int(i)] || !m_ready[int(i)].isNull())
            continue;
        auto *f = new Fetcher(this, int(i), m_segments[int(i)].url);
        m_fetchers[int(i)] = f;
        m_inFlight[int(i)] = true;
        ++inFlightCount;
        f->start();
    }
}

void HlsDownloader::fetcherDone(HlsDownloader::Fetcher *f)
{
    const int idx = f->index;
    if (idx < 0) {
        const QByteArray payload = f->body;
        f->deleteLater();
        parsePlaylist(payload, m_playlistUrl, m_masterFetch);
        return;
    }
    f->deleteLater();
    if (idx < m_inFlight.size()) {
        m_inFlight[idx] = false;
        m_ready[idx] = f->body;
        if (m_fetchers.value(idx) == f)
            m_fetchers[idx] = nullptr;
    }
    m_bytes += f->body.size();
    flushInOrder();
    pumpFetchers();
}

void HlsDownloader::fetcherFailed(HlsDownloader::Fetcher *f, const QString &err)
{
    const int idx = f->index;
    f->deleteLater();
    if (idx >= 0 && idx < m_inFlight.size())
        m_inFlight[idx] = false;
    finish(false, err);
}

void HlsDownloader::flushInOrder()
{
    while (m_nextIndex < m_segments.size() && !m_ready[m_nextIndex].isNull()) {
        m_part.write(m_ready[m_nextIndex]);
        m_ready[m_nextIndex] = QByteArray(QLatin1String(""));   // non-null marks "used"
        ++m_nextIndex;
    }
    if (m_nextIndex >= m_segments.size()) {
        m_part.flush();
        finish(true, QString());
    }
}

void HlsDownloader::finish(bool ok, const QString &err)
{
    if (m_state == State::Completed || m_state == State::Failed || m_state == State::Canceled)
        return;
    m_tick.stop();
    m_error = err;
    if (ok)
        m_part.flush();
    setState(ok ? State::Completed : State::Failed);
    emit finished(ok, err);
}

void HlsDownloader::setState(State s)
{
    if (m_state == s)
        return;
    m_state = s;
    emit stateChanged(s);
}

} // namespace neat
