#include "favoritesrepository.h"
#include "dbconnectionpool.h"
#include <QSqlQuery>
#include <QSqlError>
#include <QDebug>

// 添加收藏：先查是否已有记录，已存在则重新置为有效，不存在则插入新记录
std::optional<qint64> FavoritesRepository::addFavorite(qint64 userId, qint64 sessionId)
{
    auto conn = DBConnectionPool::instance().connection();
    QSqlQuery query(conn);

    // 第一步：检查该用户-批次对是否已有记录（含已取消的软删除记录）
    query.prepare("SELECT id FROM favorites WHERE user_id = ? AND session_id = ?");
    query.addBindValue(userId);
    query.addBindValue(sessionId);

    if (!query.exec()) {
        qWarning() << "[FavoritesRepository] 查询收藏记录失败:" << query.lastError().text();
        return std::nullopt;
    }

    if (query.next()) {
        // 已有记录，把 is_active 重新置 1（支持取消后重新收藏）
        qint64 id = query.value(0).toLongLong();
        QSqlQuery updateQuery(conn);
        updateQuery.prepare("UPDATE favorites SET is_active = 1, updated_at = CURRENT_TIMESTAMP "
                           "WHERE user_id = ? AND session_id = ?");
        updateQuery.addBindValue(userId);
        updateQuery.addBindValue(sessionId);

        if (!updateQuery.exec()) {
            qWarning() << "[FavoritesRepository] 更新收藏状态失败:" << updateQuery.lastError().text();
            return std::nullopt;
        }

        qDebug() << "[FavoritesRepository] 重新收藏成功，id=" << id;
        return id;
    }

    // 第二步：记录不存在，插入新记录
    query.prepare("INSERT INTO favorites (user_id, session_id, is_active, created_at, updated_at) "
                  "VALUES (?, ?, 1, CURRENT_TIMESTAMP, CURRENT_TIMESTAMP)");
    query.addBindValue(userId);
    query.addBindValue(sessionId);

    if (!query.exec()) {
        qWarning() << "[FavoritesRepository] 插入收藏失败:" << query.lastError().text();
        return std::nullopt;
    }

    // 返回自增 ID
    qint64 favoriteId = query.lastInsertId().toLongLong();
    qDebug() << "[FavoritesRepository] 首次收藏成功，id=" << favoriteId;
    return favoriteId;
}

// 检查指定用户是否已收藏某批次（只查 is_active = 1 的记录）
bool FavoritesRepository::isFavorited(qint64 userId, qint64 sessionId)
{
    auto conn = DBConnectionPool::instance().connection();
    QSqlQuery query(conn);

    query.prepare("SELECT 1 FROM favorites WHERE user_id = ? AND session_id = ? AND is_active = 1 LIMIT 1");
    query.addBindValue(userId);
    query.addBindValue(sessionId);

    if (!query.exec()) {
        qWarning() << "[FavoritesRepository] 查询收藏状态失败:" << query.lastError().text();
        return false;
    }

    return query.next();
}

// 获取用户已收藏的批次列表（联表 upload_sessions，字段与 listSessionsByUser 完全一致）
QList<UploadRepository::SessionRow> FavoritesRepository::getFavoriteSessionsByUserId(qint64 userId)
{
    QList<UploadRepository::SessionRow> result;
    auto conn = DBConnectionPool::instance().connection();
    QSqlQuery query(conn);

    // 只取 is_active = 1 的收藏，按收藏更新时间倒序（最近收藏的排前面）
    query.prepare(R"SQL(
        SELECT s.id, s.user_id, s.title, s.tags, s.description, s.file_count, s.created_at
        FROM upload_sessions s
        INNER JOIN favorites f ON f.session_id = s.id
        WHERE f.user_id = ? AND f.is_active = 1
        ORDER BY f.updated_at DESC
    )SQL");
    query.addBindValue(userId);

    if (!query.exec()) {
        qWarning() << "[FavoritesRepository] 查询收藏列表失败:" << query.lastError().text();
        return result;
    }

    while (query.next()) {
        UploadRepository::SessionRow row;
        row.id          = query.value(0).toLongLong();
        row.userId      = query.value(1).toLongLong();
        row.title       = query.value(2).toString();
        row.tags        = query.value(3).toString();
        row.description = query.value(4).toString();
        row.fileCount   = query.value(5).toInt();
        row.createdAt   = query.value(6).toString();
        result.append(row);
    }

    return result;
}

// 取消收藏：软删除，只更新状态为 0，保留历史记录
bool FavoritesRepository::removeFavorite(qint64 userId, qint64 sessionId)
{
    auto conn = DBConnectionPool::instance().connection();
    QSqlQuery query(conn);

    query.prepare("UPDATE favorites SET is_active = 0, updated_at = CURRENT_TIMESTAMP "
                  "WHERE user_id = ? AND session_id = ? AND is_active = 1");
    query.addBindValue(userId);
    query.addBindValue(sessionId);

    if (!query.exec()) {
        qWarning() << "[FavoritesRepository] 取消收藏失败:" << query.lastError().text();
        return false;
    }

    return query.numRowsAffected() > 0;
}

// 硬删除某批次的全部收藏记录（批次被删除时，所有用户对它的收藏都失去意义）
bool FavoritesRepository::removeBySessionId(qint64 sessionId)
{
    auto conn = DBConnectionPool::instance().connection();
    QSqlQuery query(conn);

    query.prepare("DELETE FROM favorites WHERE session_id = ?");
    query.addBindValue(sessionId);

    if (!query.exec()) {
        qWarning() << "[FavoritesRepository] 按批次删除收藏失败:" << query.lastError().text();
        return false;
    }

    qDebug() << "[FavoritesRepository] 批次" << sessionId
             << "的收藏记录已清理，受影响" << query.numRowsAffected() << "条";
    return true;
}

// 清理孤儿收藏：收藏指向的批次已不在 upload_sessions 中（满足“检测到没有该批次则删除收藏记录”）
bool FavoritesRepository::purgeOrphans()
{
    auto conn = DBConnectionPool::instance().connection();
    QSqlQuery query(conn);

    if (!query.exec("DELETE FROM favorites "
                    "WHERE session_id NOT IN (SELECT id FROM upload_sessions)")) {
        qWarning() << "[FavoritesRepository] 清理孤儿收藏失败:" << query.lastError().text();
        return false;
    }

    const int affected = query.numRowsAffected();
    if (affected > 0)
        qDebug() << "[FavoritesRepository] 清理孤儿收藏" << affected << "条";
    return true;
}
