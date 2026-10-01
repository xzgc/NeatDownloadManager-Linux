#include "mainwindow.h"

#include "core/db.h"
#include "core/extprotocol.h"
#include "core/settings.h"
#include "aboutwindow.h"
#include "authwindow.h"
#include "browserswindow.h"
#include "propertieswindow.h"
#include "quitwindow.h"
#include "settingswindow.h"
#include "core/wsserver.h"
#include "downloadwindow.h"
#include "fileicon.h"
#include "format.h"
#include "segprogressbar.h"
#include "urlwindow.h"

#include <QAction>
#include <QEvent>
#include <QIcon>
#include <QTranslator>
#include <QPainter>
#include <QPainterPath>
#include <QStyledItemDelegate>
#include <QApplication>
#include <QCloseEvent>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHeaderView>
#include <QMenu>
#include <QSet>
#include <QSplitter>
#include <QStandardPaths>
#include <QLabel>
#include <QSystemTrayIcon>
#include <QTableWidget>
#include <QToolBar>
#include <QTreeWidget>
#include <QUrl>

namespace neat {

namespace {
enum Cols { ColName = 0, ColSize, ColStatus, ColProgress, ColLastTry, ColCount };
enum Roles { IdRole = Qt::UserRole + 1, NumericRole };   // NumericRole sorts by value
} // namespace

// sorts numerically when both items carry NumericRole (bytes/epoch/percent)
class SortItem : public QTableWidgetItem {
public:
    using QTableWidgetItem::QTableWidgetItem;
    bool operator<(const QTableWidgetItem &other) const override
    {
        const QVariant a = data(NumericRole), b = other.data(NumericRole);
        if (a.isValid() && b.isValid())
            return a.toLongLong() < b.toLongLong();
        return QTableWidgetItem::operator<(other);
    }
};

// paints the Progress column from item roles (sort-safe: no cell widgets)
class ProgressDelegate : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    static constexpr int TotalRole = Qt::UserRole + 10;
    static constexpr int DoneRole = Qt::UserRole + 11;
    static constexpr int HlsRole = Qt::UserRole + 12;
    static constexpr int HlsDoneRole = Qt::UserRole + 13;
    static constexpr int HlsCountRole = Qt::UserRole + 14;

    void paint(QPainter *p, const QStyleOptionViewItem &opt,
               const QModelIndex &idx) const override
    {
        p->save();
        p->setRenderHint(QPainter::Antialiasing);
        const bool hls = idx.data(HlsRole).toBool();
        const qint64 total = idx.data(TotalRole).toLongLong();
        const qint64 done = idx.data(DoneRole).toLongLong();
        const int hlsCount = idx.data(HlsCountRole).toInt();
        const int hlsDone = idx.data(HlsDoneRole).toInt();

        // percentage centered in the band right above the bar; a finished
        // download always shows 100% no matter what the roles contain
        const bool complete = hls ? (hlsCount > 0 && hlsDone >= hlsCount)
                                  : (total > 0 && done >= total);
        const int permille = idx.data(NumericRole).toInt();   // percent * 1000
        if (hls || total > 0) {
            const int percent = complete ? 100 : qBound(0, qRound(permille / 1000.0), 99);
            QFont f = p->font();
            f.setPointSizeF(f.pointSizeF() * 0.85);
            f.setBold(complete);
            p->setFont(f);
            p->setPen(complete ? QColor(0x1E, 0x84, 0x49)   // brand green
                               : QColor(0x6B, 0x72, 0x80)); // secondary gray
            const QRectF textRect = opt.rect.adjusted(0, 2, 0, -12);
            p->drawText(textRect, Qt::AlignHCenter | Qt::AlignVCenter,
                        QString::number(percent) + QLatin1Char('%'));
        }

        // bar sits in the lower band; modern flat colors (theme.qss)
        const QRectF r = QRectF(opt.rect).adjusted(3, opt.rect.height() - 13, -3, -4);
        const QColor track = QColor(0xE9, 0xEB, 0xEE);
        const QColor fill = QColor(0x27, 0xAE, 0x60);
        QPainterPath frame;
        frame.addRoundedRect(r, r.height() / 2, r.height() / 2);
        p->fillPath(frame, track);

        if (hls) {
            const int count = hlsCount;
            const int done = hlsDone;
            if (count > 0) {
                const qreal gap = 1.0;
                const qreal cellW = (r.width() - gap * (count - 1)) / count;
                for (int i = 0; i < count; ++i) {
                    if (i >= done)
                        break;
                    QPainterPath cell;
                    cell.addRect(QRectF(r.x() + i * (cellW + gap), r.top() + 1,
                                        cellW, r.height() - 2));
                    p->fillPath(frame.intersected(cell), fill);
                }
            }
        } else {
            if (total > 0 && done > 0) {
                QPainterPath bar;
                bar.addRect(QRectF(r.left() + 1, r.top() + 1,
                                   qMin<qint64>(done, total) * (r.width() - 2) / total,
                                   r.height() - 2));
                p->fillPath(frame.intersected(bar), fill);
            }
        }
        p->restore();
    }
};

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    setWindowTitle(tr("Neat Download Manager"));
    setWindowIcon(QIcon(QStringLiteral(":/icons/appicon.png")));
    resize(980, 560);

    buildToolbar();

    auto *split = new QSplitter(this);
    m_categories = buildCategoryTree();
    m_downloads = buildDownloadsTable();
    split->addWidget(m_categories);
    split->addWidget(m_downloads);
    split->setStretchFactor(0, 0);
    split->setStretchFactor(1, 1);
    split->setSizes({200, 780});
    setCentralWidget(split);

    buildTray();

    m_db = new Db(this);
    const QString dataDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (m_db->open(dataDir + QStringLiteral("/NeatDB.db")))
        loadRecords();
    startWsServer();
    if (Settings::instance().language() == QLatin1String("zh")) {
        if (m_translator.load(QStringLiteral(":/icons/neatdm_zh.qm")))
            qApp->installTranslator(&m_translator);
    }
}

void MainWindow::toggleLanguage()
{
    Settings &s = Settings::instance();
    if (s.language() == QLatin1String("zh")) {
        s.setLanguage(QStringLiteral("en"));
        qApp->removeTranslator(&m_translator);
    } else {
        s.setLanguage(QStringLiteral("zh"));
        if (m_translator.load(QStringLiteral(":/icons/neatdm_zh.qm")))
            qApp->installTranslator(&m_translator);
    }
}

void MainWindow::changeEvent(QEvent *e)
{
    if (e->type() == QEvent::LanguageChange)
        retranslateUi();
    QMainWindow::changeEvent(e);
}

void MainWindow::retranslateUi()
{
    setWindowTitle(tr("Neat Download Manager"));
    m_actNewUrl->setText(tr("New URL"));
    m_actBrowsers->setText(tr("Browsers"));
    m_actSettings->setText(tr("Settings"));
    m_actAbout->setText(tr("About"));
    m_actResume->setText(tr("Resume"));
    m_actStop->setText(tr("Stop"));
    m_actDelete->setText(tr("Delete"));
    m_actQuit->setText(tr("Quit"));
    m_toolbar->setWindowTitle(tr("Main"));

    // category tree: same structure as buildCategoryTree (3 tops x 6 children)
    const QStringList tops = { tr("All Downloads"), tr("Complete"), tr("Incomplete") };
    const QStringList cats = { tr("Video"), tr("Audio"), tr("Compressed"),
                               tr("Document"), tr("Application"), tr("Misc") };
    for (int t = 0; t < m_categories->topLevelItemCount() && t < tops.size(); ++t) {
        m_categories->topLevelItem(t)->setText(0, tops[t]);
        for (int k = 0; k < m_categories->topLevelItem(t)->childCount() && k < cats.size(); ++k)
            m_categories->topLevelItem(t)->child(k)->setText(0, cats[k]);
    }

    m_downloads->setHorizontalHeaderLabels({ tr("File Name"), tr("File Size"), tr("Status"),
                                             tr("Progress"), tr("Last Try") });
    // refresh status column from canonical state
    for (auto it = m_rows.begin(); it != m_rows.end(); ++it) {
        const int row = rowOfId(it.key());
        if (row < 0)
            continue;
        if (QTableWidgetItem *si = m_downloads->item(row, ColStatus))
            si->setText(it.value().status == QLatin1String("Complete") ? tr("Complete")
                                                                       : tr("Incomplete"));
    }
    rebuildTrayMenu();
}

void MainWindow::rebuildTrayMenu()
{
    if (!m_tray)
        return;
    QMenu *old = m_tray->contextMenu();
    auto *menu = new QMenu(this);
    auto *showAction = menu->addAction(tr("Show Window"));
    menu->addSeparator();
    auto *quitAction = menu->addAction(tr("Total Quit"));
    connect(quitAction, &QAction::triggered, this, [this] {
        if (activeDownloads() > 0) {
            QuitWindow q(this);
            q.exec();
            if (!q.wantsQuit())
                return;
        }
        qApp->quit();
    });
    m_tray->setContextMenu(menu);
    connect(showAction, &QAction::triggered, this, &MainWindow::showFromTray);
    delete old;
}

void MainWindow::startWsServer()
{
    m_ws = new WsServer(this);
    if (!m_ws->start()) {
        qWarning("WebSocket server: port 10007 busy");
        return;
    }
    const Settings &s = Settings::instance();
    const QString panels = QStringList{QStringLiteral("ShowPanelChrome=%1").arg(s.browserPanel(QStringLiteral("Chrome")) ? 1 : 0),
                                        QStringLiteral("ShowPanelFox=%1").arg(s.browserPanel(QStringLiteral("Fox")) ? 1 : 0),
                                        QStringLiteral("ShowPanelEdge=%1").arg(s.browserPanel(QStringLiteral("Edge")) ? 1 : 0)}
                                   .join(QLatin1String("\r\n"));
    connect(m_ws, &WsServer::clientConnected, this, [this, panels](WsConnection *conn) {
        conn->sendText(QStringLiteral("nowaiting"));
        conn->sendText(panels);
    });
    connect(m_ws, &WsServer::messageReceived, this,
            [this](WsConnection *, const QString &msg) {
                const ExtDownloadRequest req = ExtProtocol::parse(msg);
                if (req.isValid())
                    startDownloadFromExtension(req);
            });
}

void MainWindow::buildToolbar()
{
    m_toolbar = addToolBar(tr("Main"));
    m_toolbar->setMovable(false);
    m_toolbar->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);

    // 1:1 with NeatMainToolBar (sub_502CD0): left group New URL/Resume/Stop/
    // Delete, then a flexible gap, then the right group flush to the right
    // edge: Settings/Browsers/About/Quit. The original implements the gap as
    // a blank toolbar button whose text is refilled with spaces on every
    // resize ((width - 304*dpi - leftGroupText) / spaceWidth); a stretch
    // widget is the Qt equivalent.
    m_actNewUrl = m_toolbar->addAction(QIcon(QStringLiteral(":/icons/newurl.png")), tr("New URL"));
    m_actResume = m_toolbar->addAction(QIcon(QStringLiteral(":/icons/resume.png")), tr("Resume"));
    m_actStop = m_toolbar->addAction(QIcon(QStringLiteral(":/icons/stop.png")), tr("Stop"));
    m_actDelete = m_toolbar->addAction(QIcon(QStringLiteral(":/icons/delete.png")), tr("Delete"));
    m_actLang = m_toolbar->addAction(QStringLiteral("中文/EN"));
    auto *stretch = new QWidget(m_toolbar);
    stretch->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    m_toolbar->addWidget(stretch);
    m_actSettings = m_toolbar->addAction(QIcon(QStringLiteral(":/icons/settings.png")), tr("Settings"));
    m_actBrowsers = m_toolbar->addAction(QIcon(QStringLiteral(":/icons/browsers.png")), tr("Browsers"));
    m_actAbout = m_toolbar->addAction(QIcon(QStringLiteral(":/icons/about.png")), tr("About"));
    m_actQuit = m_toolbar->addAction(QIcon(QStringLiteral(":/icons/quit.png")), tr("Quit"));

    connect(m_actNewUrl, &QAction::triggered, this, [this] {
        UrlWindow dlg(this);
        if (dlg.exec() == QDialog::Accepted && !dlg.url().isEmpty())
            startDownload(dlg.url());
    });
    connect(m_actResume, &QAction::triggered, this, [this] {
        for (qint64 id : selectedIds()) {
            const auto it = m_rows.find(id);
            if (it != m_rows.end() && it.value().window) {
                it.value().window->resumeEngine();
                it.value().window->show();
                it.value().window->raise();
                continue;
            }
            const QVector<DownloadRecord> recs = m_db->loadAll();
            for (const DownloadRecord &r : recs) {
                if (r.id == id && r.status != QLatin1String("Complete")) {
                    openDownloadWindow(r);
                    break;
                }
            }
        }
    });
    connect(m_actSettings, &QAction::triggered, this,
            [this] { SettingsWindow(this).exec(); });
    connect(m_actBrowsers, &QAction::triggered, this,
            [this] { (new BrowsersWindow(this))->show(); });
    connect(m_actAbout, &QAction::triggered, this,
            [this] { (new AboutWindow(this))->show(); });
    connect(m_actLang, &QAction::triggered, this, &MainWindow::toggleLanguage);
    connect(m_actQuit, &QAction::triggered, this, [this] {
        QuitWindow q(this);
        q.exec();
    });
    connect(m_actStop, &QAction::triggered, this, &MainWindow::stopSelected);
    connect(m_actDelete, &QAction::triggered, this, &MainWindow::deleteSelected);
}

QTreeWidget *MainWindow::buildCategoryTree()
{
    // 1:1 with NeatCategoryTree (sub_4F2190): three top nodes sharing one icon,
    // each with six category children; ids mirror the original item lParam
    // encoding (group x 10 + category). Original mixes its own 16px resource
    // icons with system shell icons (.zip/.exe/generic) for the categories;
    // freedesktop theme icons are the Linux equivalent of the shell ones.
    auto *tree = new QTreeWidget(this);
    tree->header()->hide();
    tree->setRootIsDecorated(true);
    tree->setUniformRowHeights(true);

    const QIcon topIcon = QIcon(QStringLiteral(":/icons/cat_all.png"));
    const QIcon catIcons[6] = {
        QIcon(QStringLiteral(":/icons/cat_video.png")),
        QIcon(QStringLiteral(":/icons/cat_audio.png")),
        QIcon::fromTheme(QStringLiteral("application-zip"),
                         QIcon(QStringLiteral(":/icons/ft/application-zip.png"))),
        QIcon(QStringLiteral(":/icons/cat_document.png")),
        QIcon::fromTheme(QStringLiteral("application-x-executable"),
                         QIcon(QStringLiteral(":/icons/ft/application-x-executable.png"))),
        QIcon::fromTheme(QStringLiteral("text-x-generic"),
                         QIcon(QStringLiteral(":/icons/ft/text-x-generic.png"))),
    };
    const QString tops[3] = { tr("All Downloads"), tr("Complete"), tr("Incomplete") };
    const QString cats[6] = { tr("Video"), tr("Audio"), tr("Compressed"),
                              tr("Document"), tr("Application"), tr("Misc") };
    const int kCatId[6] = { 1, 2, 3, 4, 5, 6 };   // original iImage indices

    for (int t = 0; t < 3; ++t) {
        auto *top = new QTreeWidgetItem(tree, {tops[t]});
        top->setIcon(0, topIcon);
        top->setFlags(Qt::ItemIsSelectable | Qt::ItemIsEnabled);
        top->setData(0, Qt::UserRole, (t + 1) * 10);
        for (int k = 0; k < 6; ++k) {
            auto *ch = new QTreeWidgetItem(top, {cats[k]});
            ch->setIcon(0, catIcons[kCatId[k] - 1]);
            ch->setFlags(Qt::ItemIsSelectable | Qt::ItemIsEnabled);
            ch->setData(0, Qt::UserRole, (t + 1) * 10 + kCatId[k]);
        }
        top->setExpanded(false);   // original sends no TVM_EXPAND: starts collapsed
    }

    // original performs no initial selection either; m_filterRole stays "All"
    connect(tree, &QTreeWidget::currentItemChanged, this,
            [this](QTreeWidgetItem *cur, QTreeWidgetItem *) {
                m_filterRole = cur ? cur->data(0, Qt::UserRole).toInt() : 10;
                applyCurrentFilter();
            });
    return tree;
}

QTableWidget *MainWindow::buildDownloadsTable()
{
    auto *table = new QTableWidget(0, ColCount, this);
    table->setHorizontalHeaderLabels(
        {tr("File Name"), tr("File Size"), tr("Status"), tr("Progress"), tr("Last Try")});
    table->horizontalHeader()->setStretchLastSection(true);
    table->horizontalHeader()->setSectionResizeMode(ColName, QHeaderView::Interactive);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setAlternatingRowColors(true);
    table->verticalHeader()->hide();
    table->setSortingEnabled(true);
    table->setItemDelegateForColumn(ColProgress, new ProgressDelegate(table));
    table->setContextMenuPolicy(Qt::CustomContextMenu);

    connect(table, &QTableWidget::cellDoubleClicked, this, [this](int row, int col) {
        Q_UNUSED(col);
        const qint64 id = idAtRow(row);
        if (id <= 0)
            return;
        const auto it = m_rows.find(id);
        if (it != m_rows.end() && it.value().window) {
            it.value().window->show();
            it.value().window->raise();
            it.value().window->activateWindow();
            return;
        }
        const QVector<DownloadRecord> recs = m_db->loadAll();
        for (const DownloadRecord &r : recs) {
            if (r.id != id)
                continue;
            if (r.status != QLatin1String("Complete"))
                openDownloadWindow(r);
            else
                (new PropertiesWindow(r, this))->show();
            return;
        }
    });
    connect(table, &QTableWidget::customContextMenuRequested, this,
            [this, table](const QPoint &pos) {
        const QModelIndex at = table->indexAt(pos);
        if (at.isValid() && !table->selectionModel()->isSelected(at))
            table->selectRow(at.row());
        if (selectedIds().isEmpty())
            return;
        QMenu menu(this);
        menu.addAction(tr("Stop"), this, &MainWindow::stopSelected);
        menu.addAction(tr("Delete"), this, &MainWindow::deleteSelected);
        menu.addAction(tr("Redownload"), this, &MainWindow::redownloadSelected);
        menu.addSeparator();
        menu.addAction(tr("Open"), this, [this] {
            const QVector<DownloadRecord> recs = m_db->loadAll();
            for (qint64 id : selectedIds())
                for (const DownloadRecord &r : recs)
                    if (r.id == id && r.status == QLatin1String("Complete"))
                        QDesktopServices::openUrl(
                            QUrl::fromLocalFile(r.folderpath + QLatin1Char('/') + r.filename));
        });
        menu.addAction(tr("Properties"), this, [this] {
            const QVector<DownloadRecord> recs = m_db->loadAll();
            for (qint64 id : selectedIds())
                for (const DownloadRecord &r : recs)
                    if (r.id == id)
                        (new PropertiesWindow(r, this))->show();
        });
        menu.addAction(tr("Open Folder"), this, [this] {
            const QVector<DownloadRecord> recs = m_db->loadAll();
            for (qint64 id : selectedIds())
                for (const DownloadRecord &r : recs)
                    if (r.id == id)
                        QDesktopServices::openUrl(QUrl::fromLocalFile(r.folderpath));
        });
        menu.exec(m_downloads->viewport()->mapToGlobal(pos));
    });
    return table;
}

void MainWindow::buildTray()
{
    m_tray = new QSystemTrayIcon(QIcon(QStringLiteral(":/icons/appicon.png")), this);
    rebuildTrayMenu();
    connect(m_tray, &QSystemTrayIcon::activated, this,
            [this](QSystemTrayIcon::ActivationReason r) {
                if (r == QSystemTrayIcon::Trigger)
                    showFromTray();
            });
    m_tray->show();
}

void MainWindow::loadRecords()
{
    const QVector<DownloadRecord> recs = m_db->loadAll();
    for (const DownloadRecord &r : recs) {
        const bool complete = r.status == QLatin1String("Complete");
        QString category = r.category.toLower();
        if (category == QLatin1String("app")) {   // pre-v0.2 spelling
            category = QStringLiteral("application");
            m_db->updateDownload(r.id, QStringLiteral("category"), category);
        }
        qint64 shownSize = r.filesize;
        const QString onDisk = r.folderpath + QLatin1Char('/') + r.filename;
        if (r.status == QLatin1String("Complete") && QFileInfo::exists(onDisk)) {
            const qint64 actual = QFileInfo(onDisk).size();
            if (actual > 0 && actual != r.filesize) {
                shownSize = actual;
                m_db->updateDownload(r.id, QStringLiteral("filesize"), actual);
            }
        }
        const int row = appendRow(r.id, r.filename, shownSize, r.lasttry,
                                  complete ? tr("Complete") : tr("Incomplete"));
        Row rr;
        rr.id = r.id;
        rr.category = category;
        rr.status = r.status;
        rr.sizeHint = shownSize;
        rr.doneHint = complete ? shownSize : 0;
        m_rows.insert(r.id, rr);
        paintRowBar(r.id);
    }
    m_downloads->sortByColumn(ColLastTry, Qt::DescendingOrder);
    applyCurrentFilter();
}

int MainWindow::rowOfId(qint64 id) const
{
    for (int r = 0; r < m_downloads->rowCount(); ++r)
        if (idAtRow(r) == id)
            return r;
    return -1;
}

qint64 MainWindow::idAtRow(int row) const
{
    const QTableWidgetItem *it = m_downloads->item(row, ColName);
    return it ? it->data(IdRole).toLongLong() : 0;
}

int MainWindow::appendRow(qint64 id, const QString &name, qint64 sizeBytes, qint64 lastTrySecs,
                          const QString &status)
{
    const int row = m_downloads->rowCount();
    m_downloads->insertRow(row);

    auto *nameItem = new SortItem;
    nameItem->setText(name);
    nameItem->setIcon(fileIconForName(name));
    nameItem->setData(IdRole, id);
    m_downloads->setItem(row, ColName, nameItem);

    auto *sizeItem = new SortItem;
    sizeItem->setText(sizeBytes > 0 ? formatBytes(sizeBytes) : tr("Unknown"));
    sizeItem->setData(NumericRole, sizeBytes);
    m_downloads->setItem(row, ColSize, sizeItem);

    auto *statusItem = new SortItem;
    statusItem->setText(status);
    m_downloads->setItem(row, ColStatus, statusItem);

    auto *progressItem = new SortItem;   // numeric only; the bar paints on top
    progressItem->setData(NumericRole, 0);
    m_downloads->setItem(row, ColProgress, progressItem);

    auto *dateItem = new SortItem;
    dateItem->setText(QDateTime::fromSecsSinceEpoch(lastTrySecs)
                          .toString(QStringLiteral("yyyy-MM-dd HH:mm:ss")));
    dateItem->setData(NumericRole, lastTrySecs);
    m_downloads->setItem(row, ColLastTry, dateItem);
    return row;
}

void MainWindow::setRowProgress(qint64 id, qint64 done, qint64 total)
{
    const int row = rowOfId(id);
    if (row < 0)
        return;
    const auto it = const_cast<QHash<qint64, Row> &>(m_rows).find(id);
    if (it != m_rows.end()) {
        it.value().doneHint = done;
        if (total > 0)
            it.value().sizeHint = total;
    }
    if (QTableWidgetItem *pi = m_downloads->item(row, ColProgress))
        pi->setData(NumericRole, total > 0 ? done * 100000 / total : 0);
    if (QTableWidgetItem *si = m_downloads->item(row, ColSize);
        si && total > 0 && si->data(NumericRole).toLongLong() != total) {
        si->setText(formatBytes(total));
        si->setData(NumericRole, total);
    }
}

void MainWindow::paintRowBar(qint64 id)
{
    const auto it = m_rows.find(id);
    if (it == m_rows.end())
        return;
    const Row &r = it.value();
    const int row = rowOfId(id);
    if (row < 0)
        return;
    if (QTableWidgetItem *pi = m_downloads->item(row, ColProgress)) {
        pi->setData(ProgressDelegate::HlsRole, r.hls);
        pi->setData(ProgressDelegate::HlsDoneRole, r.hlsDone);
        pi->setData(ProgressDelegate::HlsCountRole, qMax(r.hlsCount, 1));
        pi->setData(ProgressDelegate::TotalRole, r.sizeHint);
        pi->setData(ProgressDelegate::DoneRole, r.doneHint);
        pi->setData(NumericRole, r.hls ? (r.hlsCount > 0
                                              ? r.hlsDone * 100000 / r.hlsCount
                                              : 0)
                                       : (r.sizeHint > 0
                                              ? qMin(r.doneHint, r.sizeHint) * 100000
                                                    / r.sizeHint
                                              : 0));
        m_downloads->viewport()->update();
    }
}

void MainWindow::wireWindow(DownloadWindow *win)
{
    const qint64 id = win->downloadId();

    // site credentials: silent lookup from the auths table, prompt via AuthWindow
    if (DownloadEngine *eng = win->engine()) {
        eng->setCredentialLookup([this](const QString &host, const QString &proto,
                                        QString &user, QString &pass) {
            return m_db->findCredential(host, proto, user, pass);
        });
        connect(eng, &DownloadEngine::authRequired, this,
                [this, win](const QString &host, const QString &realm, const QString &scheme) {
                    AuthWindow dlg(scheme, host, realm, this);
                    if (dlg.exec() == QDialog::Accepted && !dlg.userName().isEmpty()) {
                        if (dlg.remember())
                            m_db->saveCredential(host, QStringLiteral("http"), dlg.userName(),
                                                 dlg.password());
                        if (DownloadEngine *e = win->engine())
                            e->provideCredentials(dlg.userName(), dlg.password());
                    } else {
                        if (DownloadEngine *e = win->engine())
                            e->provideCredentials(QString(), QString());
                    }
                });
    }

    connect(win, &DownloadWindow::rowStatusChanged, this,
            [this, id](qint64, const QString &status, const QString &speed) {
                const int row = rowOfId(id);
                if (row < 0)
                    return;
                if (QTableWidgetItem *si = m_downloads->item(row, ColStatus))
                    si->setText(status + QStringLiteral("  ") + speed);
                if (auto it = m_rows.find(id); it != m_rows.end()) {
                    it.value().status = status == tr("Complete") ? QStringLiteral("Complete")
                                                                : QStringLiteral("Incomplete");
                    applyRowFilter(it.value());
                }
            });
    connect(win, &DownloadWindow::rowSegmentsChanged, this,
            [this, id](qint64, int doneCount, int count, bool hls) {
                auto it = m_rows.find(id);
                if (it == m_rows.end())
                    return;
                it.value().hls = hls;
                it.value().hlsDone = doneCount;
                it.value().hlsCount = count;
                paintRowBar(id);
            });
    connect(win, &DownloadWindow::rowProgressChanged, this,
            [this, id](qint64 done, qint64 total) {
                setRowProgress(id, done, total);
                paintRowBar(id);
            });
    connect(win, &DownloadWindow::metadataKnown, this,
            [this](qint64 recId, qint64 filesize, int connections, bool resumable) {
                m_db->updateDownload(recId, QStringLiteral("filesize"), filesize);
                m_db->updateDownload(recId, QStringLiteral("connections"), qint64(connections));
                m_db->updateDownload(recId, QStringLiteral("resumable"), resumable ? 1 : 0);
            });
    connect(win, &DownloadWindow::optionsChanged, this,
            [this](qint64 recId, qint64 bandwidthlimit, int connections) {
                if (bandwidthlimit >= 0)
                    m_db->updateDownload(recId, QStringLiteral("bandwidthlimit"), bandwidthlimit);
                if (connections > 0)
                    m_db->updateDownload(recId, QStringLiteral("connections"), qint64(connections));
            });
    connect(win, &DownloadWindow::engineError, this, [this](const QString &err) { m_lastError = err; });
    connect(win, &DownloadWindow::downloadFinished, this,
            [this](qint64 recId, bool ok, const QString &finalPath) {
                const auto it = m_rows.find(recId);
                if (it == m_rows.end())
                    return;
                if (ok) {
                    m_db->updateStatus(recId, QStringLiteral("Complete"));
                    if (!finalPath.isEmpty()) {
                        const qint64 actual = QFileInfo(finalPath).size();
                        if (actual > 0) {
                            it.value().sizeHint = actual;
                            it.value().doneHint = actual;
                            m_db->updateDownload(recId, QStringLiteral("filesize"), actual);
                            if (QTableWidgetItem *si = m_downloads->item(rowOfId(recId), ColSize)) {
                                si->setText(formatBytes(actual));
                                si->setData(NumericRole, actual);
                            }
                            paintRowBar(recId);
                        }
                    }
                    m_db->updateDownload(recId, QStringLiteral("folderpath"),
                                         QFileInfo(finalPath).absolutePath());
                    if (!finalPath.isEmpty()) {
                        const QString actualName = QFileInfo(finalPath).fileName();
                        m_db->updateDownload(recId, QStringLiteral("filename"), actualName);
                        if (QTableWidgetItem *ni = m_downloads->item(rowOfId(recId), ColName))
                            ni->setText(actualName);
                    }
                    it.value().status = QStringLiteral("Complete");
                    applyRowFilter(it.value());
                } else {
                    m_db->updateStatus(recId, QStringLiteral("Incomplete"), m_lastError);
                    it.value().status = QStringLiteral("Incomplete");
                    applyRowFilter(it.value());
                    m_anyFailed = true;
                }
                it.value().window = nullptr;   // registry entry stays: context menu /
                                               // properties / delete still need it
                // finished: paint the bar completely full
                if (ok) {
                    it.value().doneHint = it.value().sizeHint;
                    it.value().hlsDone = it.value().hlsCount;
                }
                paintRowBar(recId);
            });
}

void MainWindow::startDownloadFromExtension(const ExtDownloadRequest &req)
{
    if (req.ltype == QLatin1String("hls")) {
        startHlsDownload(req);
        return;
    }
    // POST downloads: replay as GET in M3 (POST body replay lands with M4 auth work)
    QStringList headers = req.extraHeaders;
    if (!req.cookies.isEmpty())
        headers << QStringLiteral("Cookie: ") + req.cookies;
    if (!req.referer.isEmpty())
        headers << QStringLiteral("Referer: ") + req.referer;
    if (!req.origin.isEmpty())
        headers << QStringLiteral("Origin: ") + req.origin;

    // reuse the record/new-flow by delegating with meta
    m_pendingFileName = req.filename;
    m_pendingHeaders = headers;
    m_pendingPageUrl = req.pageUrl;
    m_pendingPageTitle = req.pageTitle;
    m_pendingPostBody = req.postData;
    m_pendingPostType = req.contentType;
    startDownload(req.url);
    m_pendingFileName.clear();
    m_pendingHeaders.clear();
    m_pendingPostBody.clear();
    m_pendingPostType.clear();
}

void MainWindow::startHlsDownload(const ExtDownloadRequest &req)
{
    Settings &s = Settings::instance();
    const qint64 id = s.lastDownloadId() + 1;
    s.setLastDownloadId(id);

    QString fileName = req.filename;
    if (fileName.isEmpty() && !req.pageTitle.isEmpty())
        fileName = req.pageTitle;
    if (fileName.isEmpty())
        fileName = QStringLiteral("TS File");
    const QString category = QStringLiteral("video");
    QString destDir = s.downloadDirectory();
    if (s.categoryFolders()) {
        destDir += QLatin1String("/Video");
        QDir().mkpath(destDir);
    }

    DownloadRecord rec;
    rec.id = id;
    rec.url = req.url;
    rec.filename = fileName;
    rec.ltype = QStringLiteral("hls");
    rec.category = category;
    rec.status = QStringLiteral("Incomplete");
    rec.lasttry = rec.firsttry = QDateTime::currentSecsSinceEpoch();
    rec.folderpath = destDir;
    rec.pageurl = req.pageUrl;
    rec.pagetitle = req.pageTitle;
    rec.hittitle = req.label;
    m_db->insertDownload(rec);

    const int row = appendRow(id, fileName, 0, QDateTime::currentSecsSinceEpoch(),
                              tr("Starting..."));
    Row r;
    r.id = id;
    r.category = category;
    r.status = QStringLiteral("Incomplete");
    r.hls = true;
    m_rows.insert(id, r);
    QStringList headers = req.extraHeaders;
    if (!req.cookies.isEmpty())
        headers << QStringLiteral("Cookie: ") + req.cookies;
    if (!req.referer.isEmpty())
        headers << QStringLiteral("Referer: ") + req.referer;
    auto *win = new DownloadWindow(id, QUrl(req.url), destDir, this, fileName, headers,
                                   /*hls=*/true);
    m_rows[id].window = win;
    wireWindow(win);
    if (m_defaultLimitKb > 0)
        win->applyDefaultBandwidth(m_defaultLimitKb);
    win->show();
}

void MainWindow::startDownload(const QString &url)
{
    // duplicate of an incomplete download -> resume it
    if (const auto rec = m_db->findIncompleteByUrl(url)) {
        const auto it = m_rows.find(rec->id);
        if (it != m_rows.end() && it.value().window) {
            it.value().window->show();
            it.value().window->raise();
            return;
        }
        if (it != m_rows.end())
            m_rows.erase(it);
        openDownloadWindow(*rec);
        return;
    }

    Settings &s = Settings::instance();
    const qint64 id = s.lastDownloadId() + 1;
    s.setLastDownloadId(id);

    const QUrl u(url);
    QString fileName = m_pendingFileName.isEmpty() ? QFileInfo(u.path()).fileName()
                                                   : m_pendingFileName;
    if (fileName.isEmpty())
        fileName = QStringLiteral("download");
    const QString category = Db::classify(fileName);
    QString destDir = s.downloadDirectory();
    if (s.categoryFolders()) {
        const QString sub = category == QLatin1String("app") ? QStringLiteral("Misc") : category;
        destDir += QLatin1Char('/') + sub.left(1).toUpper() + sub.mid(1);
        QDir().mkpath(destDir);
    }

    DownloadRecord rec;
    rec.id = id;
    rec.url = url;
    rec.filename = fileName;
    rec.category = category;
    rec.status = QStringLiteral("Incomplete");
    rec.lasttry = rec.firsttry = QDateTime::currentSecsSinceEpoch();
    rec.folderpath = destDir;
    rec.pageurl = m_pendingPageUrl;
    rec.pagetitle = m_pendingPageTitle;
    rec.mimetype;   // filled after probe in later milestone
    m_db->insertDownload(rec);

    const int row = appendRow(id, fileName, 0, QDateTime::currentSecsSinceEpoch(),
                              tr("Starting..."));
    Row r;
    r.id = id;
    r.category = category;
    r.status = QStringLiteral("Incomplete");
    m_rows.insert(id, r);
    auto *win = new DownloadWindow(id, u, destDir, this, m_pendingFileName, m_pendingHeaders);
    if (!m_pendingPostBody.isEmpty() && win->engine())
        win->engine()->setPostData(m_pendingPostBody,
                                   m_pendingPostType.isEmpty() ? QStringLiteral(
                                                                    "application/x-www-form-urlencoded")
                                                               : m_pendingPostType);
    m_rows[id].window = win;
    wireWindow(win);
    if (m_defaultLimitKb > 0)
        win->applyDefaultBandwidth(m_defaultLimitKb);
    win->show();
}

void MainWindow::openDownloadWindow(const DownloadRecord &rec)
{
    m_db->touchLastTry(rec.id);
    auto *win = new DownloadWindow(rec, this);
    if (rec.bandwidthlimit > 0 || rec.connections > 0)
        win->applyResumeSettings(rec.bandwidthlimit, rec.connections);
    if (!m_rows.contains(rec.id)) {
        Row r;
        r.id = rec.id;
        r.category = rec.category;
        r.status = rec.status;
        r.sizeHint = rec.filesize;
        r.doneHint = rec.status == QLatin1String("Complete") ? rec.filesize : 0;
        appendRow(rec.id, rec.filename, rec.filesize, rec.lasttry, tr("Resuming..."));
        m_rows.insert(rec.id, r);
        paintRowBar(rec.id);
    } else {
        const int row = rowOfId(rec.id);
        if (QTableWidgetItem *si = m_downloads->item(row, ColStatus))
            si->setText(tr("Resuming..."));
    }
    m_rows[rec.id].window = win;
    wireWindow(win);
    win->show();
}

QVector<qint64> MainWindow::selectedIds() const
{
    QVector<qint64> out;
    QSet<qint64> seen;
    const QList<QTableWidgetItem *> items = m_downloads->selectedItems();
    for (const auto *i : items) {
        const qint64 id = idAtRow(i->row());
        if (id > 0 && !seen.contains(id)) {
            seen.insert(id);
            out.append(id);
        }
    }
    return out;
}

void MainWindow::stopSelected()
{
    for (qint64 id : selectedIds()) {
        const auto it = m_rows.find(id);
        if (it != m_rows.end() && it.value().window)
            it.value().window->stopEngine();
    }
}

void MainWindow::deleteSelected()
{
    const QVector<qint64> ids = selectedIds();
    if (ids.isEmpty())
        return;
    m_db->deleteDownloads(ids);
    for (qint64 id : ids) {
        const auto it = m_rows.find(id);
        if (it == m_rows.end())
            continue;
        if (it.value().window)
            it.value().window->deleteLater();
        const int row = rowOfId(id);
        if (row >= 0)
            m_downloads->removeRow(row);
        m_rows.erase(it);
    }
}

void MainWindow::redownloadSelected()
{
    const QVector<DownloadRecord> recs = m_db->loadAll();
    for (qint64 id : selectedIds()) {
        for (const DownloadRecord &r : recs) {
            if (r.id != id)
                continue;
            const auto it = m_rows.find(id);
            if (it != m_rows.end()) {
                if (it.value().window)
                    it.value().window->deleteLater();
                const int row = rowOfId(id);
                if (row >= 0)
                    m_downloads->removeRow(row);
                m_rows.erase(it);
            }
            m_db->deleteDownloads({id});
            QFile::remove(r.folderpath + QStringLiteral("/.") + QString::number(id)
                           + QStringLiteral(".neatpart"));
            startDownload(r.url);
        }
    }
}

void MainWindow::applyRowFilter(const Row &r)
{
    static const char *kCats[] = { "video", "audio", "compressed", "document",
                                   "application", "misc" };
    bool show = true;
    const int group = m_filterRole / 10;        // 1 All, 2 Complete, 3 Incomplete
    const int catIdx = m_filterRole % 10;       // 0 any, 1..6 category
    if (group == 2)
        show = r.status == QLatin1String("Complete");
    else if (group == 3)
        show = r.status != QLatin1String("Complete");
    if (show && catIdx > 0) {
        QString cat = r.category.toLower();
        if (cat == QLatin1String("app"))
            cat = QStringLiteral("application");   // records stored before v0.2
        show = cat == QLatin1String(kCats[catIdx - 1]);
    }
    const int row = rowOfId(r.id);
    if (row >= 0)
        m_downloads->setRowHidden(row, !show);
}

void MainWindow::applyCurrentFilter()
{
    for (auto it = m_rows.begin(); it != m_rows.end(); ++it)
        applyRowFilter(it.value());
}

bool MainWindow::runUiSmoke()
{
    bool ok = true;
    auto check = [&ok](bool cond, const char *what) {
        qInfo("%s: %s", cond ? "PASS" : "FAIL", what);
        ok = ok && cond;
    };

    // toolbar carries the extracted original icons
    {
        const QAction *acts[] = { m_actNewUrl, m_actBrowsers, m_actSettings,
                                  m_actAbout, m_actResume, m_actStop, m_actDelete,
                                  m_actQuit };
        bool iconsOk = true;
        for (const QAction *a : acts) {
            iconsOk = iconsOk && !a->icon().isNull() && !a->icon().availableSizes().isEmpty();
            // the corner of every extracted original icon must be transparent;
            // opaque corners mean the ICO alpha/AND-mask was lost again
            const QImage im = a->icon().pixmap(32, 32).toImage();
            iconsOk = iconsOk && !im.isNull() && im.pixelColor(0, 0).alpha() == 0;
        }
        check(iconsOk, "toolbar-icons");
    }

    // every dialog constructs and lays out offscreen
    {
        AboutWindow about(this);
        QString aboutAll;
        for (const QLabel *lbl : about.findChildren<QLabel *>())
            aboutAll += lbl->text() + QLatin1Char('\n');
        check(aboutAll.contains(QLatin1String("over.arse@gmail.com"))
                  && aboutAll.contains(QStringLiteral("连晋"))
                  && aboutAll.contains(QLatin1String("1:1"))
                  && aboutAll.contains(QLatin1String("NeatDownloadManager"))
                  && !aboutAll.contains(QLatin1String("Javad Motallebi"))
                  && !aboutAll.contains(QLatin1String("Basic Version"))
                  && aboutAll.contains(QLatin1String("prior written consent")),
              "about-content");
        SettingsWindow sw(this);
        sw.show();
        BrowsersWindow bw(this);
        bw.show();
        AboutWindow aw(this);
        aw.show();
        QuitWindow qw(this);
        qw.show();
        AuthWindow auw(QStringLiteral("Basic"), QStringLiteral("example.com"),
                       QStringLiteral("realm"), this);
        auw.show();
        DownloadRecord rec;
        rec.id = 999999;
        rec.filename = QStringLiteral("smoke.bin");
        rec.url = QStringLiteral("http://example.com/smoke.bin");
        rec.status = QStringLiteral("Complete");
        rec.folderpath = QStringLiteral("/tmp");
        PropertiesWindow pw(rec, this);
        pw.show();
        QCoreApplication::processEvents();
        check(true, "dialogs-construct");
    }

    // filter behaviour with mixed records
    m_db->deleteDownloads({1001, 1002, 1003});
    DownloadRecord a;
    a.id = 1001; a.filename = "v.mp4"; a.category = "video"; a.status = "Complete";
    a.folderpath = "/tmp"; a.url = "http://x/v";
    DownloadRecord b = a;
    b.id = 1002; b.filename = "d.pdf"; b.category = "document"; b.status = "Incomplete";
    DownloadRecord d = a;
    d.id = 1003; d.filename = "m.zip"; d.category = "compressed"; d.status = "Complete";
    m_db->insertDownload(a);
    m_db->insertDownload(b);
    m_db->insertDownload(d);
    loadRecords();

    auto visibleCount = [this](qint64 id) {
        const int row = rowOfId(id);
        return row >= 0 && !m_downloads->isRowHidden(row);
    };
    // sidebar shape: 3 top nodes x 6 children, original id encoding, collapsed
    bool treeOk = m_categories->topLevelItemCount() == 3;
    for (int t = 0; treeOk && t < 3; ++t) {
        const QTreeWidgetItem *top = m_categories->topLevelItem(t);
        treeOk = top->childCount() == 6 && top->data(0, Qt::UserRole).toInt() == (t + 1) * 10
                 && !top->isExpanded() && !top->icon(0).isNull()
                 && top->child(0)->data(0, Qt::UserRole).toInt() == (t + 1) * 10 + 1
                 && !top->child(0)->icon(0).isNull();
    }
    check(treeOk, "category-tree-structure");

    m_filterRole = 20;
    applyCurrentFilter();
    check(visibleCount(1001) && !visibleCount(1002) && visibleCount(1003), "filter-complete");
    m_filterRole = 30;
    applyCurrentFilter();
    check(!visibleCount(1001) && visibleCount(1002), "filter-incomplete");
    m_filterRole = 11;
    applyCurrentFilter();
    check(visibleCount(1001) && !visibleCount(1002) && !visibleCount(1003), "filter-video");
    m_filterRole = 21;   // Complete + Video
    applyCurrentFilter();
    check(visibleCount(1001) && !visibleCount(1003), "filter-complete-video");
    m_filterRole = 10;
    applyCurrentFilter();
    check(visibleCount(1001) && visibleCount(1002) && visibleCount(1003), "filter-all");

    // live language toggle retranslates the main window. Qt posts LanguageChange
    // to widgets from the running event loop; with no loop here, deliver it the
    // way QApplication does, then let any queued follow-ups flush.
    auto flipAndCheck = [this](const QString &want) {
        QEvent langEv(QEvent::LanguageChange);
        QCoreApplication::sendEvent(this, &langEv);
        QCoreApplication::processEvents();
        return m_actDelete->text() == want;
    };
    // direction-agnostic: the persisted preference decides where we start
    const bool startsZh = Settings::instance().language() == QLatin1String("zh");
    const QString flippedText = startsZh ? QString(QLatin1String("Delete"))
                                         : QStringLiteral("删除");
    const QString restoredText = startsZh ? QStringLiteral("删除")
                                          : QString(QLatin1String("Delete"));
    toggleLanguage();
    const bool flipped = flipAndCheck(flippedText);
    toggleLanguage();
    const bool restored = flipAndCheck(restoredText);
    check(flipped && restored, "language-toggle");
    check(m_rows.contains(1001) && m_rows.value(1001).window == nullptr,
          "completed-row-retained");

    // download list shows per-extension file-type icons
    {
        bool iconOk = true;
        for (qint64 id : {qint64(1001), qint64(1002), qint64(1003)}) {
            const int r = rowOfId(id);
            const QTableWidgetItem *ni = r >= 0 ? m_downloads->item(r, ColName) : nullptr;
            iconOk = iconOk && ni && !ni->icon().isNull() && !ni->icon().pixmap(16, 16).isNull();
        }
        check(iconOk, "file-icons");
    }

    // sorting keeps progress data with its row (roles live on items)
    m_downloads->sortByColumn(ColName, Qt::AscendingOrder);
    QCoreApplication::processEvents();
    bool rolesFollow = true;
    for (auto it = m_rows.begin(); it != m_rows.end(); ++it) {
        const int r = rowOfId(it.key());
        const QTableWidgetItem *pi = r >= 0 ? m_downloads->item(r, ColProgress) : nullptr;
        if (!pi || pi->data(ProgressDelegate::TotalRole).toLongLong() != it.value().sizeHint) {
            rolesFollow = false;
            break;
        }
    }
    check(rolesFollow, "sort-keeps-progress");
    m_downloads->sortByColumn(ColLastTry, Qt::DescendingOrder);
    m_db->deleteDownloads({1001, 1002, 1003});
    return ok;
}

void MainWindow::showFromTray()
{
    show();
    setWindowState(windowState() & ~Qt::WindowMinimized | Qt::WindowActive);
    raise();
    activateWindow();
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    // Original behavior: close button hides to notification area.
    if (m_tray && m_tray->isVisible()) {
        hide();
        event->ignore();
        return;
    }
    QMainWindow::closeEvent(event);
}

} // namespace neat
