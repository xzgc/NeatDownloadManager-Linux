#include "completewindow.h"

#include "format.h"

#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QDesktopServices>
#include <QPushButton>
#include <QStyle>
#include <QUrl>
#include <QVBoxLayout>

namespace neat {

CompleteWindow::CompleteWindow(const QString &fileName, const QString &filePath, QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Download Completed"));
    setAttribute(Qt::WA_DeleteOnClose);
    setMinimumWidth(420);

    auto *root = new QVBoxLayout(this);

    auto *top = new QHBoxLayout;
    auto *icon = new QLabel(this);
    const QFileInfo info(filePath);
    icon->setPixmap(info.isExecutable()
                        ? style()->standardIcon(QStyle::SP_FileDialogContentsView).pixmap(48)
                        : style()->standardIcon(QStyle::SP_FileIcon).pixmap(48));
    auto *col = new QVBoxLayout;
    m_name = new QLabel(fileName, this);
    m_name->setWordWrap(true);
    m_size = new QLabel(formatBytes(info.size()), this);
    col->addWidget(m_name);
    col->addWidget(m_size);
    top->addWidget(icon);
    top->addLayout(col);
    top->addStretch(1);
    root->addLayout(top);

    auto *buttons = new QHBoxLayout;
    buttons->addStretch(1);
    auto *btnOpen = new QPushButton(tr("Open"), this);
    auto *btnFolder = new QPushButton(tr("Open Folder"), this);
    auto *btnClose = new QPushButton(tr("Close"), this);
    buttons->addWidget(btnOpen);
    buttons->addWidget(btnFolder);
    buttons->addWidget(btnClose);
    root->addLayout(buttons);

    connect(btnOpen, &QPushButton::clicked, this,
            [filePath] { QDesktopServices::openUrl(QUrl::fromLocalFile(filePath)); });
    connect(btnFolder, &QPushButton::clicked, this, [filePath] {
        QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(filePath).absolutePath()));
    });
    connect(btnClose, &QPushButton::clicked, this, &QDialog::close);
}

} // namespace neat
