#pragma once
#include <QDialog>

class QLineEdit;
class QCheckBox;
class QLabel;

namespace neat {

// Replica of NeatAuthWindow: shows scheme/host/realm, asks for user+password,
// optional "Remember" (persists to the auths table).
class AuthWindow : public QDialog {
    Q_OBJECT
public:
    AuthWindow(const QString &scheme, const QString &host, const QString &realm,
               QWidget *parent = nullptr);

    QString userName() const;
    QString password() const;
    bool remember() const;

private:
    QLineEdit *m_user = nullptr;
    QLineEdit *m_pass = nullptr;
    QCheckBox *m_remember = nullptr;
};

} // namespace neat
