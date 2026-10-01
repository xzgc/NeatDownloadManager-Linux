#include "downloadwindow.h"

#include "completewindow.h"
#include "core/db.h"
#include "core/hls.h"
#include "core/settings.h"
#include "format.h"
#include "segprogressbar.h"

#include <QCheckBox>
#include <QEvent>
#include <QComboBox>
#include <QFileInfo>
#include <QFormLayout>
#include <QFrame>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QTabWidget>
#include <QTableWidget>
#include <QVBoxLayout>

namespace neat {

// like the original app: on name collision append -1, -2, ... before the suffix
static QString uniqueTarget(const QString &path)
{
    if (!QFileInfo::exists(path))
        return path;
    const QFileInfo fi(path);
    const QString base = fi.absolutePath() + QLatin1Char('/') + fi.completeBaseName();
    const QString suffix = fi.suffix().isEmpty() ? QString()
                                                 : QLatin1Char('.') + fi.suffix();
    QString candidate = path;
    for (int i = 1; QFileInfo::exists(candidate); ++i)
        candidate = base + QLatin1Char('-') + QString::number(i) + suffix;
    return candidate;
}

static QString partPathFor(qint64 id, const QString &destDir)
{
    return destDir + QStringLiteral("/.") + QString::number(id) + QStringLiteral(".neatpart");
}

// small helper: remember (label, field) so retranslate can update label texts
static void neatAddFormRow(QFormLayout *form, const QString &labelText, QWidget *field,
                           QVector<QPair<QLabel *, QString>> *registry)
{
    auto *label = new QLabel(labelText, field->parentWidget());
    form->addRow(label, field);
    registry->append({ label, labelText });
}

DownloadWindow::DownloadWindow(qint64 id, const QUrl &url, const QString &destDir,
                                 QWidget *parent, const QString &fileNameOverride,
                                 const QStringList &extraHeaders, bool hls)
    : QDialog(parent)
    , m_id(id)
    , m_url(url)
    , m_extraHeaders(extraHeaders)
    , m_hlsMode(hls)
{
    m_fileName = fileNameOverride.isEmpty() ? QFileInfo(url.path()).fileName() : fileNameOverride;
    if (m_fileName.isEmpty())
        m_fileName = QStringLiteral("download");
    if (m_hlsMode && !m_fileName.endsWith(QLatin1String(".ts"), Qt::CaseInsensitive))
        m_fileName += QStringLiteral(".ts");
    construct(destDir);
    if (m_hlsMode)
        startHlsEngine();
    else
        startEngine();
}

void DownloadWindow::startHlsEngine()
{
    const QString part = partPathFor(m_id, m_destDir);
    m_hls = new HlsDownloader(m_id, m_url, part, this);
    m_hls->setExtraRequestHeaders(m_extraHeaders);

    connect(m_hls, &HlsDownloader::stateChanged, this, [this] {
        switch (m_hls->state()) {
        case HlsDownloader::State::Probing:
            m_lblStatus->setText(tr("Starting...")); break;
        case HlsDownloader::State::Downloading:
            m_lblStatus->setText(tr("Downloading...")); break;
        case HlsDownloader::State::Paused:
            m_lblStatus->setText(tr("Paused")); break;
        default: break;
        }
        m_btnPause->setText(m_hls->state() == HlsDownloader::State::Paused ? tr("Resume")
                                                                           : tr("Pause"));
        m_lblResumable->setText(tr("No"));
    });
    connect(m_hls, &HlsDownloader::progress, this,
            [this](qint64 done, qint64 total, qint64 bps) {
        m_progress->setRange(0, 0);   // indeterminate overall size
        m_progress->setValue(0);
        setHeadline(done, total, bps);
        refreshHlsInfo();
        emit rowProgressChanged(m_id, done, total);
        emit rowStatusChanged(m_id, tr("Downloading..."), formatSpeed(bps));
    });
    connect(m_hls, &HlsDownloader::segmentsInfo, this, [this](int doneCount, int count) {
        m_lblSegments->setText(tr("Segments : %1  Completed : %2").arg(count).arg(doneCount));
        emit rowSegmentsChanged(m_id, doneCount, count, true);
        m_segments->setHlsMode(true);
        m_segments->setHlsSegments(count, doneCount);
        m_connTable->setRowCount(0);
    });
    connect(m_hls, &HlsDownloader::fetchersInfo, this,
            [this](const QVector<HlsDownloader::FetchInfo> &infos) {
        m_connTable->setRowCount(int(infos.size()));
        for (int i = 0; i < infos.size(); ++i) {
            m_connTable->setItem(i, 0, new QTableWidgetItem(QString::number(infos[i].index + 1)));
            m_connTable->setItem(i, 1, new QTableWidgetItem(QStringLiteral("TS #%1").arg(infos[i].index + 1)));
            m_connTable->setItem(i, 2, new QTableWidgetItem(QStringLiteral(" ")));
            m_connTable->setItem(i, 3, new QTableWidgetItem(infos[i].state));
        }
    });
    connect(m_hls, &HlsDownloader::finished, this, [this](bool ok, const QString &err) {
        QString finalPath;
        QString errorText = err;
        if (ok) {
            finalPath = uniqueTarget(m_destDir + QStringLiteral("/") + m_fileName);
            if (m_hls->finishToFile(finalPath)) {
                m_fileName = QFileInfo(finalPath).fileName();
                setWindowTitle(m_fileName);
            } else {
                ok = false;
                errorText = tr("Failed To Move Completed File.");
            }
        }
        m_lblStatus->setText(ok ? tr("Completed.") : errorText);
        if (ok)
            setHeadline(m_engine ? m_engine->totalBytes() : 0,
                        m_engine ? m_engine->totalBytes() : 0, 0);
        if (!ok && !errorText.isEmpty())
            emit engineError(errorText);
        emit rowStatusChanged(m_id, ok ? tr("Complete") : tr("Error"), QStringLiteral(" "));
        emit downloadFinished(m_id, ok, finalPath);
        if (ok && m_chkCompletion && m_chkCompletion->isChecked())
            (new CompleteWindow(m_fileName, finalPath, this))->show();
        close();
    });

    onEngineState();
    m_hls->start();
}

void DownloadWindow::refreshHlsInfo()
{
    if (!m_hls)
        return;
    m_lblFileSize->setText(tr("Unknown"));
    m_lblDownloaded->setText(formatBytes(m_hls->totalBytes()));
}

DownloadWindow::DownloadWindow(const DownloadRecord &rec, QWidget *parent)
    : QDialog(parent)
    , m_id(rec.id)
    , m_url(rec.url)
    , m_fileName(rec.filename)
{
    construct(rec.folderpath.isEmpty() ? Settings::instance().downloadDirectory() : rec.folderpath);
    startEngine();
}

bool DownloadWindow::isWorking() const
{
    if (m_hls)
        return m_hls->state() == HlsDownloader::State::Downloading
            || m_hls->state() == HlsDownloader::State::Probing
            || m_hls->state() == HlsDownloader::State::Paused;
    return m_engine
        && (m_engine->state() == DownloadEngine::State::Downloading
            || m_engine->state() == DownloadEngine::State::Probing
            || m_engine->state() == DownloadEngine::State::Paused);
}

void DownloadWindow::construct(const QString &destDir)
{
    m_destDir = destDir;
    setWindowTitle(m_fileName);
    setAttribute(Qt::WA_DeleteOnClose);
    setMinimumSize(620, 460);

    auto *root = new QVBoxLayout(this);
    m_tabs = new QTabWidget(this);
    buildDownloadTab();
    buildConnectionsTab();
    buildOptionsTab();
    root->addWidget(m_tabs, 1);

    auto *buttons = new QHBoxLayout;
    m_btnPause = new QPushButton(tr("Pause"), this);
    m_btnPause->setFixedSize(80, 27);
    m_btnCancel = new QPushButton(tr("Cancel"), this);
    buttons->addStretch(1);
    buttons->addWidget(m_btnPause);
    buttons->addWidget(m_btnCancel);
    root->addLayout(buttons);

    connect(m_btnPause, &QPushButton::clicked, this, [this] {
        if (m_hls) {
            if (m_hls->state() == HlsDownloader::State::Downloading
                || m_hls->state() == HlsDownloader::State::Probing)
                m_hls->pause();
            else if (m_hls->state() == HlsDownloader::State::Paused)
                m_hls->resume();
            return;
        }
        if (!m_engine)
            return;
        switch (m_engine->state()) {
        case DownloadEngine::State::Downloading:
        case DownloadEngine::State::Probing:
            m_engine->pause();
            break;
        case DownloadEngine::State::Paused:
            m_engine->resume();
            break;
        default:
            break;
        }
    });
    connect(m_btnCancel, &QPushButton::clicked, this, [this] {
        if (m_hls)
            m_hls->cancel();
        else if (m_engine)
            m_engine->cancel();
        close();
    });
}

void DownloadWindow::buildDownloadTab()
{
    auto *page = new QWidget(this);
    auto *form = new QFormLayout();
    m_formDownload = form;
    form->setContentsMargins(0, 4, 0, 0);
    auto mk = [page](const QString &init = QStringLiteral(" ")) {
        auto *l = new QLabel(init, page);
        l->setTextInteractionFlags(Qt::TextSelectableByMouse);
        return l;
    };
    m_lblStatus = mk();
    m_lblFileSize = mk();
    m_lblDownloaded = mk();
    m_lblBandwidth = mk();
    m_lblRemain = mk();
    m_lblResumable = mk();
    m_lblSegments = mk(QStringLiteral("Segments : 0  Completed : 0"));

    // progress block spans the full tab width: headline percent + summary,
    // overall capsule bar, segment cell map, caption — then the info form
    m_lblBigPct = new QLabel(QStringLiteral("0%"), page);
    QFont big = m_lblBigPct->font();
    big.setPointSizeF(big.pointSizeF() * 2.1);
    big.setBold(true);
    m_lblBigPct->setFont(big);
    m_lblBigPct->setStyleSheet(QStringLiteral("color:#1F2328;"));
    m_lblSummary = new QLabel(QStringLiteral(" "), page);
    m_lblSummary->setStyleSheet(QStringLiteral("color:#6B7280;"));
    auto *head = new QHBoxLayout;
    head->addWidget(m_lblBigPct);
    head->addStretch(1);
    head->addWidget(m_lblSummary, 0, Qt::AlignBottom | Qt::AlignRight);

    m_progress = new QProgressBar(page);
    m_progress->setTextVisible(false);
    m_progress->setFixedHeight(16);

    m_segments = new SegmentsProgressBar(page);
    m_segments->setFixedHeight(26);

    auto *lay = new QVBoxLayout(page);
    lay->setContentsMargins(14, 12, 14, 12);
    lay->setSpacing(8);
    lay->addLayout(head);
    lay->addWidget(m_progress);
    lay->addWidget(m_segments);
    lay->addWidget(m_lblSegments);
    auto *sep = new QFrame(page);
    sep->setFrameShape(QFrame::HLine);
    sep->setStyleSheet(QStringLiteral("color:#E3E5E8;"));
    lay->addWidget(sep);
    lay->addLayout(form);
    lay->addStretch(1);

    neatAddFormRow(form, tr("Status"), m_lblStatus, &m_formLabels);
    neatAddFormRow(form, tr("File Size"), m_lblFileSize, &m_formLabels);
    neatAddFormRow(form, tr("Downloaded"), m_lblDownloaded, &m_formLabels);
    neatAddFormRow(form, tr("Bandwidth"), m_lblBandwidth, &m_formLabels);
    neatAddFormRow(form, tr("Remaining Time"), m_lblRemain, &m_formLabels);
    neatAddFormRow(form, tr("Resumable"), m_lblResumable, &m_formLabels);

    m_tabs->addTab(page, tr(" Download "));
}

void DownloadWindow::buildConnectionsTab()
{
    auto *page = new QWidget(this);
    auto *lay = new QVBoxLayout(page);
    m_formLabels.clear();
    m_connTable = new QTableWidget(0, 4, page);
    m_connTable->setHorizontalHeaderLabels(
        {tr("Connection"), tr("Range"), tr("Downloaded"), tr("Status")});
    m_connTable->horizontalHeader()->setStretchLastSection(true);
    m_connTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_connTable->verticalHeader()->hide();
    lay->addWidget(m_connTable);
    m_tabs->addTab(page, tr(" Connections "));
}

void DownloadWindow::buildOptionsTab()
{
    auto *page = new QWidget(this);
    auto *lay = new QVBoxLayout(page);

    auto *bwRow = new QHBoxLayout;
    bwRow->addWidget(new QLabel(tr("Limit Bandwidth to "), page));
    m_editBandwidth = new QLineEdit(page);
    m_editBandwidth->setFixedWidth(60);
    m_editBandwidth->setPlaceholderText(tr("0 or Blank = No Limit"));
    bwRow->addWidget(m_editBandwidth);
    bwRow->addWidget(new QLabel(tr("KB/sec"), page));
    auto *btnApplyBw = new QPushButton(tr("Apply"), page);
    btnApplyBw->setFixedSize(50, 20);
    bwRow->addWidget(btnApplyBw);
    bwRow->addStretch(1);
    lay->addLayout(bwRow);
    connect(btnApplyBw, &QPushButton::clicked, this, &DownloadWindow::applyBandwidth);

    auto *connRow = new QHBoxLayout;
    connRow->addWidget(new QLabel(tr("Connections"), page));
    m_comboConnections = new QComboBox(page);
    for (int i = 1; i <= DownloadEngine::kMaxConnections; ++i)
        m_comboConnections->addItem(QString::number(i));
    m_comboConnections->setCurrentIndex(
        qMax(0, Settings::instance().maxConnections() - 1));
    m_comboConnections->setFixedHeight(17);
    connRow->addWidget(m_comboConnections);
    auto *btnApplyConn = new QPushButton(tr("Apply"), page);
    btnApplyConn->setFixedSize(50, 20);
    connRow->addWidget(btnApplyConn);
    connRow->addStretch(1);
    lay->addLayout(connRow);
    connect(btnApplyConn, &QPushButton::clicked, this, &DownloadWindow::applyConnections);

    m_chkCompletion = new QCheckBox(tr("Show Completion Dialog"), page);
    m_chkCompletion->setChecked(Settings::instance().completionDialog());
    lay->addWidget(m_chkCompletion);

    m_chkRemember = new QCheckBox(tr("Remember on Resume"), page);
    lay->addWidget(m_chkRemember);

    lay->addStretch(1);
    m_tabs->addTab(page, tr(" Options "));
}

void DownloadWindow::startEngine()
{
    const QString part = partPathFor(m_id, m_destDir);
    m_engine = new DownloadEngine(m_id, m_url, part, this);
    if (!m_extraHeaders.isEmpty())
        m_engine->setExtraRequestHeaders(m_extraHeaders);

    connect(m_engine, &DownloadEngine::stateChanged, this, [this] { onEngineState(); });
    connect(m_engine, &DownloadEngine::progress, this, [this](qint64 done, qint64 total, qint64 bps) {
        m_progress->setRange(0, total > 0 ? 100 : 0);
        m_progress->setValue(total > 0 ? int(done * 100 / total) : 0);
        setHeadline(done, total, bps);
        refreshInfo();
        emit rowProgressChanged(m_id, done, total);
        emit rowStatusChanged(m_id, tr("Downloading..."), formatSpeed(bps));
    });
    connect(m_engine, &DownloadEngine::connectionsInfo, this,
            [this](const QVector<DownloadEngine::ConnInfo> &infos) {
                m_connTable->setRowCount(int(infos.size()));
                int doneCount = 0;
                for (int i = 0; i < infos.size(); ++i) {
                    const auto &ci = infos[i];
                    if (ci.state == tr("Completed"))
                        ++doneCount;
                    m_connTable->setItem(
                        i, 0, new QTableWidgetItem(QString::number(ci.id)));
                    m_connTable->setItem(
                        i, 1,
                        new QTableWidgetItem(QStringLiteral("%1 - %2")
                                                  .arg(formatBytes(ci.start))
                                                  .arg(formatBytes(ci.end + 1))));
                    m_connTable->setItem(i, 2, new QTableWidgetItem(formatBytes(ci.downloaded)));
                    m_connTable->setItem(i, 3, new QTableWidgetItem(ci.state));
                }
                const int total = int(m_engine->segments().size());
                m_lblSegments->setText(tr("Segments : %1  Completed : %2").arg(total).arg(doneCount));
                emit rowSegmentsChanged(m_id, doneCount, total, false);
                std::vector<SegmentsProgressBar::Segment> view;
                for (const Segment &s : m_engine->segments())
                    view.push_back({s.start, s.end + 1, s.downloaded, s.active});
                m_segments->setSegments(view);
            });
    connect(m_engine, &DownloadEngine::finished, this, [this](bool ok, const QString &err) {
        QString finalPath;
        QString errorText = err;
        if (ok) {
            finalPath = uniqueTarget(m_destDir + QStringLiteral("/") + m_fileName);
            if (m_engine->finishToFile(finalPath)) {
                m_fileName = QFileInfo(finalPath).fileName();
                setWindowTitle(m_fileName);
            } else {
                ok = false;
                errorText = tr("Failed To Move Completed File.");
            }
        }
        m_lblStatus->setText(ok ? tr("Completed.") : errorText);
        if (!ok && !errorText.isEmpty())
            emit engineError(errorText);
        emit rowStatusChanged(m_id, ok ? tr("Complete") : tr("Error"), QStringLiteral(" "));
        emit downloadFinished(m_id, ok, finalPath);
        if (ok && m_chkCompletion && m_chkCompletion->isChecked())
            (new CompleteWindow(m_fileName, finalPath, this))->show();
        close();
    });

    onEngineState();
    m_engine->start();
    emit metadataKnown(m_id, 0, m_comboConnections ? m_comboConnections->currentIndex() + 1 : 0,
                       false);
}

void DownloadWindow::applyResumeSettings(qint64 bandwidthKb, int connections)
{
    if (bandwidthKb > 0) {
        if (m_engine)
            m_engine->setBandwidthLimitKb(bandwidthKb);
        if (m_editBandwidth)
            m_editBandwidth->setText(QString::number(bandwidthKb));
    }
    if (connections > 0 && m_comboConnections)
        m_comboConnections->setCurrentIndex(qBound(0, connections - 1,
                                                   DownloadEngine::kMaxConnections - 1));
}

void DownloadWindow::resumeEngine()
{
    if (m_hls) {
        if (m_hls->state() == HlsDownloader::State::Paused)
            m_hls->resume();
        return;
    }
    if (m_engine && m_engine->state() == DownloadEngine::State::Paused)
        m_engine->resume();
}

void DownloadWindow::stopEngine()
{
    if (m_hls
        && (m_hls->state() == HlsDownloader::State::Downloading
            || m_hls->state() == HlsDownloader::State::Probing)) {
        m_hls->pause();
        return;
    }
    if (m_engine
        && (m_engine->state() == DownloadEngine::State::Downloading
            || m_engine->state() == DownloadEngine::State::Probing))
        m_engine->pause();
}

void DownloadWindow::applyDefaultBandwidth(qint64 kb)
{
    if (m_hls)
        m_hls->setBandwidthLimitKb(kb);
    if (m_engine)
        m_engine->setBandwidthLimitKb(kb);
    m_editBandwidth->setText(QString::number(kb));
}

void DownloadWindow::changeEvent(QEvent *e)
{
    if (e->type() == QEvent::LanguageChange)
        retranslateUi();
    QDialog::changeEvent(e);
}

void DownloadWindow::retranslateUi()
{
    setWindowTitle(m_fileName);
    if (m_tabs) {
        m_tabs->setTabText(0, tr(" Download "));
        m_tabs->setTabText(1, tr(" Connections "));
        m_tabs->setTabText(2, tr(" Options "));
    }
    if (m_connTable)
        m_connTable->setHorizontalHeaderLabels(
            { tr("Connection"), tr("Range"), tr("Downloaded"), tr("Status") });
    if (m_btnPause)
        m_btnPause->setText(tr("Pause"));
    if (m_btnCancel)
        m_btnCancel->setText(tr("Cancel"));
    if (m_lblFileSize)
        m_lblFileSize->setText(m_engine && m_engine->totalBytes() > 0
                                   ? formatBytes(m_engine->totalBytes())
                                   : tr("Unknown"));
    if (m_lblResumable) {
        const bool res = m_engine ? m_engine->resumable() : false;
        m_lblResumable->setText(res ? tr("Yes") : tr("No"));
    }
    // retranslate registered form labels by source text
    for (auto &pair : m_formLabels)
        pair.first->setText(tr(pair.second.toUtf8().constData()));
}

void DownloadWindow::onEngineState()
{
    if (m_hls && !m_engine)
        return;
    switch (m_engine->state()) {
    case DownloadEngine::State::Probing:
        m_lblStatus->setText(tr("Starting..."));
        m_btnPause->setText(tr("Pause"));
        break;
    case DownloadEngine::State::Downloading:
        m_lblStatus->setText(tr("Downloading..."));
        m_btnPause->setText(tr("Pause"));
        break;
    case DownloadEngine::State::Paused:
        m_lblStatus->setText(tr("Paused"));
        m_btnPause->setText(tr("Resume"));
        break;
    default:
        break;
    }
    m_lblResumable->setText(m_engine->resumable() ? tr("Yes") : tr("No"));
    if (m_engine->totalBytes() > 0)
        emit metadataKnown(m_id, m_engine->totalBytes(),
                           m_engine->targetConnections(), m_engine->resumable());
    refreshInfo();
}

void DownloadWindow::setHeadline(qint64 done, qint64 total, qint64 bps)
{
    const bool complete = total > 0 && done >= total;
    const int percent = complete ? 100 : (total > 0 ? int(done * 100 / total) : 0);
    m_lblBigPct->setText(QString::number(percent) + QLatin1Char('%'));
    m_lblBigPct->setStyleSheet(complete
                                   ? QStringLiteral("color:#1E8449;")
                                   : QStringLiteral("color:#1F2328;"));
    QString summary = formatBytes(done);
    if (total > 0)
        summary += QStringLiteral(" / ") + formatBytes(total);
    if (bps > 0)
        summary += QStringLiteral("   ") + formatSpeed(bps);
    m_lblSummary->setText(summary);
}

void DownloadWindow::refreshInfo()
{
    if (!m_engine)
        return;
    const qint64 total = m_engine->totalBytes();
    const qint64 done = m_engine->downloadedBytes();
    m_lblFileSize->setText(total > 0 ? formatBytes(total) : tr("Unknown"));
    m_lblDownloaded->setText(formatBytes(done));
    if (m_engine->state() == DownloadEngine::State::Downloading && m_engine->speed() > 0
        && total > done)
        m_lblRemain->setText(formatRemaining(total - done, m_engine->speed()));
}

void DownloadWindow::applyBandwidth()
{
    const qint64 kb = m_editBandwidth->text().trimmed().toLongLong();
    if (m_hls)
        m_hls->setBandwidthLimitKb(qMax<qint64>(0, kb));
    if (m_engine)
        m_engine->setBandwidthLimitKb(qMax<qint64>(0, kb));
    if (m_chkRemember && m_chkRemember->isChecked())
        emit optionsChanged(m_id, qMax<qint64>(0, kb), 0);
}

void DownloadWindow::applyConnections()
{
    const int n = m_comboConnections->currentIndex() + 1;
    if (m_engine)
        m_engine->setConnectionCount(n);
    if (m_chkRemember && m_chkRemember->isChecked())
        emit optionsChanged(m_id, -1, n);
}

} // namespace neat
