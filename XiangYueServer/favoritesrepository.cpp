#include "favoritesrepository.h"
#include "dbconnectionpool.h"
#include <QSqlQuery>
#include <QSqlError>
#include <QDebug>

// 添加收藏：先查是否存在，已存在则更新状态，不存在则插入新记录
std::optional<qint64> FavoritesRepository::addFavorite(qint64 userId, qint64 resourceId)
{
    auto conn = DBConnectionPool::instance().connection();
    QSqlQuery query(conn);

    // 第一步：检查该用户-资源对是否已有记录
    query.prepare("SELECT id FROM favorites WHERE user_id = ? AND resource_id = ?");
    query.addBindValue(userId);
    query.addBindValue(resourceId);

    if (!query.exec()) {
        qWarning() << "[FavoritesRepository] 查询收藏记录失败:" << query.lastError().text();
        return std::nullopt;
    }

    if (query.next()) {
        // 已有记录，把 is_active 重新设为 1（支持取消后重新收藏）
        qint64 id = query.value(0).toLongLong();
        QSqlQuery updateQuery(conn);
        updateQuery.prepare("UPDATE favorites SET is_active = 1, updated_at = CURRENT_TIMESTAMP "
                           "WHERE user_id = ? AND resource_id = ?");
        updateQuery.addBindValue(userId);
        updateQuery.addBindValue(resourceId);

        if (!updateQuery.exec()) {
            qWarning() << "[FavoritesRepository] 更新收藏状态失败:" << updateQuery.lastError().text();
            return std::nullopt;
        }

        qDebug() << "[FavoritesRepository] 重新收藏成功，id=" << id;
        return id;
    }

    // 第二步：记录不存在，插入新记录
    query.prepare("INSERT INTO favorites (user_id, resource_id, is_active, created_at, updated_at) "
                  "VALUES (?, ?, 1, CURRENT_TIMESTAMP, CURRENT_TIMESTAMP)");
    query.addBindValue(userId);
    query.addBindValue(resourceId);

    if (!query.exec()) {
        qWarning() << "[FavoritesRepository] 插入收藏失败:" << query.lastError().text();
        return std::nullopt;
    }

    // 返回自增 ID
    qint64 favoriteId = query.lastInsertId().toLongLong();
    qDebug() << "[FavoritesRepository] 首次收藏成功，id=" << favoriteId;
    return favoriteId;
}

// 检查指定用户是否已收藏某资源（只查 is_active = 1 的记录）
bool FavoritesRepository::isFavorited(qint64 userId, qint64 resourceId)
{
    auto conn = DBConnectionPool::instance().connection();
    QSqlQuery query(conn);

    query.prepare("SELECT 1 FROM favorites WHERE user_id = ? AND resource_id = ? AND is_active = 1 LIMIT 1");
    query.addBindValue(userId);
    query.addBindValue(resourceId);

    if (!query.exec()) {
        qWarning() << "[FavoritesRepository] 查询收藏状态失败:" << query.lastError().text();
        return false;
    }

    return query.next();
}

// 获取用户所有已收藏的资源文件名列表（按更新时间倒序）
QStringList FavoritesRepository::getFavoritesByUserId(qint64 userId)
{
    QStringList result;
    auto conn = DBConnectionPool::instance().connection();
    QSqlQuery query(conn);

    // 联表查询：只取 is_active = 1 的收藏记录
    query.prepare("SELECT r.filename FROM resources r "
                  "INNER JOIN favorites f ON r.id = f.resource_id "
                  "WHERE f.user_id = ? AND f.is_active = 1 "
                  "ORDER BY f.updated_at DESC");
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

// 取消收藏：软删除，只更新状态为 0，保留历史记录
bool FavoritesRepository::removeFavorite(qint64 userId, qint64 resourceId)
{
    auto conn = DBConnectionPool::instance().connection();
    QSqlQuery query(conn);

    query.prepare("UPDATE favorites SET is_active = 0, updated_at = CURRENT_TIMESTAMP "
                  "WHERE user_id = ? AND resource_id = ? AND is_active = 1");
    query.addBindValue(userId);
    query.addBindValue(resourceId);

    if (!query.exec()) {
        qWarning() << "[FavoritesRepository] 取消收藏失败:" << query.lastError().text();
        return false;
    }

    return query.numRowsAffected() > 0;
}
