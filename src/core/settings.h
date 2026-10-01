#pragma once
#include <QSettings>
#include <QString>

namespace neat {

// Persistent settings, stored at ~/.config/neatdm/settings.ini.
// Key names mirror the original app (Mac NSUserDefaults / Win registry HKCU\Software\NeatDM)
// so behavior and defaults stay comparable during replication.
class Settings {
public:
    static Settings &instance();

    QString downloadDirectory() const;
    void setDownloadDirectory(const QString &v);

    bool categoryFolders() const;            // CategoryFolders
    void setCategoryFolders(bool v);

    bool completionDialog() const;           // CompletionDialog
    void setCompletionDialog(bool v);

    bool autoStart() const;                  // AppAutoStart
    void setAutoStart(bool v);

    qint64 bandwidthLimitKb() const;         // BandWidthLimit, KB/s, 0 = unlimited
    void setBandwidthLimitKb(qint64 v);

    int maxConnections() const;              // MaxConnections (default 16, MKV flow uses 32)
    void setMaxConnections(int v);

    bool connectionsAllAtOnce() const;       // ConnectionsAtOnce
    void setConnectionsAllAtOnce(bool v);

    bool useCustomUserAgent() const;         // UseUAgent
    void setUseCustomUserAgent(bool v);

    QString userAgent() const;               // DefaultAgent
    QString language() const;                // Language: "en" | "zh"
    void setLanguage(const QString &v);
    void setUserAgent(const QString &v);

    int socksVersion() const;                // SocksVersion (4a/5)
    void setSocksVersion(int v);

    bool browserPanel(const QString &browser) const;   // ChromePanel / EdgePanel / FoxPanel
    void setBrowserPanel(const QString &browser, bool v);

    qint64 lastDownloadId() const;           // LastDownloadID
    void setLastDownloadId(qint64 v);

    // Proxy block, per protocol "HTTP"/"HTTPS"/"FTP": <p>_IsActive, <p>_ProxyAddress,
    // <p>_ProxyPort, <p>_ProxyType ("No Proxy"/"HTTP-Proxy"/"SOCKS-Proxy"),
    // <p>_UserName, <p>_PassWord.
    bool proxyActive(const QString &proto) const;
    void setProxyActive(const QString &proto, bool v);
    QString proxyAddress(const QString &proto) const;
    void setProxyAddress(const QString &proto, const QString &v);
    int proxyPort(const QString &proto) const;
    void setProxyPort(const QString &proto, int v);
    QString proxyType(const QString &proto) const;
    void setProxyType(const QString &proto, const QString &v);
    QString proxyUser(const QString &proto) const;
    void setProxyUser(const QString &proto, const QString &v);
    QString proxyPassword(const QString &proto) const;
    void setProxyPassword(const QString &proto, const QString &v);

    static const QString DefaultUserAgent;

private:
    Settings();
    QSettings m_s;
    QString proxyKey(const QString &proto, const char *suffix) const;
};

} // namespace neat
