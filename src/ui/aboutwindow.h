#pragma once
#include <QDialog>

class QLabel;
class QPushButton;

namespace neat {

// Replica of NeatAboutWindow: version, website, contact and a Check For
// Update button that fetches the vendor's version file (through our own
// single-connection engine) and compares markers.
class AboutWindow : public QDialog {
    Q_OBJECT
public:
    explicit AboutWindow(QWidget *parent = nullptr);

private:
    void checkForUpdate();
    QLabel *m_status = nullptr;
    QPushButton *m_check = nullptr;
    bool m_canDownload = false;
};

} // namespace neat
