#pragma once
#include <QDialog>

class QCheckBox;
class QLabel;

namespace neat {

// Replica of NeatBrowsersWindow: Chrome / Firefox / Edge cards with install
// status, "Add X Extension" (opens the store page) and the per-browser
// media-panel toggle (persisted + pushed to connected extensions).
class BrowsersWindow : public QDialog {
    Q_OBJECT
public:
    explicit BrowsersWindow(QWidget *parent = nullptr);

private:
    void buildCard(const QString &name, const QString &exe, const QString &panelKey,
                   const QString &storeUrl);
};

} // namespace neat
