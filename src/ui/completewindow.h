#pragma once
#include <QDialog>

class QLabel;

namespace neat {

// Replica of NeatCompleteWindow: file icon + name + size with
// Open / Open Folder / Close buttons.
class CompleteWindow : public QDialog {
    Q_OBJECT
public:
    CompleteWindow(const QString &fileName, const QString &filePath, QWidget *parent = nullptr);

private:
    QLabel *m_name = nullptr;
    QLabel *m_size = nullptr;
};

} // namespace neat
