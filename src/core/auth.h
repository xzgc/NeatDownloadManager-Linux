#pragma once
#include <QHash>
#include <QString>
#include <QUrl>

namespace neat {

// WWW-Authenticate challenge handling (replica of NeatAuthBasic/NeatAuthDigest
// roles): parse a challenge, build an Authorization header from credentials.
namespace Auth {

struct Challenge {
    QString scheme;   // Basic | Digest
    QHash<QString, QString> params;   // realm, nonce, qop, opaque, algorithm...

    bool isBasic() const { return scheme.compare(QLatin1String("Basic"), Qt::CaseInsensitive) == 0; }
    bool isDigest() const { return scheme.compare(QLatin1String("Digest"), Qt::CaseInsensitive) == 0; }
};

Challenge parseChallenge(const QString &wwwAuthenticate);

// Build the Authorization header value (no trailing CRLF).
QString buildHeader(const Challenge &ch, const QUrl &url, const QString &method,
                           const QString &user, const QString &password);

} // namespace Auth

} // namespace neat
