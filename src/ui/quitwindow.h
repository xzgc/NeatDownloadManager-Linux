#pragma once
#include <QDialog>

namespace neat {

// Replica of NeatQuitWindow: "Total Quit" confirmation while downloads are
// running. Hide = back to tray, Quit = really exit.
class QuitWindow : public QDialog {
    Q_OBJECT
public:
    explicit QuitWindow(QWidget *parent = nullptr);

    bool wantsQuit() const { return m_quit; }

private:
    bool m_quit = false;
};

} // namespace neat
