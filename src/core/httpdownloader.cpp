#include "httpdownloader.h"
#include "settings.h"

#include <QDir>
#include <QFileInfo>
#include <QSslConfiguration>

namespace neat {

static const int kMaxRedirects = 20;
static const int kConnectTimeoutMs = 30000;

HttpDownloader::HttpDownloader(QObject *parent)
    : QObject(parent)
{
    connect(&m_speedTimer, &QTimer::timeout, this, [this] {
        const qint64 elapsed = m_clock.elapsed();
        if (elapsed > 0 && m_state == State::Downloading) {
            m_speed = (m_downloaded - m_speedBase) * 1000 / elapsed;
            m_speedBase = m_downloaded;
            m_clock.restart();
            emit progress(m_downloaded, m_total, m_speed);
        }
    });
}

HttpDownloader::~HttpDownloader()
{
    if (m_file.isOpen())
        m_file.close();
}

QString HttpDownloader::suggestFileName(const QUrl &url)
{
    const QString name = QFileInfo(url.path()).fileName();
    if (!name.isEmpty())
        return name;
    return QStringLiteral("download");
}

void HttpDownloader::start(const QUrl &url, const QString &partFilePath)
{
    m_url = url;
    m_partPath = partFilePath;
    m_downloaded = 0;
    m_total = -1;
    m_redirects = 0;
    m_startedOnce = true;

    QDir().mkpath(QFileInfo(partFilePath).absolutePath());
    m_file.setFileName(partFilePath);
    if (!m_file.open(QIODevice::WriteOnly | QIODevice::Append)) {
        finish(false, QStringLiteral("Failed To Open Segment-File."));
        return;
    }
    setState(State::Connecting);
    sendRequest();
}

void HttpDownloader::pause()
{
    if (m_state != State::Downloading && m_state != State::Connecting)
        return;
    setState(State::Paused);   // set first: abort() may emit disconnected synchronously
    if (m_sock)
        m_sock->abort();
    m_phase = Phase::Headers;   // fresh response expected on resume
    m_buf.clear();
}

void HttpDownloader::resume()
{
    if (m_state != State::Paused)
        return;
    setState(State::Connecting);
    sendRequest();
}

void HttpDownloader::cancel()
{
    if (m_state == State::Completed || m_state == State::Canceled)
        return;
    if (m_sock)
        m_sock->abort();
    m_file.close();
    QFile::remove(m_partPath);
    setState(State::Canceled);
    emit finished(false, QStringLiteral("Download Canceled By User."));
}

void HttpDownloader::sendRequest()
{
    if (m_sock) {
        m_sock->close();
        m_sock->deleteLater();
        m_sock = nullptr;
    }
    m_tls = m_url.scheme().compare(QStringLiteral("https"), Qt::CaseInsensitive) == 0;
    const int port = m_url.port(m_tls ? 443 : 80);

    if (m_tls) {
        auto *ssl = new QSslSocket(this);
        ssl->setPeerVerifyMode(QSslSocket::VerifyPeer);
        m_sock = ssl;
    } else {
        m_sock = new QTcpSocket(this);
    }
    connect(m_sock, &QTcpSocket::connected, this, &HttpDownloader::onConnected);
    connect(m_sock, &QTcpSocket::readyRead, this, &HttpDownloader::onReadyRead);
    connect(m_sock, &QTcpSocket::disconnected, this, [this] {
        if (sender() != m_sock)
            return;
        if (m_state == State::Downloading && m_phase != Phase::Done)
            finish(m_contentLength < 0 && m_downloaded > 0,
                   m_contentLength < 0 ? QString() : QStringLiteral("Server Closed Connection Suddenly."));
    });
    using ErrSig = void (QAbstractSocket::*)(QAbstractSocket::SocketError);
    connect(m_sock, &QTcpSocket::errorOccurred, this, &HttpDownloader::onSocketError);

    if (m_tls)
        static_cast<QSslSocket *>(m_sock)->connectToHostEncrypted(m_url.host(), port);
    else
        m_sock->connectToHost(m_url.host(), port);
    QTimer::singleShot(kConnectTimeoutMs, this, [this] {
        if (m_state == State::Connecting && m_sock && m_sock->state() != QAbstractSocket::ConnectedState)
            finish(false, QStringLiteral("Connection TimeOut."));
    });
}

void HttpDownloader::onConnected()
{
    if (sender() != m_sock)
        return;
    if (m_tls) {
        auto *ssl = static_cast<QSslSocket *>(m_sock);
        if (!ssl->isEncrypted())
            return; // encrypted() will re-enter via connected signal of QSslSocket
    }

    QString path = m_url.path();
    if (path.isEmpty())
        path = QStringLiteral("/");
    if (m_url.hasQuery())
        path += QLatin1Char('?') + m_url.query();

    QByteArray req;
    req += "GET " + path.toUtf8() + " HTTP/1.1\r\n";
    req += "Host: " + m_url.host(QUrl::FullyEncoded).toUtf8() + "\r\n";
    req += "User-Agent: " + Settings::instance().userAgent().toUtf8() + "\r\n";
    req += "Accept: */*\r\n";
    req += "Connection: close\r\n";
    if (m_downloaded > 0)
        req += "Range: bytes=" + QByteArray::number(m_downloaded) + "-\r\n";
    req += "\r\n";
    m_sock->write(req);
}

void HttpDownloader::onSocketError()
{
    if (sender() != m_sock)
        return;
    if (m_sock->error() == QAbstractSocket::RemoteHostClosedError)
        return;   // handled by disconnected()
    if (m_state == State::Connecting || m_state == State::Downloading)
        finish(false, QStringLiteral("No Internet Connection or DNS Failed."));
}

void HttpDownloader::onReadyRead()
{
    if (sender() != m_sock)
        return;
    if (m_state != State::Downloading && m_state != State::Connecting)
        return;
    m_buf += m_sock->readAll();

    if (m_phase == Phase::Headers) {
        const int idx = m_buf.indexOf("\r\n\r\n");
        if (idx < 0)
            return;
        const QByteArray head = m_buf.left(idx);
        m_buf.remove(0, idx + 4);
        if (!parseHeaders(head))
            return;   // redirected / failed inside
        consumeBody();
    } else {
        consumeBody();
    }
}

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

bool HttpDownloader::parseHeaders(const QByteArray &raw)
{
    const QList<QByteArray> lines = splitCrlf(raw);
    const QByteArray statusLine = lines.value(0);
    const int sp1 = statusLine.indexOf(' ');
    const int sp2 = statusLine.indexOf(' ', sp1 + 1);
    const int code = statusLine.mid(sp1 + 1, sp2 - sp1 - 1).toInt();

    QByteArray location;
    bool acceptRanges = false, chunked = false;
    qint64 contentLength = -1;
    for (int i = 1; i < lines.size(); ++i) {
        const QByteArray &l = lines[i];
        const int colon = l.indexOf(':');
        if (colon < 0)
            continue;
        const QByteArray name = l.left(colon).trimmed().toLower();
        const QByteArray value = l.mid(colon + 1).trimmed();
        if (name == "location")
            location = value;
        else if (name == "content-length")
            contentLength = value.toLongLong();
        else if (name == "accept-ranges" && value.toLower().contains("bytes"))
            acceptRanges = true;
        else if (name == "transfer-encoding" && value.toLower().contains("chunked"))
            chunked = true;
    }

    // Redirects: 301/302/303/307/308
    if ((code >= 301 && code <= 303) || code == 307 || code == 308) {
        if (++m_redirects > kMaxRedirects) {
            finish(false, QStringLiteral("Server Redirected to an unsupported  protocol/Invalid URL."));
            return false;
        }
        const QUrl next = m_url.resolved(QUrl::fromEncoded(location, QUrl::StrictMode));
        if (next.scheme() != QLatin1String("http") && next.scheme() != QLatin1String("https")) {
            finish(false, QStringLiteral("Server Redirected to an unsupported  protocol/Invalid URL."));
            return false;
        }
        m_url = next;
        m_buf.clear();
        m_sock->abort();
        setState(State::Connecting);
        sendRequest();
        return false;
    }

    if (code == 206) {
        // resumed: keep m_downloaded; total derived from Content-Range when present
        for (const auto &l : lines) {
            if (QString::fromLatin1(l).startsWith(QStringLiteral("content-range"), Qt::CaseInsensitive)) {
                const int slash = l.lastIndexOf('/');
                if (slash >= 0)
                    m_total = l.mid(slash + 1).trimmed().toLongLong();
            }
        }
    } else if (code >= 200 && code < 300) {
        if (m_downloaded > 0) {
            // server ignored the Range: restart from scratch
            m_file.close();
            m_file.open(QIODevice::WriteOnly | QIODevice::Truncate);
            m_downloaded = 0;
        }
        m_total = contentLength;
    } else if (code == 416) {
        finish(false, QStringLiteral("File Has been Changed on Server. You must Redownload it from beggining.(Reply-Code=416)"));
        return false;
    } else {
        finish(false, QStringLiteral("HTTP %1.").arg(code));
        return false;
    }

    m_resumable = acceptRanges;
    m_bodyOffset = m_downloaded;
    m_finalUrl = m_url;
    m_contentLength = chunked ? -2 : contentLength;   // -2 marks chunked
    m_phase = chunked ? Phase::ChunkSize : Phase::Body;
    setState(State::Downloading);
    if (!m_speedTimer.isActive()) {
        m_speedTimer.start(1000);
        m_clock.restart();
        m_speedBase = m_downloaded;
    }
    return true;
}

qint64 HttpDownloader::chunkStep(qint64 avail)
{
    // returns bytes of body consumed this call
    while (avail > 0) {
        if (m_phase == Phase::ChunkSize) {
            if (m_buf.startsWith("\r\n"))
                m_buf.remove(0, 2);   // CRLF terminating the previous chunk
            const int idx = m_buf.indexOf("\r\n");
            if (idx < 0)
                return 0;
            bool ok = false;
            m_chunkRemain = m_buf.left(idx).toLongLong(&ok, 16);
            m_buf.remove(0, idx + 2);
            avail = m_buf.size();
            if (m_chunkRemain == 0) {
                m_phase = Phase::ChunkTrail;
                continue;
            }
            m_phase = Phase::ChunkData;
        } else if (m_phase == Phase::ChunkData) {
            const qint64 take = qMin<qint64>(m_chunkRemain, avail);
            const QByteArray body = m_buf.left(take);
            m_file.write(body);
            m_downloaded += take;
            m_buf.remove(0, take);
            m_chunkRemain -= take;
            if (m_chunkRemain == 0)
                m_phase = Phase::ChunkSize;   // trailing CRLF eaten in next pass
            while (m_buf.startsWith("\r\n"))
                m_buf.remove(0, 2);
            avail = m_buf.size();
            emit progress(m_downloaded, m_total, m_speed);
        } else if (m_phase == Phase::ChunkTrail) {
            const int idx = m_buf.indexOf("\r\n");
            if (idx < 0)
                return 0;
            if (idx == 0) {   // empty trailer line: chunked body complete
                m_buf.clear();
                m_phase = Phase::Done;
                if (m_total < 0)
                    m_total = m_downloaded;
                m_file.close();
                finish(true, QString());
                return 0;
            }
            m_buf.remove(0, idx + 2);   // skip one trailer field
            avail = m_buf.size();
        } else {
            return 0;
        }
    }
    return 0;
}

void HttpDownloader::consumeBody()
{
    if (m_phase == Phase::ChunkSize || m_phase == Phase::ChunkData || m_phase == Phase::ChunkTrail) {
        chunkStep(m_buf.size());
        return;
    }
    if (m_phase != Phase::Body)
        return;

    if (!m_buf.isEmpty()) {
        m_file.write(m_buf);
        m_downloaded += m_buf.size();
        m_buf.clear();
        emit progress(m_downloaded, m_total, m_speed);
    }
    if (m_contentLength > 0 && m_downloaded - m_bodyOffset >= m_contentLength) {
        m_phase = Phase::Done;
        m_file.close();
        finish(true, QString());
    }
}

void HttpDownloader::finish(bool ok, const QString &error)
{
    if (m_state == State::Completed || m_state == State::Failed || m_state == State::Canceled)
        return;
    m_speedTimer.stop();
    m_error = error;
    if (m_file.isOpen())
        m_file.close();
    setState(ok ? State::Completed : State::Failed);
    emit finished(ok, error);
}

void HttpDownloader::setState(State s)
{
    if (m_state == s)
        return;
    m_state = s;
    emit stateChanged(s);
}

} // namespace neat
