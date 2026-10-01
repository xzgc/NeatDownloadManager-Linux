#include "db.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <sqlite3.h>

namespace neat {

Db::Db(QObject *parent)
    : QObject(parent)
{
}

Db::~Db()
{
    if (m_db)
        sqlite3_close(m_db);
}

bool Db::open(const QString &path)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    if (sqlite3_open(path.toUtf8().constData(), &m_db) != SQLITE_OK)
        return false;
    sqlite3_busy_timeout(m_db, 3000);
    const char *schema =
        "CREATE TABLE IF NOT EXISTS downloads ("
        " id INTEGER PRIMARY KEY,"
        " url TEXT, method TEXT, filename TEXT, ltype TEXT,"
        " filesize NUMERIC, category TEXT, status TEXT,"
        " bandwidthlimit NUMERIC, connections NUMERIC,"
        " lasttry NUMERIC, firsttry NUMERIC,"
        " useragent TEXT, resumable NUMERIC,"
        " pageurl TEXT, pagetitle TEXT, hittitle TEXT,"
        " mimetype TEXT, errortext TEXT, urla TEXT, postdata TEXT,"
        " folderpath TEXT);"
        "CREATE TABLE IF NOT EXISTS auths (id INTEGER PRIMARY KEY AUTOINCREMENT,"
        " target TEXT, protocol TEXT, user TEXT, pass TEXT);"
        "CREATE TABLE IF NOT EXISTS headers (id NUMERIC, header TEXT);"
        "ALTER TABLE downloads ADD COLUMN temppath TEXT DEFAULT '';";
    char *err = nullptr;
    if (sqlite3_exec(m_db, schema, nullptr, nullptr, &err) != SQLITE_OK) {
        // ALTER fails once the column exists; tolerate that one.
        const QString e = err ? QString::fromUtf8(err) : QString();
        sqlite3_free(err);
        if (!e.contains(QStringLiteral("duplicate column"), Qt::CaseInsensitive))
            return false;
    }
    return true;
}

bool Db::exec(const QString &sql)
{
    char *err = nullptr;
    if (sqlite3_exec(m_db, sql.toUtf8().constData(), nullptr, nullptr, &err) != SQLITE_OK) {
        sqlite3_free(err);
        return false;
    }
    return true;
}

static void bindText(sqlite3_stmt *st, int idx, const QString &v)
{
    sqlite3_bind_text(st, idx, v.toUtf8().constData(), int(v.toUtf8().size()), SQLITE_TRANSIENT);
}

qint64 Db::insertDownload(const DownloadRecord &r)
{
    sqlite3_stmt *st = nullptr;
    const char *sql =
        "INSERT INTO downloads(id,url,method,filename,ltype,filesize,category,status,"
        "bandwidthlimit,connections,lasttry,firsttry,useragent,resumable,pageurl,pagetitle,"
        "hittitle,mimetype,errortext,urla,postdata,folderpath) "
        "VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)";
    if (sqlite3_prepare_v2(m_db, sql, -1, &st, nullptr) != SQLITE_OK)
        return -1;
    sqlite3_bind_int64(st, 1, r.id);
    bindText(st, 2, r.url);
    bindText(st, 3, r.method);
    bindText(st, 4, r.filename);
    bindText(st, 5, r.ltype);
    sqlite3_bind_int64(st, 6, r.filesize);
    bindText(st, 7, r.category);
    bindText(st, 8, r.status);
    sqlite3_bind_int64(st, 9, r.bandwidthlimit);
    sqlite3_bind_int64(st, 10, r.connections);
    sqlite3_bind_int64(st, 11, r.lasttry);
    sqlite3_bind_int64(st, 12, r.firsttry);
    bindText(st, 13, r.useragent);
    sqlite3_bind_int(st, 14, r.resumable ? 1 : 0);
    bindText(st, 15, r.pageurl);
    bindText(st, 16, r.pagetitle);
    bindText(st, 17, r.hittitle);
    bindText(st, 18, r.mimetype);
    bindText(st, 19, r.errortext);
    bindText(st, 20, r.urla);
    bindText(st, 21, r.postdata);
    bindText(st, 22, r.folderpath);
    const int rc = sqlite3_step(st);
    sqlite3_finalize(st);
    return rc == SQLITE_DONE ? r.id : -1;
}

bool Db::updateDownload(qint64 id, const QString &field, const QString &value)
{
    static const QStringList kAllowed = {"url",     "method",  "filename", "ltype",   "category",
                                         "status",  "useragent", "pageurl", "pagetitle",
                                         "hittitle", "mimetype", "errortext", "urla",
                                         "postdata", "folderpath"};
    if (!kAllowed.contains(field))
        return false;
    QString safeValue = value;
    return exec(QStringLiteral("UPDATE downloads SET %1='%2' WHERE id=%3")
                    .arg(field, safeValue.replace(QLatin1Char('\''), QStringLiteral("''")))
                    .arg(id));
}

bool Db::updateDownload(qint64 id, const QString &field, qint64 value)
{
    static const QStringList kAllowed = {"filesize", "bandwidthlimit", "connections",
                                         "lasttry", "firsttry", "resumable"};
    if (!kAllowed.contains(field))
        return false;
    return exec(QStringLiteral("UPDATE downloads SET %1=%2 WHERE id=%3").arg(field).arg(value).arg(id));
}

bool Db::updateStatus(qint64 id, const QString &status, const QString &errText)
{
    const qint64 now = QDateTime::currentSecsSinceEpoch();
    QString safeStatus = status;
    QString safeErr = errText;
    QString sql = QStringLiteral("UPDATE downloads SET status='%1', lasttry=%2")
                      .arg(safeStatus.replace(QLatin1Char('\''), QStringLiteral("''"))).arg(now);
    if (!errText.isNull())
        sql += QStringLiteral(", errortext='%1'")
                   .arg(safeErr.replace(QLatin1Char('\''), QStringLiteral("''")));
    sql += QStringLiteral(" WHERE id=%1").arg(id);
    return exec(sql);
}

bool Db::touchLastTry(qint64 id)
{
    return exec(QStringLiteral("UPDATE downloads SET lasttry=%1 WHERE id=%2")
                    .arg(QDateTime::currentSecsSinceEpoch()).arg(id));
}

bool Db::deleteDownloads(const QVector<qint64> &ids)
{
    QStringList parts;
    for (qint64 id : ids)
        parts << QString::number(id);
    if (parts.isEmpty())
        return true;
    const QString in = parts.join(QLatin1Char(','));
    return exec(QStringLiteral("DELETE FROM downloads WHERE id IN (%1)").arg(in))
        && exec(QStringLiteral("DELETE FROM headers WHERE id IN (%1)").arg(in));
}

static DownloadRecord rowToRecord(sqlite3_stmt *st)
{
    DownloadRecord r;
    auto text = [&](int col) {
        const unsigned char *p = sqlite3_column_text(st, col);
        return p ? QString::fromUtf8(reinterpret_cast<const char *>(p)) : QString();
    };
    r.id = sqlite3_column_int64(st, 0);
    r.url = text(1);
    r.method = text(2);
    r.filename = text(3);
    r.ltype = text(4);
    r.filesize = sqlite3_column_int64(st, 5);
    r.category = text(6);
    r.status = text(7);
    r.bandwidthlimit = sqlite3_column_int64(st, 8);
    r.connections = sqlite3_column_int(st, 9);
    r.lasttry = sqlite3_column_int64(st, 10);
    r.firsttry = sqlite3_column_int64(st, 11);
    r.useragent = text(12);
    r.resumable = sqlite3_column_int(st, 13) != 0;
    r.pageurl = text(14);
    r.pagetitle = text(15);
    r.hittitle = text(16);
    r.mimetype = text(17);
    r.errortext = text(18);
    r.urla = text(19);
    r.postdata = text(20);
    r.folderpath = text(21);
    return r;
}

QVector<DownloadRecord> Db::loadAll()
{
    QVector<DownloadRecord> out;
    sqlite3_stmt *st = nullptr;
    if (sqlite3_prepare_v2(m_db, "SELECT * FROM downloads ORDER BY lasttry DESC", -1, &st, nullptr) != SQLITE_OK)
        return out;
    while (sqlite3_step(st) == SQLITE_ROW)
        out.append(rowToRecord(st));
    sqlite3_finalize(st);
    return out;
}

std::optional<DownloadRecord> Db::findIncompleteByUrl(const QString &url)
{
    sqlite3_stmt *st = nullptr;
    const QString sql = QStringLiteral(
        "SELECT * FROM downloads WHERE url='%1' AND status<>'Complete' ORDER BY lasttry DESC LIMIT 1")
        .arg(QString(url).replace(QLatin1Char('\''), QStringLiteral("''")));
    if (sqlite3_prepare_v2(m_db, sql.toUtf8().constData(), -1, &st, nullptr) != SQLITE_OK)
        return std::nullopt;
    std::optional<DownloadRecord> r;
    if (sqlite3_step(st) == SQLITE_ROW)
        r = rowToRecord(st);
    sqlite3_finalize(st);
    return r;
}

QStringList Db::loadCredentials()
{
    QStringList out;
    sqlite3_stmt *st = nullptr;
    if (sqlite3_prepare_v2(m_db, "SELECT target,protocol,user FROM auths", -1, &st, nullptr) != SQLITE_OK)
        return out;
    while (sqlite3_step(st) == SQLITE_ROW) {
        const unsigned char *t = sqlite3_column_text(st, 0);
        const unsigned char *p = sqlite3_column_text(st, 1);
        const unsigned char *u = sqlite3_column_text(st, 2);
        out << QString::fromUtf8((const char *)t) + QLatin1Char('|')
               + QString::fromUtf8((const char *)p) + QLatin1Char('|')
               + QString::fromUtf8((const char *)u);
    }
    sqlite3_finalize(st);
    return out;
}

bool Db::findCredential(const QString &target, const QString &proto, QString &user, QString &pass)
{
    sqlite3_stmt *st = nullptr;
    const QString sql = QStringLiteral(
        "SELECT user,pass FROM auths WHERE target='%1' AND protocol='%2' LIMIT 1")
        .arg(QString(target).replace(QLatin1Char('\''), QStringLiteral("''")),
             QString(proto).replace(QLatin1Char('\''), QStringLiteral("''")));
    if (sqlite3_prepare_v2(m_db, sql.toUtf8().constData(), -1, &st, nullptr) != SQLITE_OK)
        return false;
    bool found = false;
    if (sqlite3_step(st) == SQLITE_ROW) {
        const unsigned char *u = sqlite3_column_text(st, 0);
        const unsigned char *p = sqlite3_column_text(st, 1);
        user = QString::fromUtf8((const char *)u);
        pass = QString::fromUtf8((const char *)p);
        found = true;
    }
    sqlite3_finalize(st);
    return found;
}

bool Db::saveCredential(const QString &target, const QString &proto, const QString &user,
                        const QString &pass)
{
    sqlite3_stmt *st = nullptr;
    if (sqlite3_prepare_v2(m_db,
                           "INSERT INTO auths(target,protocol,user,pass) VALUES(?,?,?,?)", -1,
                           &st, nullptr) != SQLITE_OK)
        return false;
    bindText(st, 1, target);
    bindText(st, 2, proto);
    bindText(st, 3, user);
    bindText(st, 4, pass);
    const bool ok = sqlite3_step(st) == SQLITE_DONE;
    sqlite3_finalize(st);
    return ok;
}

QString Db::classify(const QString &fileName)
{
    static const char *video[] = {"MP4", "M4V", "MPG", "MPEG", "MPEG4", "MPE", "AVI", "WMV",
                                  "WMA", "WAV", "MOV", "MKV", "FLV", "SWF", "3GP", "WEBM",
                                  "TS", "MP2T", "MPEGTS", "OGG", "OGV", "OGM", "M4A", "MP3",
                                  "AAC", "FLAC", "RMVB", "ASF", "QT"};
    static const char *audio[] = {"MP3", "M4A", "AAC", "FLAC", "WAV", "WMA", "OGG", "OGA",
                                  "OPUS", "MID", "MIDI", "APE", "RA", "RAM"};
    static const char *comp[] = {"ZIP", "RAR", "7Z", "TAR", "GZ", "BZ2", "XZ", "ISO", "CAB",
                                 "ARJ", "LZMA", "DMG", "PKG", "DEB", "RPM", "XPI", "APK",
                                 "JAR"};
    static const char *doc[] = {"PDF", "DOC", "DOCX", "XLS", "XLSX", "PPT", "PPS", "PPTX",
                                "ODT", "ODS", "WPD", "XML", "TEX", "RTF", "EPUB", "TXT",
                                "MOBI", "AZW3"};
    const QString ext = QFileInfo(fileName).suffix().toUpper();
    auto in = [&](const char **arr, int n) {
        for (int i = 0; i < n; ++i)
            if (ext == QLatin1String(arr[i]))
                return true;
        return false;
    };
    if (in(video, sizeof(video) / sizeof(*video)))
        return QStringLiteral("video");
    if (in(audio, sizeof(audio) / sizeof(*audio)))
        return QStringLiteral("audio");
    if (in(comp, sizeof(comp) / sizeof(*comp)))
        return QStringLiteral("compressed");
    if (in(doc, sizeof(doc) / sizeof(*doc)))
        return QStringLiteral("document");
    if (ext == QLatin1String("EXE") || ext == QLatin1String("MSI") || ext == QLatin1String("BIN")
        || ext == QLatin1String("APPIMAGE") || ext == QLatin1String("BAT")
        || ext == QLatin1String("COM"))
        return QStringLiteral("application");
    return QStringLiteral("misc");
}

} // namespace neat
