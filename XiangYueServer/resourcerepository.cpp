#include "resourcerepository.h"
#include "dbconnectionpool.h"

#include <QSqlError>
#include <QSqlQuery>
#include <QVariant>
#include <QDebug>

std::optional<ResourceRecord> ResourceRepository::findByFileName(const QString &filename)
{
    //按文件名查找资源记录，供上传覆盖、删除同步、启动清理使用
    QSqlQuery q(DBConnectionPool::instance().connection());
    q.prepare(R"SQL(
        SELECT id, filename, server_path, size, uploader_user_id, uploaded_at
        FROM resources
        WHERE filename = ?
    )SQL");
    q.addBindValue(filename.trimmed());

    if (!q.exec()) {
        qDebug() << "[ResourceRepo] findByFileName failed:" << q.lastError().text();
        return std::nullopt;
    }

    if (!q.next())
        return std::nullopt;

    ResourceRecord rec;
    rec.id = q.value(0).toLongLong();
    rec.filename = q.value(1).toString();
    rec.serverPath = q.value(2).toString();
    rec.size = q.value(3).toLongLong();

    if (!q.value(4).isNull())
        rec.uploaderUserId = q.value(4).toLongLong();

    rec.uploadedAt = q.value(5).toString();
    return rec;
}

QList<ResourceRecord> ResourceRepository::listAll()
{
    //拉取全部资源记录，启动时需要用它和磁盘目录做一次对账
    QList<ResourceRecord> out;

    QSqlQuery q(DBConnectionPool::instance().connection());
    q.prepare(R"SQL(
        SELECT id, filename, server_path, size, uploader_user_id, uploaded_at
        FROM resources
        ORDER BY id DESC
    )SQL");

    if (!q.exec()) {
        qDebug() << "[ResourceRepo] listAll failed:" << q.lastError().text();
        return out;
    }

    while (q.next()) {
        ResourceRecord rec;
        rec.id = q.value(0).toLongLong();
        rec.filename = q.value(1).toString();
        rec.serverPath = q.value(2).toString();
        rec.size = q.value(3).toLongLong();
        if (!q.value(4).isNull())
            rec.uploaderUserId = q.value(4).toLongLong();
        rec.uploadedAt = q.value(5).toString();
        out.push_back(rec);
    }

    return out;
}

bool ResourceRepository::updateServerPath(const QString &oldSubDir, const QString &newSubDir, qint64 sessionId)
{
    if (oldSubDir.isEmpty() || newSubDir.isEmpty() || sessionId <= 0) return false;

    QSqlQuery q(DBConnectionPool::instance().connection());
    q.prepare("UPDATE resources SET server_path = REPLACE(server_path, ?, ?)"
              " WHERE id IN (SELECT resource_id FROM uploads WHERE session_id = ?)");
    q.addBindValue(oldSubDir);
    q.addBindValue(newSubDir);
    q.addBindValue(sessionId);

    if (!q.exec()) {
        qDebug() << "[ResourceRepo] updateServerPath failed:" << q.lastError().text();
        return false;
    }
    return true;
}

bool ResourceRepository::upsert(const QString &filename, const QString &serverPath,
                                 qint64 size, std::optional<qint64> uploaderUserId)
{
    //同名文件按“更新”处理，避免重复上传后表里出现旧路径或旧大小
    const QString name = filename.trimmed();
    if (name.isEmpty())
        return false;

    QSqlQuery q(DBConnectionPool::instance().connection());

    if (findByFileName(name).has_value()) {
        q.prepare(R"SQL(
            UPDATE resources
            SET server_path = ?,
                size = ?,
                uploader_user_id = ?,
                uploaded_at = CURRENT_TIMESTAMP
            WHERE filename = ?
        )SQL");
        q.addBindValue(serverPath);
        q.addBindValue(size);
        if (uploaderUserId.has_value()) {
            q.addBindValue(*uploaderUserId);
        } else {
            q.addBindValue(QVariant());
        }
        q.addBindValue(name);
    } else {
        q.prepare(R"SQL(
            INSERT INTO resources(filename, server_path, size, uploader_user_id)
            VALUES(?, ?, ?, ?)
        )SQL");
        q.addBindValue(name);
        q.addBindValue(serverPath);
        q.addBindValue(size);
        if (uploaderUserId.has_value()) {
            q.addBindValue(*uploaderUserId);
        } else {
            q.addBindValue(QVariant());
        }
    }

    if (!q.exec()) {
        qDebug() << "[ResourceRepo] upsert failed:" << q.lastError().text();
        return false;
    }

    return true;
}

bool ResourceRepository::deleteByFileName(const QString &filename)
{
    // 文件从磁盘移除后，同步删除数据库记录，保持文件系统和资源表一致。
    // 注意：收藏(favorites)已改为按“批次(session)”粒度存储，与单个 resource 无关，
    //       因此这里不再级联删除收藏；批次的收藏清理由 UploadService::deleteSession 统一负责。
    QSqlQuery q(DBConnectionPool::instance().connection());
    q.prepare("DELETE FROM resources WHERE filename = ?");
    q.addBindValue(filename.trimmed());

    if (!q.exec()) {
        qDebug() << "[ResourceRepo] deleteByFileName failed:" << q.lastError().text();
        return false;
    }

    qDebug() << "[ResourceRepo] 资源已删除:" << filename
             << "(受影响" << q.numRowsAffected() << "行)";
    return true;
}