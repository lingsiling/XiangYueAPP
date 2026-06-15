#include "uploadrepository.h"
#include "dbconnectionpool.h"

#include <QSqlError>
#include <QSqlQuery>
#include <QDebug>

bool UploadRepository::deleteUploadsBySessionId(qint64 sessionId)
{
	if (sessionId <= 0)
		return false;

    //删除整个批次：清理该批次在 uploads 表中的全部”文件-批次”关联记录
	QSqlQuery q(DBConnectionPool::instance().connection());
	q.prepare("DELETE FROM uploads WHERE session_id = ?");
	q.addBindValue(sessionId);

	if (!q.exec()) {
		qDebug() << "[UploadRepo] deleteUploadsBySessionId failed:" << q.lastError().text();
		return false;
	}

	return true;
}

bool UploadRepository::deleteSessionRow(qint64 sessionId)
{
	if (sessionId <= 0)
		return false;

    //删除批次记录本身：upload_sessions 中代表”一次上传”的那一行
	QSqlQuery q(DBConnectionPool::instance().connection());
	q.prepare("DELETE FROM upload_sessions WHERE id = ?");
	q.addBindValue(sessionId);

	if (!q.exec()) {
		qDebug() << "[UploadRepo] deleteSessionRow failed:" << q.lastError().text();
		return false;
	}

	return true;
}

// ====== 按批次ID查询该批次的所有文件 ======
QList<ResourceRecord> UploadRepository::listBySessionId(qint64 sessionId)
{
    QList<ResourceRecord> list;
    if (sessionId <= 0) return list;

    QSqlQuery q(DBConnectionPool::instance().connection());
    q.prepare(R"SQL(
        SELECT r.id, r.filename, r.server_path, r.size, r.uploader_user_id, r.uploaded_at
        FROM resources r
        INNER JOIN uploads u ON u.resource_id = r.id
        WHERE u.session_id = ?
        ORDER BY r.filename
    )SQL");
    q.addBindValue(sessionId);

    if (!q.exec()) {
        qDebug() << "[UploadRepo] listBySessionId failed:" << q.lastError().text();
        return list;
    }

    while (q.next()) {
        ResourceRecord rec;
        rec.id = q.value(0).toLongLong();
        rec.filename = q.value(1).toString();
        rec.serverPath = q.value(2).toString();
        rec.size = q.value(3).toLongLong();
        rec.uploaderUserId = q.value(4).toLongLong();
        rec.uploadedAt = q.value(5).toString();
        list.append(rec);
    }
    return list;
}

QString UploadRepository::uploaderNameBySessionId(qint64 sessionId)
{
    if (sessionId <= 0) return {};

    QSqlQuery q(DBConnectionPool::instance().connection());
    q.prepare("SELECT user_id FROM upload_sessions WHERE id = ?");
    q.addBindValue(sessionId);
    if (!q.exec() || !q.next()) return {};

    const qint64 userId = q.value(0).toLongLong();

    QSqlQuery q2(DBConnectionPool::instance().connection());
    q2.prepare("SELECT username FROM users WHERE id = ?");
    q2.addBindValue(userId);
    if (q2.exec() && q2.next()) {
        return q2.value(0).toString();
    }
    return {};
}

// ====== 查询某批次所属用户ID（删除批次时做归属校验/定位目录） ======
std::optional<qint64> UploadRepository::sessionUserId(qint64 sessionId)
{
    if (sessionId <= 0) return std::nullopt;

    QSqlQuery q(DBConnectionPool::instance().connection());
    q.prepare("SELECT user_id FROM upload_sessions WHERE id = ?");
    q.addBindValue(sessionId);

    if (!q.exec()) {
        qDebug() << "[UploadRepo] sessionUserId failed:" << q.lastError().text();
        return std::nullopt;
    }
    if (!q.next())
        return std::nullopt;          // 批次不存在

    return q.value(0).toLongLong();
}

// ====== 查询所有批次列表（主界面展示用） ======
QList<UploadRepository::SessionRow> UploadRepository::listAllSessions()
{
    QList<SessionRow> list;

    QSqlQuery q(DBConnectionPool::instance().connection());
    q.prepare(R"SQL(
        SELECT id, user_id, title, tags, description, file_count, created_at
        FROM upload_sessions ORDER BY created_at DESC
    )SQL");

    if (!q.exec()) {
        qDebug() << "[UploadRepo] listAllSessions failed:" << q.lastError().text();
        return list;
    }

    while (q.next()) {
        SessionRow row;
        row.id = q.value(0).toLongLong();
        row.userId = q.value(1).toLongLong();
        row.title = q.value(2).toString();
        row.tags = q.value(3).toString();
        row.description = q.value(4).toString();
        row.fileCount = q.value(5).toInt();
        row.createdAt = q.value(6).toString();
        list.append(row);
    }
    return list;
}

// ====== 查询某个用户的全部批次（"我的上传"展示用） ======
// 与 listAllSessions 的唯一区别：加 user_id 过滤，只返回当前用户自己的批次
QList<UploadRepository::SessionRow> UploadRepository::listSessionsByUser(qint64 userId)
{
    QList<SessionRow> list;
    if (userId <= 0) return list;

    QSqlQuery q(DBConnectionPool::instance().connection());
    q.prepare(R"SQL(
        SELECT id, user_id, title, tags, description, file_count, created_at
        FROM upload_sessions WHERE user_id = ? ORDER BY created_at DESC
    )SQL");
    q.addBindValue(userId);

    if (!q.exec()) {
        qDebug() << "[UploadRepo] listSessionsByUser failed:" << q.lastError().text();
        return list;
    }

    while (q.next()) {
        SessionRow row;
        row.id = q.value(0).toLongLong();
        row.userId = q.value(1).toLongLong();
        row.title = q.value(2).toString();
        row.tags = q.value(3).toString();
        row.description = q.value(4).toString();
        row.fileCount = q.value(5).toInt();
        row.createdAt = q.value(6).toString();
        list.append(row);
    }
    return list;
}