#pragma once
#include <QDialog>

class QLineEdit;

namespace neat {

// Replica of NeatUrlWindow: a URL text box with Download / Cancel.
class UrlWindow : public QDialog {
    Q_OBJECT
public:
    explicit UrlWindow(QWidget *parent = nullptr);

    QString url() const;

private:
    QLineEdit *m_edit = nullptr;
};

} // namespace neat
