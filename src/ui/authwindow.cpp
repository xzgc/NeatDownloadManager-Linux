#include "authwindow.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>

namespace neat {

AuthWindow::AuthWindow(const QString &scheme, const QString &host, const QString &realm,
                       QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("%1 Authentication for :").arg(scheme));
    setMinimumWidth(380);

    auto *form = new QFormLayout(this);
    form->addRow(tr("Type"), new QLabel(scheme, this));
    form->addRow(tr("Address"), new QLabel(host, this));
    if (!realm.isEmpty())
        form->addRow(tr("Realm"), new QLabel(realm, this));

    m_user = new QLineEdit(this);
    m_pass = new QLineEdit(this);
    m_pass->setEchoMode(QLineEdit::Password);
    form->addRow(tr("User Name"), m_user);
    form->addRow(tr("Password"), m_pass);

    m_remember = new QCheckBox(tr("Remember"), this);
    form->addRow(QString(), m_remember);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    form->addRow(QString(), buttons);
}

QString AuthWindow::userName() const { return m_user->text(); }
QString AuthWindow::password() const { return m_pass->text(); }
bool AuthWindow::remember() const { return m_remember->isChecked(); }

} // namespace neat
