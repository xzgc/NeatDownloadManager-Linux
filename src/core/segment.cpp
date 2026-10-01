#include "segment.h"

#include <QFile>
#include <QtGlobal>

#include <cstring>

namespace neat {

static const char kMagic[8] = { 'N', 'D', 'M', 'S', 'E', 'G', 'V', '1' };

void SegmentTable::initialize(qint64 total, int count)
{
    m_total = total;
    m_segs.clear();
    if (total <= 0 || count <= 0)
        return;
    const int n = int(qMin<qint64>(count, (total + kMinBorrowBytes - 1) / kMinBorrowBytes));
    qint64 pos = 0;
    for (int i = 0; i < n; ++i) {
        Segment s;
        s.start = pos;
        s.end = (i == n - 1) ? total - 1 : ((i + 1) * total / n - 1);
        pos = s.end + 1;
        m_segs.push_back(s);
    }
}

qint64 SegmentTable::doneBytes() const
{
    qint64 d = 0;
    for (const auto &s : m_segs)
        d += s.downloaded;
    return d;
}

int SegmentTable::remainingCount() const
{
    int n = 0;
    for (const auto &s : m_segs)
        if (s.end >= s.start && s.downloaded < s.end - s.start + 1)
            ++n;
    return n;
}

int SegmentTable::borrowTail(int idx)
{
    if (idx < 0 || idx >= int(m_segs.size()))
        return -1;
    Segment &s = m_segs[idx];
    const qint64 len = s.end - s.start + 1;
    const qint64 remain = len - s.downloaded;
    if (remain < 2 * kMinBorrowBytes)
        return -1;
    const qint64 tailStart = s.start + s.downloaded + remain / 2;
    Segment tail;
    tail.start = tailStart;
    tail.end = s.end;
    s.end = tailStart - 1;   // downloaded bytes all lie before the split, nothing to roll back
    m_segs.push_back(tail);
    return int(m_segs.size()) - 1;
}

int SegmentTable::slowestIndex(bool activeOnly) const
{
    int best = -1;
    qint64 bestRemain = 0;
    for (int i = 0; i < int(m_segs.size()); ++i) {
        const Segment &s = m_segs[i];
        if (activeOnly && !s.active)
            continue;
        const qint64 remain = (s.end - s.start + 1) - s.downloaded;
        if (remain > bestRemain) {
            bestRemain = remain;
            best = i;
        }
    }
    return best;
}

int SegmentTable::largestRemainingIndex() const
{
    return slowestIndex(false);
}

void SegmentTable::reflow(int targetCount)
{
    // Only used when growing connection count beyond existing segments: split
    // the largest remaining spans until we have enough candidates.
    while (remainingCount() < targetCount) {
        const int idx = largestRemainingIndex();
        if (idx < 0 || borrowTail(idx) < 0)
            break;
    }
}

bool SegmentTable::save(const QString &path) const
{
    QFile::remove(path + QStringLiteral(".bak"));
    QFile::copy(path, path + QStringLiteral(".bak"));

    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;
    unsigned char hdr[24];
    std::memcpy(hdr, kMagic, 8);
    qint64 total = m_total;
    qint64 count = m_segs.size();
    std::memcpy(hdr + 8, &total, 8);
    std::memcpy(hdr + 16, &count, 8);
    if (f.write(reinterpret_cast<char *>(hdr), 24) != 24)
        return false;
    for (const Segment &s : m_segs) {
        char rec[24];
        std::memcpy(rec, &s.start, 8);
        std::memcpy(rec + 8, &s.end, 8);
        std::memcpy(rec + 16, &s.downloaded, 8);
        if (f.write(rec, 24) != 24)
            return false;
    }
    return true;
}

bool SegmentTable::load(const QString &path, qint64 totalHint)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return false;
    char hdr[24];
    if (f.read(hdr, 24) != 24 || std::memcmp(hdr, kMagic, 8) != 0)
        return false;
    qint64 total, count;
    std::memcpy(&total, hdr + 8, 8);
    std::memcpy(&count, hdr + 16, 8);
    if (total <= 0 || total != totalHint || count <= 0 || count > 4096)
        return false;
    std::vector<Segment> segs;
    segs.reserve(int(count));
    for (qint64 i = 0; i < count; ++i) {
        char rec[24];
        if (f.read(rec, 24) != 24)
            return false;
        Segment s;
        std::memcpy(&s.start, rec, 8);
        std::memcpy(&s.end, rec + 8, 8);
        std::memcpy(&s.downloaded, rec + 16, 8);
        if (s.start < 0 || s.end < s.start || s.end >= total)
            return false;
        if (s.downloaded < 0 || s.downloaded > s.end - s.start + 1)
            return false;
        segs.push_back(s);
    }
    m_total = total;
    m_segs = std::move(segs);
    return true;
}

} // namespace neat
