// Unit checks for NetProxy::proxyFor semantics: main (HTTP) block applies to
// every scheme unless a checked https/ftp row overrides it.
#include "core/netproxy.h"
#include "core/settings.h"

#include <QCoreApplication>
#include <QUrl>
#include <cstdio>

using namespace neat;

static int g_failures = 0;
static void check(bool cond, const char *what)
{
    std::printf("%s: %s\n", cond ? "PASS" : "FAIL", what);
    if (!cond)
        ++g_failures;
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    Settings &s = Settings::instance();

    // 1) nothing configured -> direct everywhere
    check(NetProxy::proxyFor(QUrl("http://a/")).type() == QNetworkProxy::NoProxy, "none-http-direct");
    check(NetProxy::proxyFor(QUrl("https://a/")).type() == QNetworkProxy::NoProxy, "none-https-direct");

    // 2) main block only -> inherited by https and ftp
    s.setProxyActive("HTTP", true);
    s.setProxyType("HTTP", "HTTP-Proxy");
    s.setProxyAddress("HTTP", "127.0.0.1");
    s.setProxyPort("HTTP", 8080);
    const QNetworkProxy httpMain = NetProxy::proxyFor(QUrl("http://a/"));
    const QNetworkProxy httpsMain = NetProxy::proxyFor(QUrl("https://a/"));
    const QNetworkProxy ftpMain = NetProxy::proxyFor(QUrl("ftp://a/"));
    check(httpMain.type() == QNetworkProxy::HttpProxy && httpMain.hostName() == "127.0.0.1"
              && httpMain.port() == 8080, "main-http-applies");
    check(httpsMain.type() == QNetworkProxy::HttpProxy && httpsMain.port() == 8080, "main-inherited-https");
    check(ftpMain.type() == QNetworkProxy::HttpProxy && ftpMain.port() == 8080, "main-inherited-ftp");

    // 3) https override row wins for https only
    s.setProxyActive("HTTPS", true);
    s.setProxyType("HTTPS", "HTTP-Proxy");
    s.setProxyAddress("HTTPS", "10.0.0.9");
    s.setProxyPort("HTTPS", 3128);
    const QNetworkProxy httpsOvr = NetProxy::proxyFor(QUrl("https://a/"));
    check(httpsOvr.hostName() == "10.0.0.9" && httpsOvr.port() == 3128, "https-override-wins");
    check(NetProxy::proxyFor(QUrl("http://a/")).hostName() == "127.0.0.1", "http-unaffected-by-override");
    s.setProxyActive("HTTPS", false);
    s.setProxyType("HTTPS", "No Proxy");
    check(NetProxy::proxyFor(QUrl("https://a/")).port() == 8080, "override-off-falls-back");

    // 4) socks selection
    s.setProxyType("HTTP", "SOCKS-Proxy");
    check(NetProxy::proxyFor(QUrl("http://a/")).type() == QNetworkProxy::Socks5Proxy, "socks-type");

    // 5) credentials propagate
    s.setProxyUser("HTTP", "u1");
    s.setProxyPassword("HTTP", "p1");
    const QNetworkProxy cred = NetProxy::proxyFor(QUrl("http://a/"));
    check(cred.user() == "u1" && cred.password() == "p1", "credentials-propagate");

    std::printf(g_failures ? "PROXY-SEMANTICS: FAILED\n" : "PROXY-SEMANTICS: OK\n");
    return g_failures ? 1 : 0;
}
