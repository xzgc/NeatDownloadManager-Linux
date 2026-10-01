#include "netproxy.h"
#include "settings.h"

#include <QAuthenticator>

namespace neat {
namespace NetProxy {

static QString protoKey(const QUrl &url)
{
    const QString s = url.scheme().toLower();
    if (s == QLatin1String("https"))
        return QStringLiteral("HTTPS");
    if (s == QLatin1String("ftp"))
        return QStringLiteral("FTP");
    return QStringLiteral("HTTP");
}

QNetworkProxy proxyFor(const QUrl &url)
{
    const Settings &s = Settings::instance();
    // one protocol block -> a concrete proxy; inactive/empty blocks degrade to
    // NoProxy so the caller can fall through to the main configuration
    auto build = [&s](const QString &k) {
        const QString type = s.proxyType(k);
        QNetworkProxy::ProxyType t = QNetworkProxy::NoProxy;
        if (type == QLatin1String("HTTP-Proxy"))
            t = QNetworkProxy::HttpProxy;
        else if (type == QLatin1String("SOCKS-Proxy"))
            t = QNetworkProxy::Socks5Proxy;
        if (!s.proxyActive(k) || t == QNetworkProxy::NoProxy
            || s.proxyAddress(k).isEmpty() || s.proxyPort(k) <= 0)
            return QNetworkProxy(QNetworkProxy::NoProxy);

        QNetworkProxy p(t);
        p.setHostName(s.proxyAddress(k));
        p.setPort(s.proxyPort(k));
        if (!s.proxyUser(k).isEmpty()) {
            p.setUser(s.proxyUser(k));
            p.setPassword(s.proxyPassword(k));
        }
        return p;
    };

    // checked https/ftp rows override the global proxy for their protocol;
    // everything else rides the main (HTTP) configuration
    const QString key = protoKey(url);
    if (key != QLatin1String("HTTP")) {
        const QNetworkProxy p = build(key);
        if (p.type() != QNetworkProxy::NoProxy)
            return p;
    }
    return build(QStringLiteral("HTTP"));
}

void fillAuthenticator(const QNetworkProxy &proxy, QAuthenticator *auth)
{
    const Settings &s = Settings::instance();
    // proxy == the socket's proxy; match credentials by address against all
    // three protocol blocks (they share server credentials in practice)
    for (const QString &key : {QStringLiteral("HTTP"), QStringLiteral("HTTPS"),
                               QStringLiteral("FTP")}) {
        if (s.proxyAddress(key) == proxy.hostName() && !s.proxyUser(key).isEmpty()) {
            auth->setUser(s.proxyUser(key));
            auth->setPassword(s.proxyPassword(key));
            return;
        }
    }
}

} // namespace NetProxy
} // namespace neat
