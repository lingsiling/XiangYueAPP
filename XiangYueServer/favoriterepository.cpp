#include "favoriterepository.h"
#include "dbmanager.h"

#include <QSqlError>
#include <QSqlQuery>
#include <QVariant>
#include <QDebug>

std::optional<qint64> FavoriteRepository::ensureResource(const QString &resourceName)
{
    QSqlQuery findQuery(DBManager::instance().db());
    findQuery.prepare("SELECT id FROM resources WHERE filename = ?");
    findQuery.addBindValue(resourceName);

    if (!findQuery.exec()) {
        qDebug() << "[FavoriteRepo] ensureResource find failed:" << findQuery.lastError().text();
        return std::nullopt;
    }

    if (findQuery.next())
        return findQuery.value(0).toLongLong();

    QSqlQuery insertQuery(DBManager::instance().db());
    insertQuery.prepare("INSERT INTO resources(filename) VALUES(?)");
    insertQuery.addBindValue(resourceName);

    if (!insertQuery.exec()) {
        qDebug() << "[FavoriteRepo] ensureResource insert failed:" << insertQuery.lastError().text();
        return std::nullopt;
    }

    return insertQuery.lastInsertId().toLongLong();
}

bool FavoriteRepository::isFavorite(qint64 userId, qint64 resourceId)
{
    QSqlQuery q(DBManager::instance().db());
    q.prepare("SELECT 1 FROM favorites WHERE user_id = ? AND resource_id = ? LIMIT 1");
    q.addBindValue(userId);
    q.addBindValue(resourceId);

    if (!q.exec()) {
        qDebug() << "[FavoriteRepo] isFavorite failed:" << q.lastError().text();
        return false;
    }

    return q.next();
}

bool FavoriteRepository::addFavorite(qint64 userId, qint64 resourceId)
{
    QSqlQuery q(DBManager::instance().db());
    q.prepare("INSERT OR IGNORE INTO favorites(user_id, resource_id) VALUES(?, ?)");
    q.addBindValue(userId);
    q.addBindValue(resourceId);

    if (!q.exec()) {
        qDebug() << "[FavoriteRepo] addFavorite failed:" << q.lastError().text();
        return false;
    }

    return true;
}

bool FavoriteRepository::removeFavorite(qint64 userId, qint64 resourceId)
{
    QSqlQuery q(DBManager::instance().db());
    q.prepare("DELETE FROM favorites WHERE user_id = ? AND resource_id = ?");
    q.addBindValue(userId);
    q.addBindValue(resourceId);

    if (!q.exec()) {
        qDebug() << "[FavoriteRepo] removeFavorite failed:" << q.lastError().text();
        return false;
    }

    return true;
}

QStringList FavoriteRepository::listByUser(qint64 userId)
{
    QStringList out;

    QSqlQuery q(DBManager::instance().db());
    q.prepare(R"SQL(
        SELECT r.filename
        FROM favorites f
        INNER JOIN resources r ON r.id = f.resource_id
        WHERE f.user_id = ?
        ORDER BY f.created_at DESC, r.id DESC
    )SQL");
    q.addBindValue(userId);

    if (!q.exec()) {
        qDebug() << "[FavoriteRepo] listByUser failed:" << q.lastError().text();
        return out;
    }

    while (q.next()) {
        out << q.value(0).toString();
    }
    return out;
}
