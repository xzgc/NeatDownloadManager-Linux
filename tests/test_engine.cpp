#include "core/httpdownloader.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QFile>
#include <QTimer>
#include <QtGlobal>

#include <cstdio>
#include <cstring>

using namespace neat;

static QByteArray g_digest;
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
    const QString url = app.arguments().value(1);
    const QByteArray expected = QByteArray::fromHex(app.arguments().value(2).toUtf8());
    if (url.isEmpty() || expected.isEmpty()) {
        std::printf("usage: neatdm-tests <url> <md5-hex>\n");
        return 2;
    }

    auto *dl = new HttpDownloader();
    const QString part = QStringLiteral("/tmp/neatdm-test/part.bin");
    QFile::remove(part);

    bool pausedOnce = false, resumed = false;
    QTimer resumer(&app);

    QObject::connect(dl, &HttpDownloader::progress, &app, [&](qint64 done, qint64, qint64) {
        // pause mid-flight once, then resume — exercises the Range path
        if (!pausedOnce && done > 200 * 1024) {
            pausedOnce = true;
            dl->pause();
            QTimer::singleShot(300, &app, [&] {
                resumed = true;
                dl->resume();
            });
        }
    });
    QObject::connect(dl, &HttpDownloader::finished, &app, [&](bool ok, const QString &err) {
        check(ok, "download finished ok");
        if (!err.isEmpty())
            std::printf("  err: %s\n", qPrintable(err));
        check(pausedOnce, "paused once during transfer");
        check(resumed, "resumed after pause");
        check(dl->resumable(), "server advertised Accept-Ranges");
        QFile f(part);
        f.open(QIODevice::ReadOnly);
        const QByteArray got = QCryptographicHash::hash(f.readAll(), QCryptographicHash::Md5);
        check(got == expected, "md5 after pause+resume matches");
        qApp->exit(g_failures == 0 ? 0 : 1);
    });

    QTimer::singleShot(0, &app, [&] { dl->start(QUrl(url), part); });
    QTimer::singleShot(30000, &app, [&] {
        check(false, "timeout");
        qApp->exit(1);
    });
    return app.exec();
}
