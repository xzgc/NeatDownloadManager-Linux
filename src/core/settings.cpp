#include "settings.h"

#include <QStandardPaths>

namespace neat {

const QString Settings::DefaultUserAgent =
    QStringLiteral("Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) AppleWebKit/537.36 "
                   "(KHTML, like Gecko) Chrome/91.0.4472.114 Safari/537.36");

static const char *kGroup = "main";

Settings &Settings::instance()
{
    static Settings s;
    return s;
}

Settings::Settings()
    : m_s(QStringLiteral("neatdm"), QStringLiteral("settings"))
{
    m_s.beginGroup(kGroup);
}

#define RW(fnGet, fnSet, key, type, defval)                          \
    type Settings::fnGet() const { return m_s.value(key, defval).value<type>(); } \
    void Settings::fnSet(type v) { m_s.setValue(key, v); }

QString Settings::downloadDirectory() const
{
    return m_s.value("DownloadDirectory",
                     QStandardPaths::writableLocation(QStandardPaths::DownloadLocation))
        .toString();
}
void Settings::setDownloadDirectory(const QString &v) { m_s.setValue("DownloadDirectory", v); }

RW(categoryFolders, setCategoryFolders, "CategoryFolders", bool, true)
RW(completionDialog, setCompletionDialog, "CompletionDialog", bool, true)
RW(autoStart, setAutoStart, "AppAutoStart", bool, false)
RW(bandwidthLimitKb, setBandwidthLimitKb, "BandWidthLimit", qint64, qint64(0))
RW(maxConnections, setMaxConnections, "MaxConnections", int, 16)
RW(connectionsAllAtOnce, setConnectionsAllAtOnce, "ConnectionsAtOnce", bool, true)
RW(useCustomUserAgent, setUseCustomUserAgent, "UseUAgent", bool, false)
RW(socksVersion, setSocksVersion, "SocksVersion", int, 5)
RW(lastDownloadId, setLastDownloadId, "LastDownloadID", qint64, qint64(0))

QString Settings::language() const
{
    return m_s.value("Language", QStringLiteral("en")).toString();
}
void Settings::setLanguage(const QString &v) { m_s.setValue("Language", v); }

QString Settings::userAgent() const { return m_s.value("DefaultAgent", DefaultUserAgent).toString(); }
void Settings::setUserAgent(const QString &v) { m_s.setValue("DefaultAgent", v); }

bool Settings::browserPanel(const QString &browser) const
{
    return m_s.value(browser + QStringLiteral("Panel"), true).toBool();
}
void Settings::setBrowserPanel(const QString &browser, bool v)
{
    m_s.setValue(browser + QStringLiteral("Panel"), v);
}

QString Settings::proxyKey(const QString &proto, const char *suffix) const
{
    return proto + QLatin1String(suffix);
}

bool Settings::proxyActive(const QString &proto) const
{
    return m_s.value(proxyKey(proto, "_IsActive"), false).toBool();
}
void Settings::setProxyActive(const QString &proto, bool v)
{
    m_s.setValue(proxyKey(proto, "_IsActive"), v);
}
QString Settings::proxyAddress(const QString &proto) const
{
    return m_s.value(proxyKey(proto, "_ProxyAddress")).toString();
}
void Settings::setProxyAddress(const QString &proto, const QString &v)
{
    m_s.setValue(proxyKey(proto, "_ProxyAddress"), v);
}
int Settings::proxyPort(const QString &proto) const
{
    return m_s.value(proxyKey(proto, "_ProxyPort"), 0).toInt();
}
void Settings::setProxyPort(const QString &proto, int v)
{
    m_s.setValue(proxyKey(proto, "_ProxyPort"), v);
}
QString Settings::proxyType(const QString &proto) const
{
    return m_s.value(proxyKey(proto, "_ProxyType"), QStringLiteral("No Proxy")).toString();
}
void Settings::setProxyType(const QString &proto, const QString &v)
{
    m_s.setValue(proxyKey(proto, "_ProxyType"), v);
}
QString Settings::proxyUser(const QString &proto) const
{
    return m_s.value(proxyKey(proto, "_UserName")).toString();
}
void Settings::setProxyUser(const QString &proto, const QString &v)
{
    m_s.setValue(proxyKey(proto, "_UserName"), v);
}
QString Settings::proxyPassword(const QString &proto) const
{
    return m_s.value(proxyKey(proto, "_PassWord")).toString();
}
void Settings::setProxyPassword(const QString &proto, const QString &v)
{
    m_s.setValue(proxyKey(proto, "_PassWord"), v);
}

} // namespace neat
