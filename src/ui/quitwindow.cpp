#include "quitwindow.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

namespace neat {

QuitWindow::QuitWindow(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Quit NeatDownloadManager"));

    auto *root = new QVBoxLayout(this);
    auto *title = new QLabel(tr("Download In Progress"), this);
    title->setStyleSheet(QStringLiteral("font-weight: bold;"));
    root->addWidget(title);
    root->addWidget(new QLabel(
        tr("You can hide NeatDownloadManager in Notification Area by clicking on MainWindow "
           "close button and then bring it to front by clicking on Status Menu Item.\n"
           "Are you sure you want to Quit Totally ?"),
        this));

    auto *hide = new QPushButton(tr("Hide"), this);
    auto *quit = new QPushButton(tr("Quit"), this);
    auto *row = new QHBoxLayout;
    row->addStretch(1);
    row->addWidget(hide);
    row->addWidget(quit);
    root->addLayout(row);

    connect(hide, &QPushButton::clicked, this, [this] {
        m_quit = false;
        reject();
    });
    connect(quit, &QPushButton::clicked, this, [this] {
        m_quit = true;
        accept();
    });
}

} // namespace neat
