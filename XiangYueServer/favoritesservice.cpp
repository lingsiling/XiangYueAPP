#include "favoritesservice.h"
#include <QDebug>

// 添加收藏：校验参数 → 验证批次存在 → 检查重复 → 委托 Repository
FavoritesService::AddFavoriteResult FavoritesService::addFavorite(qint64 userId, qint64 sessionId)
{
    AddFavoriteResult result;

    // 校验：未登录用户不能收藏
    if (userId <= 0) {
        result.reason = "UNAUTHORIZED";
        return result;
    }

    // 校验：批次ID非法
    if (sessionId <= 0) {
        result.reason = "INVALID_SESSION";
        return result;
    }

    // 检查批次是否存在（sessionUserId 不存在时返回 nullopt）
    if (!m_sessionRepo.sessionUserId(sessionId).has_value()) {
        result.reason = "SESSION_NOT_FOUND";
        return result;
    }

    // 检查是否已经收藏过（去重）
    if (m_repo.isFavorited(userId, sessionId)) {
        result.reason = "ALREADY_FAVORITED";
        return result;
    }

    // 委托 Repository 执行数据库操作
    const auto optFavoriteId = m_repo.addFavorite(userId, sessionId);
    if (!optFavoriteId.has_value()) {
        result.reason = "DATABASE_ERROR";
        return result;
    }

    result.ok = true;
    qDebug() << "[FavoritesService] 收藏成功，userId=" << userId
             << "sessionId=" << sessionId
             << "favoriteId=" << optFavoriteId.value();
    return result;
}

// 获取用户收藏列表：先清理孤儿收藏（批次已删的残留），再查
FavoritesService::GetFavoritesResult FavoritesService::getFavorites(qint64 userId)
{
    GetFavoritesResult result;

    if (userId <= 0) {
        result.reason = "UNAUTHORIZED";
        return result;
    }

    // 清理“批次已不存在”的孤儿收藏，保证列表不出现失效项
    m_repo.purgeOrphans();

    result.sessions = m_repo.getFavoriteSessionsByUserId(userId);
    result.ok = true;
    qDebug() << "[FavoritesService] 获取收藏列表成功，userId=" << userId
             << "数量=" << result.sessions.size();
    return result;
}

// 取消收藏：校验 → 委托 Repository 软删除
FavoritesService::RemoveFavoriteResult FavoritesService::removeFavorite(qint64 userId, qint64 sessionId)
{
    RemoveFavoriteResult result;

    if (userId <= 0) {
        result.reason = "UNAUTHORIZED";
        return result;
    }

    if (sessionId <= 0) {
        result.reason = "INVALID_SESSION";
        return result;
    }

    // 委托 Repository 执行软删除
    if (m_repo.removeFavorite(userId, sessionId)) {
        result.ok = true;
        qDebug() << "[FavoritesService] 取消收藏成功，userId=" << userId
                 << "sessionId=" << sessionId;
        return result;
    }

    result.reason = "DATABASE_ERROR";
    return result;
}

// 检查指定批次是否已被用户收藏
FavoritesService::CheckFavoriteResult FavoritesService::checkFavorite(qint64 userId, qint64 sessionId)
{
    CheckFavoriteResult result;

    if (userId <= 0) {
        result.reason = "UNAUTHORIZED";
        return result;
    }

    if (sessionId <= 0) {
        result.reason = "INVALID_SESSION";
        return result;
    }

    // 委托 Repository 查询收藏状态
    result.ok = true;
    result.isFavorited = m_repo.isFavorited(userId, sessionId);
    qDebug() << "[FavoritesService] 检查收藏状态，userId=" << userId
             << "sessionId=" << sessionId
             << "isFavorited=" << result.isFavorited;
    return result;
}
