#include "auth.h"

#include <QCryptographicHash>
#include <QUuid>

namespace neat {
namespace Auth {

Challenge parseChallenge(const QString &wwwAuthenticate)
{
    Challenge c;
    const int space = wwwAuthenticate.indexOf(QLatin1Char(' '));
    c.scheme = space < 0 ? wwwAuthenticate.trimmed() : wwwAuthenticate.left(space).trimmed();

    // parse key=value pairs; values may be quoted and contain commas rarely —
    // split on commas outside quotes
    const QString rest = space < 0 ? QString() : wwwAuthenticate.mid(space + 1);
    QString cur;
    bool inQuotes = false;
    const auto flush = [&c](const QString &kv) {
        const int eq = kv.indexOf(QLatin1Char('='));
        if (eq <= 0)
            return;
        const QString k = kv.left(eq).trimmed();
        QString v = kv.mid(eq + 1).trimmed();
        if (v.startsWith(QLatin1Char('"')) && v.endsWith(QLatin1Char('"')) && v.size() >= 2)
            v = v.mid(1, v.size() - 2);
        c.params[k.toLower()] = v;
    };
    for (const QChar &ch : rest) {
        if (ch == QLatin1Char('"')) {
            inQuotes = !inQuotes;
            cur += ch;
        } else if (ch == QLatin1Char(',') && !inQuotes) {
            flush(cur);
            cur.clear();
        } else {
            cur += ch;
        }
    }
    if (!cur.trimmed().isEmpty())
        flush(cur);
    return c;
}

static QString md5hex(const QString &s)
{
    return QString::fromLatin1(
        QCryptographicHash::hash(s.toUtf8(), QCryptographicHash::Md5).toHex());
}

QString buildHeader(const Challenge &ch, const QUrl &url, const QString &method,
                    const QString &user, const QString &password)
{
    if (ch.isBasic()) {
        const QByteArray raw = (user + QLatin1Char(':') + password).toUtf8();
        return QStringLiteral("Basic ") + QString::fromLatin1(raw.toBase64());
    }
    if (ch.isDigest()) {
        QString path = url.path();
        if (path.isEmpty())
            path = QStringLiteral("/");
        if (url.hasQuery())
            path += QLatin1Char('?') + url.query();
        const QString realm = ch.params.value(QStringLiteral("realm"));
        const QString nonce = ch.params.value(QStringLiteral("nonce"));
        const QString qop = ch.params.value(QStringLiteral("qop"));
        const QString opaque = ch.params.value(QStringLiteral("opaque"));
        const QString cnonce = QUuid::createUuid().toString(QUuid::Id128).left(16);
        const QString nc = QStringLiteral("00000001");

        const QString ha1 = md5hex(user + QLatin1Char(':') + realm + QLatin1Char(':') + password);
        const QString ha2 = md5hex(method + QLatin1Char(':') + path);
        QString response;
        if (qop.contains(QLatin1String("auth"), Qt::CaseInsensitive))
            response = md5hex(ha1 + QLatin1Char(':') + nonce + QLatin1Char(':') + nc
                              + QLatin1Char(':') + cnonce + QStringLiteral(":auth:") + ha2);
        else
            response = md5hex(ha1 + QLatin1Char(':') + nonce + QLatin1Char(':') + ha2);

        QString h = QStringLiteral("Digest username=\"%1\", realm=\"%2\", nonce=\"%3\", "
                                   "uri=\"%4\", response=\"%5\"")
                        .arg(user, realm, nonce, path, response);
        if (qop.contains(QLatin1String("auth"), Qt::CaseInsensitive))
            h += QStringLiteral(", qop=auth, nc=%1, cnonce=\"%2\"").arg(nc, cnonce);
        if (!opaque.isEmpty())
            h += QStringLiteral(", opaque=\"%1\"").arg(opaque);
        h += QStringLiteral(", algorithm=MD5");
        return h;
    }
    return QString();
}

} // namespace Auth
} // namespace neat
