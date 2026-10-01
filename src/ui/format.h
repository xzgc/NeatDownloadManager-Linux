#pragma once
#include <QString>

namespace neat {

// Byte/speed formatting replicas of NeatNsUtils (getFileSizeString / PerSecond).
inline QString formatBytes(qint64 v)
{
    if (v < 1000)
        return QStringLiteral("%1 Byte").arg(v);
    const double kb = v / 1024.0;
    if (kb < 1000)
        return QStringLiteral("%1 KB").arg(kb, 0, 'f', 1);
    const double mb = kb / 1024.0;
    if (mb < 1000)
        return QStringLiteral("%1 MB").arg(mb, 0, 'f', 1);
    return QStringLiteral("%1 GB").arg(mb / 1024.0, 0, 'f', 1);
}

inline QString formatSpeed(qint64 v)
{
    return formatBytes(v) + QStringLiteral("/sec");
}

inline QString formatRemaining(qint64 remainBytes, qint64 bytesPerSec)
{
    if (bytesPerSec <= 0 || remainBytes <= 0)
        return QStringLiteral(" ");
    const qint64 sec = remainBytes / bytesPerSec;
    if (sec < 60)
        return QStringLiteral("%1 sec").arg(sec);
    if (sec < 3600)
        return QStringLiteral("%1 min %2 sec").arg(sec / 60).arg(sec % 60);
    return QStringLiteral("%1 h %2 min").arg(sec / 3600).arg((sec % 3600) / 60);
}

} // namespace neat
