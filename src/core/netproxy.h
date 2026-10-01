#pragma once
#include <QNetworkProxy>
#include <QUrl>

namespace neat {

// Builds the per-socket proxy from the original settings keys:
// {HTTP|HTTPS|FTP}_IsActive / _ProxyAddress / _ProxyPort / _ProxyType
// ("No Proxy" | "HTTP-Proxy" | "SOCKS-Proxy") / _UserName / _PassWord.
namespace NetProxy {

QNetworkProxy proxyFor(const QUrl &url);
void fillAuthenticator(const QNetworkProxy &proxy, QAuthenticator *auth);

} // namespace NetProxy

} // namespace neat
