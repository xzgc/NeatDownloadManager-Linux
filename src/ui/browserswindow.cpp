#include "browserswindow.h"

#include "core/settings.h"
#include "core/wsserver.h"

#include <QCheckBox>
#include <QDesktopServices>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QStandardPaths>
#include <QUrl>
#include <QVBoxLayout>

namespace neat {

BrowsersWindow::BrowsersWindow(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Browsers"));
    setMinimumWidth(520);

    auto *root = new QVBoxLayout(this);
    buildCard(tr("Google Chrome"), QStringLiteral("google-chrome"),
              QStringLiteral("Chrome"),
              QStringLiteral("https://chrome.google.com/webstore/detail/"
                             "NeatDownloadManager-Extension/cpcifbdmkopohnnofedkjghjiclmhdah"));
    buildCard(tr("Mozilla Firefox"), QStringLiteral("firefox"), QStringLiteral("Fox"),
              QStringLiteral("https://addons.mozilla.org/en-US/firefox/addon/"
                             "neatdownloadmanager-extension/"));
    buildCard(tr("Microsoft Edge"), QStringLiteral("microsoft-edge"), QStringLiteral("Edge"),
              QStringLiteral("https://microsoftedge.microsoft.com/addons/detail/"
                             "neatdownloadmanager-exten/pbghcbaeehloijjcebiflemhcebmlnke"));
    root->addStretch(1);
    auto *close = new QPushButton(tr("Close"), this);
    close->setFixedWidth(70);
    auto *row = new QHBoxLayout;
    row->addStretch(1);
    row->addWidget(close);
    root->addLayout(row);
    connect(close, &QPushButton::clicked, this, &QDialog::close);
}

void BrowsersWindow::buildCard(const QString &name, const QString &exe, const QString &panelKey,
                               const QString &storeUrl)
{
    const bool installed = !QStandardPaths::findExecutable(exe).isEmpty();

    auto *group = new QGroupBox(name, this);
    auto *lay = new QVBoxLayout(group);

    auto *status = new QLabel(installed ? QString() : tr("Not Installed"), group);
    if (!installed)
        status->setStyleSheet(QStringLiteral("color: gray;"));
    lay->addWidget(status);

    auto *chk = new QCheckBox(tr("Show Download Panel on Web Media Players"), group);
    chk->setChecked(Settings::instance().browserPanel(panelKey));
    lay->addWidget(chk);
    connect(chk, &QCheckBox::toggled, this, [panelKey, chk](bool on) {
        Settings::instance().setBrowserPanel(panelKey, on);
        // live-push to every connected extension, like the original
        WsServer::broadcastStatic(QStringLiteral("ShowPanel%1=%2").arg(panelKey, on ? 1 : 0));
    });

    auto *add = new QPushButton(tr("%1 Extension").arg(name.section(QLatin1Char(' '), 0, 0)),
                                group);
    connect(add, &QPushButton::clicked, this,
            [storeUrl] { QDesktopServices::openUrl(QUrl(storeUrl)); });
    lay->addWidget(add, 0, Qt::AlignLeft);

    qobject_cast<QVBoxLayout *>(layout())->addWidget(group);
}

} // namespace neat
