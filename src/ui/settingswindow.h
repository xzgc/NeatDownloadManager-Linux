#pragma once
#include <QDialog>

class QCheckBox;
class QComboBox;
class QTabWidget;
class QGroupBox;
class QLineEdit;
class QRadioButton;
class QTableWidget;

namespace neat {

// Replica of NeatSettingWindow: General / Connections / Proxy-Socks tabs plus
// the site credentials table. OK persists everything, Cancel discards.
class SettingsWindow : public QDialog {
    Q_OBJECT
public:
    explicit SettingsWindow(QWidget *parent = nullptr);
    void setTab(int index);   // 0 General / 1 Connections / 2 Proxy/Socks / 3 Credentials

private:
    void buildGeneralTab();
    void buildConnectionsTab();
    void buildProxyTab();
    void buildCredentialsTab();
    void load();
    void save();

    // Proxy/Socks tab — one Address/Port/User/Password row per protocol block,
    // laid out like the original (header labels above each edit row).
    struct ProxyRow {
        QLineEdit *host = nullptr;
        QLineEdit *port = nullptr;
        QLineEdit *user = nullptr;
        QLineEdit *pass = nullptr;
    };
    QWidget *buildProxyRowHeader(QWidget *page);
    QWidget *buildProxyRowEdits(QWidget *page, ProxyRow *out);
    void setRowEnabled(const ProxyRow &row, bool on);
    void refreshProxyTab();

    QRadioButton *m_pxNone = nullptr;
    QRadioButton *m_pxHttp = nullptr;
    QRadioButton *m_pxSocks = nullptr;
    QRadioButton *m_socksV5 = nullptr;
    QRadioButton *m_socksV4 = nullptr;
    ProxyRow m_httpRow;             // main block, driven by the radio group
    QCheckBox *m_httpsActive = nullptr;
    ProxyRow m_httpsRow;
    QCheckBox *m_ftpActive = nullptr;
    ProxyRow m_ftpRow;

    QLineEdit *m_dir = nullptr;
    QCheckBox *m_categoryFolders = nullptr;
    QCheckBox *m_completionDialog = nullptr;
    QCheckBox *m_autoStart = nullptr;

    QComboBox *m_maxConn = nullptr;
    QRadioButton *m_allAtOnce = nullptr;
    QRadioButton *m_oneByOne = nullptr;
    QLineEdit *m_bandwidth = nullptr;

    QTableWidget *m_creds = nullptr;
    QTabWidget *m_tabs = nullptr;
};

} // namespace neat
