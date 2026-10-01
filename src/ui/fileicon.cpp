#include "fileicon.h"

#include <QFileInfo>
#include <QHash>

namespace neat {

namespace {

struct IconRule {
    const char *exts;      // space separated, lowercase
    const char *iconName;  // freedesktop mimetype icon name
};

const IconRule kRules[] = {
    { "pdf", "application-pdf" },
    { "doc", "application-msword" },
    { "docx", "application-vnd.openxmlformats-officedocument.wordprocessingml.document" },
    { "odt", "application-vnd.oasis.opendocument.text" },
    { "rtf", "application-msword" },
    { "xls", "application-vnd.ms-excel" },
    { "xlsx", "application-vnd.openxmlformats-officedocument.spreadsheetml.sheet" },
    { "ods", "application-vnd.oasis.opendocument.spreadsheet" },
    { "csv", "application-vnd.ms-excel" },
    { "ppt", "application-vnd.ms-powerpoint" },
    { "pptx", "application-vnd.openxmlformats-officedocument.presentationml.presentation" },
    { "odp", "application-vnd.oasis.opendocument.presentation" },
    { "mp4 mkv avi mov wmv flv webm ts m4v mpg mpeg rmvb vob 3gp m2ts", "video-x-generic" },
    { "mp3 flac wav ogg m4a ape wma aac opus aiff", "audio-x-generic" },
    { "png", "image-png" },
    { "jpg jpeg jpe", "image-jpeg" },
    { "gif", "image-gif" },
    { "bmp", "image-bmp" },
    { "svg", "image-svg+xml" },
    { "tif tiff", "image-tiff" },
    { "jpg2 jp2 heic webp raw cr2 nef", "image-x-generic" },
    { "zip", "application-zip" },
    { "rar 7z tar gz bz2 xz zst cab lz4", "application-x-archive" },
    { "iso img", "application-x-cd-image" },
    { "exe msi bat com", "application-x-executable" },
    { "deb rpm pkg apk appimage", "package-x-generic" },
    { "torrent", "application-x-bittorrent" },
    { "html htm", "text-html" },
    { "py", "text-x-python" },
    { "cs", "text-x-csharp" },
    { "sh js ts cpp c h hpp java rb go rs php css json xml yml yaml ini conf", "text-x-script" },
    { "txt log md nfo", "text-x-generic" },
};

QHash<QString, QString> buildExtMap()
{
    QHash<QString, QString> m;
    for (const IconRule &r : kRules) {
        const QString exts = QString::fromLatin1(r.exts);
        for (const QString &e : exts.split(QLatin1Char(' ')))
            m.insert(e, QString::fromLatin1(r.iconName));
    }
    return m;
}

QIcon themed(const QString &name)
{
    // bundled-first: every target machine gets the same icons regardless of
    // which icon themes the distro ships (deliberately theme-independent)
    const QIcon bundled(QStringLiteral(":/icons/ft/") + name + QStringLiteral(".png"));
    if (!bundled.isNull())
        return bundled;
    return QIcon::fromTheme(name);
}

} // namespace

QIcon fileIconForName(const QString &fileName)
{
    static const QHash<QString, QString> extMap = buildExtMap();

    const QString ext = QFileInfo(fileName).suffix().toLower();
    const QString name = extMap.value(ext);
    if (!name.isEmpty()) {
        const QIcon ic = themed(name);
        if (!ic.isNull())
            return ic;
        // degrade to the category generic before falling out
        if (name.startsWith(QLatin1String("image-")))
            return themed(QStringLiteral("image-x-generic"));
    }
    return themed(QStringLiteral("text-x-generic"));
}

} // namespace neat
