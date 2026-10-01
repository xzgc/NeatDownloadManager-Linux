#include "propertieswindow.h"

#include "core/db.h"
#include "format.h"

#include <QDesktopServices>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QUrl>
#include <QVBoxLayout>

namespace neat {

static QString dateOf(qint64 secs)
{
    return QDateTime::fromSecsSinceEpoch(secs)
        .toString(QStringLiteral("MMM d  H:mm:ss yyyy"));
}

PropertiesWindow::PropertiesWindow(const DownloadRecord &rec, QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Download Properties"));
    setMinimumWidth(480);

    auto *root = new QVBoxLayout(this);
    auto *form = new QFormLayout;

    auto ro = [](const QString &text, QWidget *p) {
        auto *e = new QLineEdit(text, p);
        e->setReadOnly(true);
        return e;
    };
    form->addRow(tr("File Name"), ro(rec.filename, this));
    form->addRow(tr("File Size"),
                 ro(rec.filesize > 0 ? formatBytes(rec.filesize) : tr("Unknown"), this));
    form->addRow(tr("Status"), ro(rec.status, this));
    form->addRow(tr("Page Title"), ro(rec.pagetitle, this));
    form->addRow(tr("URL"), ro(rec.url, this));
    form->addRow(tr("Folder"), ro(rec.folderpath, this));
    form->addRow(tr("Added"), ro(dateOf(rec.firsttry), this));
    form->addRow(tr("Last Try"), ro(dateOf(rec.lasttry), this));
    form->addRow(tr("Error"), ro(rec.errortext, this));
    root->addLayout(form);

    auto *page = new QPushButton(tr("Show Page"), this);
    auto *folder = new QPushButton(tr("Open Folder"), this);
    auto *ok = new QPushButton(tr("OK"), this);
    auto *row = new QHBoxLayout;
    row->addStretch(1);
    row->addWidget(page);
    row->addWidget(folder);
    row->addWidget(ok);
    root->addLayout(row);

    connect(page, &QPushButton::clicked, this,
            [rec] { QDesktopServices::openUrl(QUrl(rec.pageurl)); });
    connect(folder, &QPushButton::clicked, this, [rec] {
        QDesktopServices::openUrl(QUrl::fromLocalFile(rec.folderpath));
    });
    connect(ok, &QPushButton::clicked, this, &QDialog::close);
}

} // namespace neat
