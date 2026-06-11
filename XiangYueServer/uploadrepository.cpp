#include "uploadrepository.h"
#include "dbconnectionpool.h"

#include <QSqlError>
#include <QSqlQuery>
#include <QDebug>

std::optional<qint64> UploadRepository::insert(qint64 userId, qint64 resourceId)
{
	if (userId <= 0 || resourceId <= 0)
		return std::nullopt;

    //同一个资源只保留一条上传记录：如果已经存在，直接返回已有记录 ID
	QSqlQuery existsQuery(DBConnectionPool::instance().connection());
	existsQuery.prepare("SELECT id FROM uploads WHERE resource_id = ? LIMIT 1");
	existsQuery.addBindValue(resourceId);
	if (!existsQuery.exec()) {
		qDebug() << "[UploadRepo] exists check failed:" << existsQuery.lastError().text();
		return std::nullopt;
	}

	if (existsQuery.next()) {
		const qint64 existingId = existsQuery.value(0).toLongLong();
		qDebug() << "[UploadRepo] skip duplicate upload record, resourceId=" << resourceId
		         << "existingId=" << existingId;
		return existingId;
	}

    //只负责把“谁上传了哪个资源”写入 uploads 表
	QSqlQuery q(DBConnectionPool::instance().connection());
	q.prepare(R"SQL(
		INSERT INTO uploads(user_id, resource_id)
		VALUES(?, ?)
	)SQL");
	q.addBindValue(userId);
	q.addBindValue(resourceId);

	if (!q.exec()) {
		qDebug() << "[UploadRepo] insert failed:" << q.lastError().text();
		return std::nullopt;
	}

	return q.lastInsertId().toLongLong();
}

std::optional<qint64> UploadRepository::insertByFileName(qint64 userId, const QString &filename)
{
	const QString name = filename.trimmed();
	if (userId <= 0 || name.isEmpty())
		return std::nullopt;

    //先查出 resources.id，再调用通用 insert 插入 uploads，避免 INSERT...SELECT 在某些驱动上不返回 lastInsertId
	QSqlQuery q(DBConnectionPool::instance().connection());
	q.prepare("SELECT id FROM resources WHERE filename = ?");
	q.addBindValue(name);
	if (!q.exec()) {
		qDebug() << "[UploadRepo] find resource failed:" << q.lastError().text();
		return std::nullopt;
	}

	if (!q.next()) {
		qDebug() << "[UploadRepo] resource not found for filename:" << name;
		return std::nullopt;
	}

	const qint64 resourceId = q.value(0).toLongLong();
	return insert(userId, resourceId);
}

bool UploadRepository::deleteByResourceId(qint64 resourceId)
{
	if (resourceId <= 0)
		return false;

    //资源删除时同步清理 uploads，避免“我的上传”里残留记录
	QSqlQuery q(DBConnectionPool::instance().connection());
	q.prepare("DELETE FROM uploads WHERE resource_id = ?");
	q.addBindValue(resourceId);

	if (!q.exec()) {
		qDebug() << "[UploadRepo] deleteByResourceId failed:" << q.lastError().text();
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