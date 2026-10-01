#include "core/settings.h"
#include "core/singleinstance.h"
#include "ui/mainwindow.h"
#include "ui/aboutwindow.h"
#include "ui/settingswindow.h"

#include <QApplication>
#include <QFile>
#include <QCommandLineParser>
#include <QTimer>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("NeatDM"));
    QApplication::setOrganizationName(QStringLiteral("neatdm"));
    QApplication::setWindowIcon(QIcon(QStringLiteral(":/icons/appicon.png")));

    // app-wide modern flat theme (light + brand green), single source in qrc
    {
        QFile f(QStringLiteral(":/icons/theme.qss"));
        if (f.open(QIODevice::ReadOnly | QIODevice::Text))
            app.setStyleSheet(QString::fromUtf8(f.readAll()));
    }

    QCommandLineParser cli;
    cli.setApplicationDescription(QStringLiteral("NeatDM - download manager"));
    QCommandLineOption selftest(QStringLiteral("selftest"),
                                QStringLiteral("run smoke test and exit"));
    QCommandLineOption lifetime(QStringLiteral("lifetime"),
                                QStringLiteral("auto-exit after N ms (selftest)"), QStringLiteral("ms"));
    QCommandLineOption uiSmoke(QStringLiteral("selftest-ui"),
                                QStringLiteral("construct all windows offscreen and exit"));
    QCommandLineOption waitDl(QStringLiteral("wait-downloads"),
                              QStringLiteral("exit when all downloads finished"));
    QCommandLineOption limitKb(QStringLiteral("limit-kbps"),
                               QStringLiteral("default bandwidth limit KB/s"), QStringLiteral("kb"));
    cli.addOption(selftest);
    cli.addOption(lifetime);
    cli.addOption(waitDl);
    cli.addOption(uiSmoke);
    cli.addOption(limitKb);
    QCommandLineOption openSet(QStringLiteral("open-settings"),
                               QStringLiteral("open the settings window on startup"));
    QCommandLineOption openSetTab(QStringLiteral("settings-tab"),
                                  QStringLiteral("settings tab index to show (0-3)"), QStringLiteral("idx"));
    cli.addOption(openSet);
    cli.addOption(openSetTab);
    QCommandLineOption openAbout(QStringLiteral("open-about"),
                                 QStringLiteral("open the about window on startup"));
    cli.addOption(openAbout);
    QCommandLineOption expandTree(QStringLiteral("expand-tree"),
                                  QStringLiteral("expand the first category node (screenshots)"));
    cli.addOption(expandTree);
    cli.addPositionalArgument(QStringLiteral("url"), QStringLiteral("URL to download"));
    cli.process(app);

    neat::SingleInstance guard;
    if (!guard.tryLock()) {
        qInfo("another instance is running, requested raise");
        return 0;
    }

    neat::MainWindow w;
    w.show();
    QObject::connect(&guard, &neat::SingleInstance::raiseRequested, &w,
                     &neat::MainWindow::showFromTray);

    if (cli.isSet(limitKb))
        w.setDefaultBandwidthKb(cli.value(limitKb).toLongLong());

    bool anyFailed = false;
    if (!cli.positionalArguments().isEmpty()) {
        for (const QString &url : cli.positionalArguments())
            w.startDownload(url);
    }

    if (cli.isSet(openSet)) {
        auto *sw = new neat::SettingsWindow(&w);
        sw->setAttribute(Qt::WA_DeleteOnClose);
        sw->setTab(cli.value(QStringLiteral("settings-tab")).toInt());
        sw->show();
    }

    if (cli.isSet(expandTree))
        w.expandFirstCategory();

    if (cli.isSet(openAbout)) {
        auto *aw = new neat::AboutWindow(&w);
        aw->setAttribute(Qt::WA_DeleteOnClose);
        aw->show();
    }

    if (cli.isSet(uiSmoke)) {
        const bool ok = w.runUiSmoke();
        QTimer::singleShot(200, &app, [ok] { QCoreApplication::exit(ok ? 0 : 5); });
    }

    if (cli.isSet(selftest) || cli.isSet(waitDl)) {
        auto &s = neat::Settings::instance();
        s.setMaxConnections(32);
        const bool ok = s.maxConnections() == 32
            && s.downloadDirectory().contains(QLatin1String("/"))
            && s.userAgent() == neat::Settings::DefaultUserAgent;
        qInfo("selftest settings roundtrip: %s", ok ? "OK" : "FAIL");

        // exit when no active downloads remain (or lifetime timer fires)
        auto *poll = new QTimer(&app);
        QObject::connect(poll, &QTimer::timeout, &app, [&] {
            if (w.activeDownloads() == 0)
                QCoreApplication::exit(w.anyFailed() ? 3 : 0);
        });
        poll->start(500);
        const int ms = cli.value(lifetime).toInt();
        if (ms > 0)
            QTimer::singleShot(ms, &app, [&] { QCoreApplication::exit(4); });
    }

    const int rc = app.exec();
    if (cli.isSet(waitDl))
        qInfo("wait-downloads exit rc=%d", rc);
    return rc;
}
