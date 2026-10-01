#pragma once
#include <optional>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVector>
#include <QDateTime>

struct sqlite3;

namespace neat {

struct DownloadRecord {
    qint64 id = 0;
    QString url, method = QStringLiteral("GET"), filename, ltype = QStringLiteral("normal");
    qint64 filesize = 0;
    QString category = QStringLiteral("misc"), status;   // "", Complete, Incomplete, Error...
    qint64 bandwidthlimit = 0;
    int connections = 0;
    qint64 lasttry = 0, firsttry = 0;
    QString useragent;
    bool resumable = false;
    QString pageurl, pagetitle, hittitle, mimetype, errortext, urla, postdata, folderpath;
};

// SQLite persistence, schema identical to the original NeatDB.db
// (downloads / auths / headers).
class Db : public QObject {
public:
    explicit Db(QObject *parent = nullptr);
    ~Db() override;

    bool open(const QString &path);
    bool isOpen() const { return m_db != nullptr; }

    qint64 insertDownload(const DownloadRecord &r);
    bool updateDownload(qint64 id, const QString &field, const QString &value);
    bool updateDownload(qint64 id, const QString &field, qint64 value);
    bool updateStatus(qint64 id, const QString &status, const QString &errText = QString());
    bool touchLastTry(qint64 id);
    bool deleteDownloads(const QVector<qint64> &ids);
    QVector<DownloadRecord> loadAll();
    std::optional<DownloadRecord> findIncompleteByUrl(const QString &url);

    // site credentials (auths)
    QStringList loadCredentials();
    bool findCredential(const QString &target, const QString &proto, QString &user, QString &pass);
    bool saveCredential(const QString &target, const QString &proto, const QString &user,
                        const QString &pass);

    static QString classify(const QString &fileName);

private:
    bool exec(const QString &sql);
    sqlite3 *m_db = nullptr;
};

} // namespace neat
