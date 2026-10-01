#pragma once
#include <QWidget>
#include <vector>

namespace neat {

// Segment-visualizing progress bar, replica of the original NeatSegmentsProgressBar:
// the track represents the whole file; each segment is a block whose fill shows
// downloaded bytes. HLS mode renders fixed TS-segment cells instead.
class SegmentsProgressBar : public QWidget {
    Q_OBJECT
public:
    struct Segment {
        qint64 start = 0;
        qint64 end = 0;
        qint64 downloaded = 0;
        bool active = false;   // has a live connection
    };

    explicit SegmentsProgressBar(QWidget *parent = nullptr);

    void setSegments(const std::vector<Segment> &segs);
    void setHlsSegments(int count, int completed);
    void setHlsMode(bool hls);
    QSize minimumSizeHint() const override { return {120, 12}; }

protected:
    void paintEvent(QPaintEvent *) override;

private:
    std::vector<Segment> m_segments;
    int m_hlsCount = 0;
    int m_hlsDone = 0;
    bool m_hls = false;
};

} // namespace neat
