// commentrepository.cpp
#include "commentrepository.h"
#include "dbmanager.h"

#include <QSqlQuery>
#include <QSqlError>
#include <QVariant>
#include <QDebug>

std::optional<qint64> CommentRepository::insert(const QString &resourceName,
                                                qint64 userId,
                                                const QString &content)
{
    QSqlQuery q(DBManager::instance().db());
    q.prepare("INSERT INTO comments(resource_name, user_id, content) VALUES(?,?,?)");
    q.addBindValue(resourceName);
    q.addBindValue(userId);
    q.addBindValue(content);

    if (!q.exec()) {
        qDebug() << "[CommentRepo] insert failed:" << q.lastError().text();
        return std::nullopt;
    }

    // SQLite 支持 lastInsertId
    return q.lastInsertId().toLongLong();
}

QList<CommentRecord> CommentRepository::listByResource(const QString &resourceName)
{
    QList<CommentRecord> out;

    QSqlQuery q(DBManager::instance().db());
    q.prepare(R"SQL(
        SELECT c.id, c.resource_name, c.user_id, IFNULL(u.username,''), c.created_at, c.content
        FROM comments c
        LEFT JOIN users u ON u.id = c.user_id
        WHERE c.resource_name = ?
        ORDER BY c.id DESC
    )SQL");
    q.addBindValue(resourceName);

    if (!q.exec()) {
        qDebug() << "[CommentRepo] listByResource failed:" << q.lastError().text();
        return out;
    }

    while (q.next()) {
        CommentRecord r;
        r.id = q.value(0).toLongLong();
        r.resourceName = q.value(1).toString();
        r.userId = q.value(2).toLongLong();
        r.username = q.value(3).toString();
        r.createdAt = q.value(4).toString();
        r.content = q.value(5).toString();
        out.push_back(r);
    }
    return out;
}

std::optional<CommentRecord> CommentRepository::findById(qint64 commentId)
{
    QSqlQuery q(DBManager::instance().db());
    q.prepare(R"SQL(
        SELECT c.id, c.resource_name, c.user_id, IFNULL(u.username,''), c.created_at, c.content
        FROM comments c
        LEFT JOIN users u ON u.id = c.user_id
        WHERE c.id = ?
    )SQL");
    q.addBindValue(commentId);

    if (!q.exec()) {
        qDebug() << "[CommentRepo] findById failed:" << q.lastError().text();
        return std::nullopt;
    }
    if (!q.next())
        return std::nullopt;

    CommentRecord r;
    r.id = q.value(0).toLongLong();
    r.resourceName = q.value(1).toString();
    r.userId = q.value(2).toLongLong();
    r.username = q.value(3).toString();
    r.createdAt = q.value(4).toString();
    r.content = q.value(5).toString();
    return r;
}

bool CommentRepository::deleteById(qint64 commentId)
{
    QSqlQuery q(DBManager::instance().db());
    q.prepare("DELETE FROM comments WHERE id=?");
    q.addBindValue(commentId);

    if (!q.exec()) {
        qDebug() << "[CommentRepo] deleteById failed:" << q.lastError().text();
        return false;
    }
    return true;
}