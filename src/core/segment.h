#pragma once
#include <QString>
#include <QStringList>
#include <vector>

namespace neat {

// One byte-range of the file: [start, end] inclusive, `downloaded` bytes of it
// are on disk starting at `start` (contiguous from the front).
struct Segment {
    qint64 start = 0;
    qint64 end = -1;
    qint64 downloaded = 0;
    bool active = false;   // has a live connection assigned
};

// Segment bookkeeping + persistence (our segments.bin equivalent).
// Layout on disk (little endian): magic "NDMSEGV1", total, count,
// then per segment {start, end, downloaded}.
class SegmentTable {
public:
    static constexpr qint64 kMinBorrowBytes = 256 * 1024;   // don't split tails smaller than this

    void initialize(qint64 total, int count);
    bool load(const QString &path, qint64 totalHint);
    bool save(const QString &path) const;

    const std::vector<Segment> &segments() const { return m_segs; }
    std::vector<Segment> &segments() { return m_segs; }

    qint64 total() const { return m_total; }
    qint64 doneBytes() const;
    int remainingCount() const;

    // Split the tail half off `idx` as a new segment; returns its index, -1 if
    // the remaining range is too small to be worth another connection.
    int borrowTail(int idx);

    // Segment with the most undownloaded bytes among active ones, or overall.
    int slowestIndex(bool activeOnly) const;
    int largestRemainingIndex() const;

    // Drop fully-downloaded granularity: compact segments so `count` segments
    // evenly cover the remaining undownloaded spans (used when connections
    // increase beyond current segments).
    void reflow(int targetCount);

private:
    std::vector<Segment> m_segs;
    qint64 m_total = 0;
};

} // namespace neat
