#include "segprogressbar.h"

#include <QPainter>
#include <QPainterPath>

#include <algorithm>

namespace neat {

SegmentsProgressBar::SegmentsProgressBar(QWidget *parent)
    : QWidget(parent)
{
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
}

void SegmentsProgressBar::setSegments(const std::vector<Segment> &segs)
{
    m_segments = segs;
    update();
}

void SegmentsProgressBar::setHlsSegments(int count, int completed)
{
    m_hlsCount = count;
    m_hlsDone = completed;
    update();
}

void SegmentsProgressBar::setHlsMode(bool hls)
{
    m_hls = hls;
    update();
}

void SegmentsProgressBar::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    // modern flat theme (theme.qss): light track + brand green fills.
    // Like the original: every segment is its own clearly separated cell so
    // the whole map and each cell's partial progress read at a glance.
    const QColor track = QColor(0xE9, 0xEB, 0xEE);
    const QColor fill = QColor(0x27, 0xAE, 0x60);
    const QColor activeFill = QColor(0x2E, 0xCC, 0x71);

    const int n = m_hls ? m_hlsCount : int(m_segments.size());
    if (n <= 0)
        return;
    // keep the gaps visible; shrink them only when the bar gets crowded
    const qreal gap = n > 64 ? 1.0 : n > 32 ? 2.0 : 3.0;
    const qreal radius = std::min<qreal>(3.0, height() / 3.0);
    const qreal cellW = (width() - gap * (n - 1)) / n;

    auto paintCell = [&](int i, qreal frac, bool active) {
        const QRectF cell(i * (cellW + gap), 0, cellW, height());
        QPainterPath frame;
        frame.addRoundedRect(cell, radius, radius);
        p.fillPath(frame, track);
        if (frac <= 0)
            return;
        const QRectF inner = cell.adjusted(0, 1, 0, -1);
        QPainterPath barPath;
        barPath.addRect(QRectF(inner.x(), inner.y(),
                               inner.width() * qMin<qreal>(frac, 1.0), inner.height()));
        p.fillPath(frame.intersected(barPath), active ? activeFill : fill);
    };

    if (m_hls) {
        for (int i = 0; i < m_hlsCount; ++i)
            paintCell(i, i < m_hlsDone ? 1.0 : 0.0, false);
        return;
    }

    qint64 total = 0;
    for (const auto &s : m_segments)
        total = std::max(total, s.end);
    if (total <= 0)
        return;
    for (size_t i = 0; i < m_segments.size(); ++i) {
        const Segment &s = m_segments[i];
        const qint64 span = s.end - s.start;
        const qreal frac = span > 0
                               ? qreal(std::min<qint64>(s.downloaded, span)) / span : 0.0;
        paintCell(int(i), frac, s.active);
    }
}

} // namespace neat
