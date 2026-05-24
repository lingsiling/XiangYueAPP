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