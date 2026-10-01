#include "urlwindow.h"

#include <QDialogButtonBox>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QUrl>
#include <QVBoxLayout>

namespace neat {

UrlWindow::UrlWindow(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("New URL"));
    setMinimumWidth(520);

    auto *layout = new QVBoxLayout(this);
    m_edit = new QLineEdit(this);
    m_edit->setPlaceholderText(tr("http://  or  https://  or  ftp://"));
    layout->addWidget(new QLabel(tr("URL :"), this));
    layout->addWidget(m_edit);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    buttons->button(QDialogButtonBox::Ok)->setText(tr("Download"));
    buttons->button(QDialogButtonBox::Cancel)->setText(tr("Cancel"));
    connect(buttons, &QDialogButtonBox::accepted, this, [this] {
        const QUrl u(m_edit->text().trimmed());
        if (u.scheme() == QLatin1String("http") || u.scheme() == QLatin1String("https"))
            accept();
        else
            m_edit->setStyleSheet(QStringLiteral("color: red;"));
    });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);
}

QString UrlWindow::url() const
{
    return m_edit->text().trimmed();
}

} // namespace neat
