#pragma once
#include <QHash>
#include <QTranslator>
#include <QStringList>
#include <QMainWindow>
#include <QVector>

class QSystemTrayIcon;
class QTreeWidget;
class QTableWidget;
class QToolBar;
class QAction;

namespace neat {

class DownloadWindow;
class Db;
class WsServer;
struct ExtDownloadRequest;
class SegmentsProgressBar;
struct DownloadRecord;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);

    void showFromTray();
    void startDownload(const QString &url);
    void startDownloadFromExtension(const ExtDownloadRequest &req);
    void startHlsDownload(const ExtDownloadRequest &req);
    void setDefaultBandwidthKb(qint64 kb) { m_defaultLimitKb = kb; }
    int activeDownloads() const
    {
        int n = 0;
        for (const Row &r : m_rows)
            if (r.window)
                ++n;
        return n;
    }
    bool runUiSmoke();   // offscreen smoke test for --selftest-ui
    bool anyFailed() const { return m_anyFailed; }

protected:
    void closeEvent(QCloseEvent *event) override;
    void changeEvent(QEvent *e) override;

private:
    void buildToolbar();
    QTreeWidget *buildCategoryTree();
    QTableWidget *buildDownloadsTable();
    void buildTray();
    void loadRecords();
    int appendRow(qint64 id, const QString &name, qint64 sizeBytes, qint64 lastTrySecs,
                  const QString &status);
    int rowOfId(qint64 id) const;
    qint64 idAtRow(int row) const;
    void setRowProgress(qint64 id, qint64 done, qint64 total);
    void paintRowBar(qint64 id);
    void wireWindow(DownloadWindow *win);
    void openDownloadWindow(const DownloadRecord &rec);
    QVector<qint64> selectedIds() const;
    void stopSelected();
    void deleteSelected();
    void redownloadSelected();

    QToolBar *m_toolbar = nullptr;
    QAction *m_actNewUrl = nullptr;
    QAction *m_actBrowsers = nullptr;
    QAction *m_actSettings = nullptr;
    QAction *m_actAbout = nullptr;
    QAction *m_actResume = nullptr;
    QAction *m_actStop = nullptr;
    QAction *m_actDelete = nullptr;
    QAction *m_actQuit = nullptr;
    QAction *m_actLang = nullptr;
    QTreeWidget *m_categories = nullptr;
    QTableWidget *m_downloads = nullptr;
    QSystemTrayIcon *m_tray = nullptr;
    Db *m_db = nullptr;
    WsServer *m_ws = nullptr;
    void startWsServer();
    qint64 m_defaultLimitKb = 0;
    QString m_pendingFileName, m_pendingPageUrl, m_pendingPageTitle;
    QStringList m_pendingHeaders;
    QByteArray m_pendingPostBody;
    QString m_pendingPostType;
    bool m_anyFailed = false;
    QString m_lastError;

    struct Row {
        qint64 id = 0;
        DownloadWindow *window = nullptr;   // null for historical records
        QString category;
        QString status;
        qint64 sizeHint = 0;      // total bytes (0 = unknown)
        qint64 doneHint = 0;      // downloaded bytes
        int hlsDone = 0, hlsCount = 0;
        bool hls = false;
    };
    QHash<qint64, Row> m_rows;
    int m_filterRole = 10;  // original lParam node id: T0=All/Complete/Incomplete,
                            // T1..T6 = Video/Audio/Compressed/Document/Application/Misc
    QTranslator m_translator;
    void toggleLanguage();
    void retranslateUi();
    void rebuildTrayMenu();
    void applyRowFilter(const Row &r);
    void applyCurrentFilter();
};

} // namespace neat
