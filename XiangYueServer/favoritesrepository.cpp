#include "favoritesrepository.h"
#include "dbconnectionpool.h"
#include <QSqlQuery>
#include <QSqlError>
#include <QDebug>

std::optional<qint64> FavoritesRepository::addFavorite(qint64 userId, qint64 resourceId)
{
    auto conn = DBConnectionPool::instance().connection();
    QSqlQuery query(conn);

    query.prepare("INSERT INTO favorites (user_id, resource_id, created_at) "
                  "VALUES (?, ?, CURRENT_TIMESTAMP)");
    query.addBindValue(userId);
    query.addBindValue(resourceId);

    if (!query.exec()) {
        qWarning() << "[FavoritesRepository] 插入收藏失败:" << query.lastError().text();
        return std::nullopt;
    }

    //获取插入的记录 ID
    qint64 favoriteId = query.lastInsertId().toLongLong();
    return favoriteId;
}

bool FavoritesRepository::isFavorited(qint64 userId, qint64 resourceId)
{
    auto conn = DBConnectionPool::instance().connection();
    QSqlQuery query(conn);

    query.prepare("SELECT 1 FROM favorites WHERE user_id = ? AND resource_id = ? LIMIT 1");
    query.addBindValue(userId);
    query.addBindValue(resourceId);

    if (!query.exec()) {
        qWarning() << "[FavoritesRepository] 查询收藏状态失败:" << query.lastError().text();
        return false;
    }

    return query.next();
}

QStringList FavoritesRepository::getFavoritesByUserId(qint64 userId)
{
    QStringList result;
    auto conn = DBConnectionPool::instance().connection();
    QSqlQuery query(conn);

    //左连接 resources 表，获取资源文件名
    query.prepare("SELECT r.filename FROM resources r "
                  "INNER JOIN favorites f ON r.id = f.resource_id "
                  "WHERE f.user_id = ? "
                  "ORDER BY f.created_at DESC");
    query.addBindValue(userId);

    if (!query.exec()) {
        qWarning() << "[FavoritesRepository] 查询收藏列表失败:" << query.lastError().text();
        return result;
    }

    while (query.next()) {
        result.append(query.value("filename").toString());
    }

    return result;
}
