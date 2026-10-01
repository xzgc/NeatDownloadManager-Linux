#pragma once
#include <QDialog>
#include <QDateTime>

class QLabel;

namespace neat {

struct DownloadRecord;

// Replica of NeatPropertiesWindow: read-only download details with Show Page
// and Open Folder actions.
class PropertiesWindow : public QDialog {
    Q_OBJECT
public:
    PropertiesWindow(const DownloadRecord &rec, QWidget *parent = nullptr);
};

} // namespace neat
