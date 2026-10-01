#include "extprotocol.h"

namespace neat {

ExtDownloadRequest ExtProtocol::parse(const QString &message)
{
    ExtDownloadRequest r;
    const QStringList lines = message.split(QLatin1String("\r\n"));
    for (const QString &lineRaw : lines) {
        const QString line = lineRaw;
        if (line.isEmpty())
            continue;

        const int colon = line.indexOf(QLatin1Char(':'));
        if (colon < 1)
            continue;
        const QString name = line.left(colon).trimmed();
        const QString value = line.mid(colon + 1).trimmed();

        bool numeric = false;
        const int field = name.toInt(&numeric);

        if (numeric) {
            switch (field) {
            case 1: r.method = value.toUpper(); break;
            case 2: r.url = value; break;
            case 3: r.filename = value; break;
            case 4: r.pageTitle = value; break;
            case 5: r.pageUrl = value; break;
            case 6: r.ltype = value.toLower(); break;
            case 7: r.filesize = value.toLongLong(); break;
            case 8: r.mimetype = value; break;
            case 9: r.label = value; break;
            default: break;
            }
            continue;
        }

        if (name.compare(QLatin1String("Cookie"), Qt::CaseInsensitive) == 0) {
            r.cookies = value;
        } else if (name.compare(QLatin1String("Origin"), Qt::CaseInsensitive) == 0) {
            r.origin = value;
        } else if (name.compare(QLatin1String("Referer"), Qt::CaseInsensitive) == 0) {
            r.referer = value;
        } else if (name.compare(QLatin1String("Content-Type"), Qt::CaseInsensitive) == 0) {
            r.contentType = value;
        } else if (name.compare(QLatin1String("Content-Disposition"), Qt::CaseInsensitive) == 0) {
            r.contentDisposition = value;
        } else if (name.compare(QLatin1String("Content-Length"), Qt::CaseInsensitive) == 0) {
            // informational
        } else if (name.startsWith(QLatin1String("__0NeatPostData9__"), Qt::CaseSensitive)) {
            r.postData = line.mid(line.indexOf(QLatin1Char(':')) + 1).toUtf8();
        } else if (name.startsWith(QLatin1String("x-"), Qt::CaseInsensitive)) {
            r.extraHeaders << line;
        }
    }
    return r;
}

} // namespace neat
