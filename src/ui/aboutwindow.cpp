#include "aboutwindow.h"

#include "core/httpdownloader.h"

#include <QDesktopServices>
#include <QFile>
#include <QRegularExpression>
#include <QFormLayout>
#include <QLabel>
#include <QPixmap>
#include <QPushButton>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QVBoxLayout>

namespace neat {

static const char *kOurMarker = "LinuxVersion0100";   // our release marker
static const char *kVersionUrl = "http://www.neatdownloadmanager.com/WinVersion.txt";

AboutWindow::AboutWindow(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("About NeatDownloadManager"));
    setMinimumWidth(420);

    auto *root = new QVBoxLayout(this);

    auto *icon = new QLabel(this);
    icon->setPixmap(QIcon(QStringLiteral(":/icons/appicon.png")).pixmap(64, 64));
    icon->setAlignment(Qt::AlignCenter);
    root->addWidget(icon);

    auto *name = new QLabel(tr("Neat Download Manager 0.1.0 (Linux replica)"), this);
    name->setAlignment(Qt::AlignCenter);
    root->addWidget(name);
    auto *replica = new QLabel(
        tr("The official NeatDownloadManager has no Linux version. This "
           "application is an independent open-source Linux edition built "
           "by replicating its features and user interface 1:1."), this);
    replica->setAlignment(Qt::AlignCenter);
    replica->setWordWrap(true);
    root->addWidget(replica);
    root->addSpacing(8);
    auto *linuxAuthor = new QLabel(
        tr("Linux Edition Copyright & Author : 连晋  over.arse@gmail.com"), this);
    linuxAuthor->setAlignment(Qt::AlignCenter);
    root->addWidget(linuxAuthor);
    auto *commercial = new QLabel(
        tr("Commercial use requires the author's prior written consent."), this);
    commercial->setAlignment(Qt::AlignCenter);
    commercial->setStyleSheet(QStringLiteral("color:#6B7280;"));
    root->addWidget(commercial);

    m_status = new QLabel(QString(), this);
    m_status->setAlignment(Qt::AlignCenter);
    root->addWidget(m_status);

    m_check = new QPushButton(tr("Check For Update"), this);
    auto *ok = new QPushButton(tr("OK"), this);
    auto *row = new QHBoxLayout;
    row->addStretch(1);
    row->addWidget(m_check);
    row->addWidget(ok);
    root->addLayout(row);

    connect(ok, &QPushButton::clicked, this, &QDialog::close);
    connect(m_check, &QPushButton::clicked, this, [this] {
        if (m_canDownload) {
            // like the original: hand the installer download to our own engine
            QDesktopServices::openUrl(QUrl(QStringLiteral(
                "http://www.neatdownloadmanager.com/index.php")));
            return;
        }
        checkForUpdate();
    });
}

void AboutWindow::checkForUpdate()
{
    m_status->setText(tr("Checking For Update..."));
    m_check->setEnabled(false);

    const QString part = QStandardPaths::writableLocation(QStandardPaths::TempLocation)
                         + QStringLiteral("/neatdm-version.txt.part");
    QFile::remove(part);
    auto *dl = new HttpDownloader(this);
    connect(dl, &HttpDownloader::finished, this, [this, part](bool ok, const QString &) {
        m_check->setEnabled(true);
        QString text;
        if (ok) {
            QFile f(part);
            if (f.open(QIODevice::ReadOnly))
                text = QString::fromUtf8(f.readAll());
            QFile::remove(part);
        }
        if (!ok || text.isEmpty()) {
            m_status->setText(tr("Unable to Check For Update , Please Try Again Later"));
            return;
        }
        // vendor marker e.g. WinVersion1424; newer than our marker -> update
        qint64 theirs = 0, ours = 0;
        QRegularExpression rx(QStringLiteral("Version(\\d+)"));
        const auto m1 = rx.match(text);
        const auto m2 = rx.match(QString::fromLatin1(kOurMarker));
        if (m1.hasMatch())
            theirs = m1.captured(1).toLongLong();
        if (m2.hasMatch())
            ours = m2.captured(1).toLongLong();
        if (theirs > ours) {
            m_status->setText(tr("NOT Up To Date, Click Above Button to get the New Version"));
            m_check->setText(tr("Download New Version"));
            m_canDownload = true;
        } else {
            m_status->setText(tr("Application is Up To Date."));
        }
    });
    dl->start(QUrl(QString::fromLatin1(kVersionUrl)), part);
}

} // namespace neat
