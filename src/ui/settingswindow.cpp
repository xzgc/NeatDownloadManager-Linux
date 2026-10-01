#include "settingswindow.h"

#include "core/db.h"
#include "core/settings.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFormLayout>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QIntValidator>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QRadioButton>
#include <QStandardPaths>
#include <QTabWidget>
#include <QTableWidget>
#include <QVBoxLayout>

namespace neat {

SettingsWindow::SettingsWindow(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Neat Download Manager Settings"));
    setMinimumSize(560, 420);

    auto *root = new QVBoxLayout(this);
    m_tabs = new QTabWidget(this);
    root->addWidget(m_tabs, 1);
    buildGeneralTab();
    buildConnectionsTab();
    buildProxyTab();
    buildCredentialsTab();

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    buttons->button(QDialogButtonBox::Ok)->setText(tr("OK"));
    buttons->button(QDialogButtonBox::Cancel)->setText(tr("Cancel"));
    connect(buttons, &QDialogButtonBox::accepted, this, [this] {
        save();
        accept();
    });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    root->addWidget(buttons);

    load();
}

void SettingsWindow::setTab(int index)
{
    if (m_tabs && index >= 0 && index < m_tabs->count())
        m_tabs->setCurrentIndex(index);
}

void SettingsWindow::buildGeneralTab()
{
    auto *page = new QWidget(this);
    auto *form = new QFormLayout(page);

    auto *dirRow = new QHBoxLayout;
    m_dir = new QLineEdit(page);
    auto *browse = new QPushButton(tr("Select Folder"), page);
    dirRow->addWidget(m_dir, 1);
    dirRow->addWidget(browse);
    form->addRow(tr("Download Directory"), dirRow);
    connect(browse, &QPushButton::clicked, this, [this] {
        const QString d = QFileDialog::getExistingDirectory(this, tr("Select a Folder for Downloaded Files :"),
                                                            m_dir->text());
        if (!d.isEmpty())
            m_dir->setText(d);
    });

    m_categoryFolders = new QCheckBox(
        tr("Create Category Folders ( e.g.  Video, Document, ...  )"), page);
    form->addRow(QString(), m_categoryFolders);
    m_completionDialog = new QCheckBox(tr("Show Download Completion Dialog"), page);
    form->addRow(QString(), m_completionDialog);
    m_autoStart = new QCheckBox(tr("Start Automatically at Login"), page);
    form->addRow(QString(), m_autoStart);

    m_tabs->addTab(page, tr(" General "));
}

void SettingsWindow::buildConnectionsTab()
{
    auto *page = new QWidget(this);
    auto *form = new QFormLayout(page);

    m_maxConn = new QComboBox(page);
    for (int i = 1; i <= 32; ++i)
        m_maxConn->addItem(QString::number(i));
    form->addRow(tr("Max Connections per Download   ( 8 Connections Recommended )"), m_maxConn);

    m_allAtOnce = new QRadioButton(tr("Create Additional Connections All at Once"), page);
    m_oneByOne = new QRadioButton(tr("Create Additional Connections One by One"), page);
    m_allAtOnce->setChecked(true);
    form->addRow(tr("When first Connection Starts Downloading  , :"), m_allAtOnce);
    form->addRow(QString(), m_oneByOne);

    m_bandwidth = new QLineEdit(page);
    m_bandwidth->setPlaceholderText(tr("0 or blank for No limit"));
    form->addRow(tr("Bandwidth Limit per Download (KB/s)"), m_bandwidth);

    m_tabs->addTab(page, tr(" Connections "));
}

void SettingsWindow::buildProxyTab()
{
    // 1:1 replica of the original Proxy/Socks page: one radio cascade
    // (No Proxy/Socks / HTTP Proxy / Socks Proxy + V5/V4) that owns the main
    // Address:Port User Password row, then optional per-protocol overrides
    // for https and ftp, each with its own row under its own headers.
    auto *page = new QWidget(this);
    auto *grid = new QGridLayout(page);
    grid->setContentsMargins(25, 12, 25, 12);
    grid->setVerticalSpacing(6);

    m_pxNone = new QRadioButton(tr("No Proxy/Socks"), page);
    m_pxHttp = new QRadioButton(tr("HTTP Proxy"), page);
    m_pxSocks = new QRadioButton(tr("Socks Proxy"), page);

    auto *socksVersionRow = new QWidget(page);
    auto *svLay = new QHBoxLayout(socksVersionRow);
    svLay->setContentsMargins(21, 0, 0, 0);
    svLay->setSpacing(18);
    m_socksV5 = new QRadioButton(tr("Socks V5"), socksVersionRow);
    m_socksV4 = new QRadioButton(tr("Socks V4"), socksVersionRow);
    svLay->addWidget(m_socksV5);
    svLay->addWidget(m_socksV4);
    svLay->addStretch(1);

    grid->addWidget(m_pxNone, 0, 0);
    grid->addWidget(m_pxHttp, 1, 0);
    grid->addWidget(buildProxyRowHeader(page), 0, 1, Qt::AlignBottom);
    grid->addWidget(m_pxSocks, 2, 0);
    grid->addWidget(buildProxyRowEdits(page, &m_httpRow), 2, 1);
    grid->addWidget(socksVersionRow, 3, 0);
    grid->setColumnStretch(1, 1);

    m_httpsActive = new QCheckBox(tr("https Protocol"), page);
    grid->addWidget(buildProxyRowHeader(page), 4, 1, Qt::AlignBottom);
    grid->addWidget(m_httpsActive, 5, 0);
    grid->addWidget(buildProxyRowEdits(page, &m_httpsRow), 5, 1);

    m_ftpActive = new QCheckBox(tr("ftp Protocol"), page);
    grid->addWidget(buildProxyRowHeader(page), 6, 1, Qt::AlignBottom);
    grid->addWidget(m_ftpActive, 7, 0);
    grid->addWidget(buildProxyRowEdits(page, &m_ftpRow), 7, 1);

    grid->setRowStretch(8, 1);

    for (QRadioButton *rb : {m_pxNone, m_pxHttp, m_pxSocks})
        connect(rb, &QRadioButton::toggled, this, &SettingsWindow::refreshProxyTab);
    connect(m_httpsActive, &QCheckBox::toggled, this, &SettingsWindow::refreshProxyTab);
    connect(m_ftpActive, &QCheckBox::toggled, this, &SettingsWindow::refreshProxyTab);
    refreshProxyTab();

    m_tabs->addTab(page, tr(" Proxy/Socks "));
}

QWidget *SettingsWindow::buildProxyRowHeader(QWidget *page)
{
    // "Address    :   Port    User     Password" like the original column
    // header labels that sit above every editable proxy row.
    auto *w = new QWidget(page);
    auto *lay = new QHBoxLayout(w);
    lay->setContentsMargins(0, 0, 0, 0);
    auto *portLabel = new QLabel(tr("Port"), w);
    portLabel->setFixedWidth(60);   // match the port editor column below
    lay->addWidget(new QLabel(tr("Address"), w), 1, Qt::AlignLeft);
    lay->addWidget(new QLabel(QStringLiteral(":"), w));
    lay->addWidget(portLabel, 0, Qt::AlignLeft);
    lay->addWidget(new QLabel(tr("User"), w), 1, Qt::AlignLeft);
    lay->addWidget(new QLabel(tr("Password"), w), 1, Qt::AlignLeft);
    return w;
}

QWidget *SettingsWindow::buildProxyRowEdits(QWidget *page, ProxyRow *out)
{
    auto *w = new QWidget(page);
    auto *lay = new QHBoxLayout(w);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(6);
    out->host = new QLineEdit(w);
    out->port = new QLineEdit(w);
    out->port->setFixedWidth(48);
    out->port->setValidator(new QIntValidator(1, 65535, out->port));
    out->user = new QLineEdit(w);
    out->pass = new QLineEdit(w);
    out->pass->setEchoMode(QLineEdit::Password);
    lay->addWidget(out->host, 1);
    lay->addWidget(new QLabel(QStringLiteral(":"), w));
    lay->addWidget(out->port);
    lay->addWidget(out->user, 1);
    lay->addWidget(out->pass, 1);
    return w;
}

void SettingsWindow::setRowEnabled(const ProxyRow &row, bool on)
{
    row.host->setEnabled(on);
    row.port->setEnabled(on);
    row.user->setEnabled(on);
    row.pass->setEnabled(on);
}

void SettingsWindow::refreshProxyTab()
{
    const bool mainOn = m_pxHttp->isChecked() || m_pxSocks->isChecked();
    setRowEnabled(m_httpRow, mainOn);
    m_socksV5->setEnabled(m_pxSocks->isChecked());
    m_socksV4->setEnabled(m_pxSocks->isChecked());
    setRowEnabled(m_httpsRow, m_httpsActive->isChecked());
    setRowEnabled(m_ftpRow, m_ftpActive->isChecked());
}

void SettingsWindow::buildCredentialsTab()
{
    auto *page = new QWidget(this);
    auto *lay = new QVBoxLayout(page);
    m_creds = new QTableWidget(0, 4, page);
    m_creds->setHorizontalHeaderLabels({tr("Target"), tr("Protocol"), tr("User"), tr("Password")});
    m_creds->horizontalHeader()->setStretchLastSection(true);
    m_creds->verticalHeader()->hide();
    lay->addWidget(m_creds);
    auto *row = new QHBoxLayout;
    auto *add = new QPushButton(tr("Add"), page);
    auto *remove = new QPushButton(tr("Remove"), page);
    row->addWidget(add);
    row->addWidget(remove);
    row->addStretch(1);
    lay->addLayout(row);
    connect(add, &QPushButton::clicked, this, [this] {
        m_creds->insertRow(m_creds->rowCount());
        for (int c = 0; c < 4; ++c)
            m_creds->setItem(m_creds->rowCount() - 1, c, new QTableWidgetItem(QString()));
    });
    connect(remove, &QPushButton::clicked, this, [this] {
        const int r = m_creds->currentRow();
        if (r >= 0)
            m_creds->removeRow(r);
    });
    m_tabs->addTab(page, tr(" Site-Credentials "));
}

void SettingsWindow::load()
{
    const Settings &s = Settings::instance();
    m_dir->setText(s.downloadDirectory());
    m_categoryFolders->setChecked(s.categoryFolders());
    m_completionDialog->setChecked(s.completionDialog());
    m_autoStart->setChecked(s.autoStart());
    m_maxConn->setCurrentIndex(qBound(0, s.maxConnections() - 1, 31));
    if (s.connectionsAllAtOnce())
        m_allAtOnce->setChecked(true);
    else
        m_oneByOne->setChecked(true);
    m_bandwidth->setText(s.bandwidthLimitKb() > 0 ? QString::number(s.bandwidthLimitKb())
                                                  : QString());

    const QString httpType = s.proxyType(QStringLiteral("HTTP"));
    if (s.proxyActive(QStringLiteral("HTTP")) && httpType == QLatin1String("SOCKS-Proxy"))
        m_pxSocks->setChecked(true);
    else if (s.proxyActive(QStringLiteral("HTTP")) && httpType == QLatin1String("HTTP-Proxy"))
        m_pxHttp->setChecked(true);
    else
        m_pxNone->setChecked(true);
    (s.socksVersion() == 4 ? m_socksV4 : m_socksV5)->setChecked(true);
    m_httpRow.host->setText(s.proxyAddress(QStringLiteral("HTTP")));
    m_httpRow.port->setText(s.proxyPort(QStringLiteral("HTTP")) > 0
                                ? QString::number(s.proxyPort(QStringLiteral("HTTP")))
                                : QString());
    m_httpRow.user->setText(s.proxyUser(QStringLiteral("HTTP")));
    m_httpRow.pass->setText(s.proxyPassword(QStringLiteral("HTTP")));
    m_httpsActive->setChecked(s.proxyActive(QStringLiteral("HTTPS")));
    m_httpsRow.host->setText(s.proxyAddress(QStringLiteral("HTTPS")));
    m_httpsRow.port->setText(s.proxyPort(QStringLiteral("HTTPS")) > 0
                                 ? QString::number(s.proxyPort(QStringLiteral("HTTPS")))
                                 : QString());
    m_httpsRow.user->setText(s.proxyUser(QStringLiteral("HTTPS")));
    m_httpsRow.pass->setText(s.proxyPassword(QStringLiteral("HTTPS")));
    m_ftpActive->setChecked(s.proxyActive(QStringLiteral("FTP")));
    m_ftpRow.host->setText(s.proxyAddress(QStringLiteral("FTP")));
    m_ftpRow.port->setText(s.proxyPort(QStringLiteral("FTP")) > 0
                               ? QString::number(s.proxyPort(QStringLiteral("FTP")))
                               : QString());
    m_ftpRow.user->setText(s.proxyUser(QStringLiteral("FTP")));
    m_ftpRow.pass->setText(s.proxyPassword(QStringLiteral("FTP")));
    refreshProxyTab();
}

static void setAutostart(bool enabled)
{
    const QString path = QStandardPaths::writableLocation(QStandardPaths::ConfigLocation)
                         + QStringLiteral("/autostart/neatdm.desktop");
    if (enabled) {
        QDir().mkpath(QFileInfo(path).absolutePath());
        QFile f(path);
        if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            f.write("[Desktop Entry]\nType=Application\nName=NeatDM\n"
                    "Exec=neatdm\nX-GNOME-Autostart-enabled=true\n");
        }
    } else {
        QFile::remove(path);
    }
}

void SettingsWindow::save()
{
    Settings &s = Settings::instance();
    s.setDownloadDirectory(m_dir->text());
    s.setCategoryFolders(m_categoryFolders->isChecked());
    s.setCompletionDialog(m_completionDialog->isChecked());
    s.setAutoStart(m_autoStart->isChecked());
    setAutostart(m_autoStart->isChecked());
    s.setMaxConnections(m_maxConn->currentIndex() + 1);
    s.setConnectionsAllAtOnce(m_allAtOnce->isChecked());
    s.setBandwidthLimitKb(m_bandwidth->text().trimmed().toLongLong());

    const bool mainOn = m_pxHttp->isChecked() || m_pxSocks->isChecked();
    const QString mainType = m_pxSocks->isChecked() ? QStringLiteral("SOCKS-Proxy")
                             : m_pxHttp->isChecked() ? QStringLiteral("HTTP-Proxy")
                                                     : QStringLiteral("No Proxy");
    s.setProxyActive(QStringLiteral("HTTP"), mainOn);
    s.setProxyType(QStringLiteral("HTTP"), mainType);
    s.setSocksVersion(m_socksV4->isChecked() ? 4 : 5);
    s.setProxyAddress(QStringLiteral("HTTP"), m_httpRow.host->text().trimmed());
    s.setProxyPort(QStringLiteral("HTTP"), m_httpRow.port->text().trimmed().toInt());
    s.setProxyUser(QStringLiteral("HTTP"), m_httpRow.user->text());
    s.setProxyPassword(QStringLiteral("HTTP"), m_httpRow.pass->text());

    // https/ftp rows are per-protocol overrides of the same global proxy type
    s.setProxyActive(QStringLiteral("HTTPS"), m_httpsActive->isChecked());
    s.setProxyType(QStringLiteral("HTTPS"),
                   m_httpsActive->isChecked() ? mainType : QStringLiteral("No Proxy"));
    s.setProxyAddress(QStringLiteral("HTTPS"), m_httpsRow.host->text().trimmed());
    s.setProxyPort(QStringLiteral("HTTPS"), m_httpsRow.port->text().trimmed().toInt());
    s.setProxyUser(QStringLiteral("HTTPS"), m_httpsRow.user->text());
    s.setProxyPassword(QStringLiteral("HTTPS"), m_httpsRow.pass->text());

    s.setProxyActive(QStringLiteral("FTP"), m_ftpActive->isChecked());
    s.setProxyType(QStringLiteral("FTP"),
                   m_ftpActive->isChecked() ? mainType : QStringLiteral("No Proxy"));
    s.setProxyAddress(QStringLiteral("FTP"), m_ftpRow.host->text().trimmed());
    s.setProxyPort(QStringLiteral("FTP"), m_ftpRow.port->text().trimmed().toInt());
    s.setProxyUser(QStringLiteral("FTP"), m_ftpRow.user->text());
    s.setProxyPassword(QStringLiteral("FTP"), m_ftpRow.pass->text());
}

} // namespace neat
