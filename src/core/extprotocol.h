#pragma once
#include <QString>
#include <QStringList>

namespace neat {

// A download request received from the browser extension, parsed from the
// line-based protocol (see docs/01 §3.2): "N:value" lines plus plain header
// lines (Cookie/Referer/Origin/Content-Type/... and x-* passthrough).
struct ExtDownloadRequest {
    QString method = QStringLiteral("GET");   // 1:
    QString url;                              // 2:
    QString filename;                         // 3: (optional)
    QString pageTitle;                        // 4:
    QString pageUrl;                          // 5:
    QString ltype = QStringLiteral("normal"); // 6: normal | media | hls
    qint64 filesize = 0;                      // 7:
    QString mimetype;                         // 8:
    QString label;                            // 9:
    QString contentType;                      // Content-Type:
    QString contentDisposition;               // Content-Disposition:
    QString cookies;                          // Cookie:
    QString origin;                           // Origin:
    QString referer;                          // Referer:
    QStringList extraHeaders;                 // x-* lines, verbatim "Name: value"
    QByteArray postData;                      // __0NeatPostData9__:
    QString userAgent;                        // 9: in POST context / custom

    bool isValid() const { return !url.isEmpty(); }
    static constexpr qint64 kMaxMessageBytes = 118784;
};

class ExtProtocol {
public:
    static ExtDownloadRequest parse(const QString &message);
};

} // namespace neat
