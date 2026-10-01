#pragma once
#include <QDialog>
#include <QUrl>
#include <QStringList>
#include <QPair>
#include <QVector>

#include "core/downloadengine.h"

class QLabel;
class QPushButton;
class QProgressBar;
class QComboBox;
class QLineEdit;
class QCheckBox;
class QTableWidget;
class QFormLayout;
class QTabWidget;

namespace neat {

struct DownloadRecord;
class SegmentsProgressBar;

// Replica of NeatDownloadWindow: three tabs (Download / Connections / Options)
// around the segmented engine, Pause-Resume and Cancel buttons.
class DownloadWindow : public QDialog {
    Q_OBJECT
public:
    // fresh download; fileNameOverride/extraHeaders come from the extension
    DownloadWindow(qint64 id, const QUrl &url, const QString &destDir, QWidget *parent = nullptr,
                   const QString &fileNameOverride = QString(),
                   const QStringList &extraHeaders = QStringList(), bool hls = false);
    // resume an incomplete record (part file kept in destDir)
    DownloadWindow(const DownloadRecord &rec, QWidget *parent = nullptr);

    qint64 downloadId() const { return m_id; }
    void retranslateUi();
    void setHeadline(qint64 done, qint64 total, qint64 bps);

    bool isWorking() const;
    DownloadEngine *engine() const { return m_engine; }
    void applyResumeSettings(qint64 bandwidthKb, int connections);
    void stopEngine();
    void resumeEngine();                       // pause + keep window open
    void applyDefaultBandwidth(qint64 kb);

protected:
    void changeEvent(QEvent *e) override;

signals:
    void rowStatusChanged(qint64 id, const QString &statusText, const QString &speedText);
    void rowProgressChanged(qint64 id, qint64 done, qint64 total);
    void metadataKnown(qint64 id, qint64 filesize, int connections, bool resumable);
    void optionsChanged(qint64 id, qint64 bandwidthlimit, int connections);
    void downloadFinished(qint64 id, bool ok, const QString &finalPath);
    void engineError(const QString &err);
    void rowSegmentsChanged(qint64 id, int done, int count, bool hls);

private:
    void construct(const QString &destDir);
    void buildDownloadTab();
    void buildConnectionsTab();
    void buildOptionsTab();
    void startEngine();
    void startHlsEngine();
    void onEngineState();
    void refreshInfo();
    void refreshHlsInfo();
    void applyBandwidth();
    void applyConnections();

    qint64 m_id;
    QUrl m_url;
    QString m_destDir;
    QString m_fileName;
    QStringList m_extraHeaders;
    DownloadEngine *m_engine = nullptr;
    class HlsDownloader *m_hls = nullptr;
    bool m_hlsMode = false;

    QTabWidget *m_tabs = nullptr;
    QFormLayout *m_formDownload = nullptr;
    QVector<QPair<QLabel *, QString>> m_formLabels;
    QLabel *m_lblStatus = nullptr;
    QLabel *m_lblFileSize = nullptr;
    QLabel *m_lblDownloaded = nullptr;
    QLabel *m_lblBandwidth = nullptr;
    QLabel *m_lblRemain = nullptr;
    QLabel *m_lblResumable = nullptr;
    QLabel *m_lblSegments = nullptr;
    QProgressBar *m_progress = nullptr;
    SegmentsProgressBar *m_segments = nullptr;
    QLabel *m_lblBigPct = nullptr;      // headline percent above the bars
    QLabel *m_lblSummary = nullptr;     // "done / total · speed" right of it
    QTableWidget *m_connTable = nullptr;
    QLineEdit *m_editBandwidth = nullptr;
    QComboBox *m_comboConnections = nullptr;
    QCheckBox *m_chkRemember = nullptr;
    QCheckBox *m_chkCompletion = nullptr;
    QPushButton *m_btnPause = nullptr;
    QPushButton *m_btnCancel = nullptr;
};

} // namespace neat
