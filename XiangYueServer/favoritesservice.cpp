#include "favoritesservice.h"
#include "resourcerepository.h"
#include <QDebug>

// 添加收藏：校验参数 → 验证资源存在 → 检查重复 → 委托 Repository
FavoritesService::AddFavoriteResult FavoritesService::addFavorite(qint64 userId, const QString &resourceName)
{
    AddFavoriteResult result;

    // 校验：未登录用户不能收藏
    if (userId <= 0) {
        result.ok = false;
        result.reason = "UNAUTHORIZED";
        return result;
    }

    // 校验：资源名不能为空
    if (resourceName.isEmpty()) {
        result.ok = false;
        result.reason = "INVALID_RESOURCE";
        return result;
    }

    // 检查资源是否存在
    ResourceRepository resourceRepo;
    const auto optResource = resourceRepo.findByFileName(resourceName);

    if (!optResource.has_value()) {
        result.ok = false;
        result.reason = "RESOURCE_NOT_FOUND";
        return result;
    }

    const auto &resource = optResource.value();

    // 检查是否已经收藏过（去重）
    if (m_repo.isFavorited(userId, resource.id)) {
        result.ok = false;
        result.reason = "ALREADY_FAVORITED";
        return result;
    }

    // 委托 Repository 执行数据库操作
    const auto optFavoriteId = m_repo.addFavorite(userId, resource.id);

    if (!optFavoriteId.has_value()) {
        result.ok = false;
        result.reason = "DATABASE_ERROR";
        return result;
    }

    result.ok = true;
    qDebug() << "[FavoritesService] 收藏成功，userId=" << userId 
             << "resourceName=" << resourceName 
             << "favoriteId=" << optFavoriteId.value();
    return result;
}

// 获取用户收藏列表
FavoritesService::GetFavoritesResult FavoritesService::getFavorites(qint64 userId)
{
    GetFavoritesResult result;

    if (userId <= 0) {
        result.ok = false;
        result.reason = "UNAUTHORIZED";
        return result;
    }

    // 委托 Repository 查询
    result.favorites = m_repo.getFavoritesByUserId(userId);
    result.ok = true;
    qDebug() << "[FavoritesService] 获取收藏列表成功，userId=" << userId 
             << "数量=" << result.favorites.size();
    return result;
}

// 取消收藏：校验 → 查资源 → 委托 Repository
FavoritesService::RemoveFavoriteResult FavoritesService::removeFavorite(qint64 userId, const QString &resourceName)
{
    RemoveFavoriteResult result;

    if (userId <= 0) {
        result.ok = false;
        result.reason = "UNAUTHORIZED";
        return result;
    }

    if (resourceName.isEmpty()) {
        result.ok = false;
        result.reason = "INVALID_RESOURCE";
        return result;
    }

    // 检查资源是否存在
    ResourceRepository resourceRepo;
    const auto optResource = resourceRepo.findByFileName(resourceName);

    if (!optResource.has_value()) {
        result.ok = false;
        result.reason = "RESOURCE_NOT_FOUND";
        return result;
    }

    const auto &resource = optResource.value();

    // 委托 Repository 执行软删除
    if (m_repo.removeFavorite(userId, resource.id)) {
        result.ok = true;
        qDebug() << "[FavoritesService] 取消收藏成功，userId=" << userId 
                 << "resourceName=" << resourceName;
        return result;
    }

    result.ok = false;
    result.reason = "DATABASE_ERROR";
    return result;
}

// 检查指定资源是否已被用户收藏
FavoritesService::CheckFavoriteResult FavoritesService::checkFavorite(qint64 userId, const QString &resourceName)
{
    CheckFavoriteResult result;

    if (userId <= 0) {
        result.ok = false;
        result.reason = "UNAUTHORIZED";
        return result;
    }

    if (resourceName.isEmpty()) {
        result.ok = false;
        result.reason = "INVALID_RESOURCE";
        return result;
    }

    // 查资源是否存在
    ResourceRepository resourceRepo;
    const auto optResource = resourceRepo.findByFileName(resourceName);

    if (!optResource.has_value()) {
        result.ok = false;
        result.reason = "RESOURCE_NOT_FOUND";
        return result;
    }

    const auto &resource = optResource.value();

    // 委托 Repository 查询收藏状态
    result.ok = true;
    result.isFavorited = m_repo.isFavorited(userId, resource.id);
    qDebug() << "[FavoritesService] 检查收藏状态，userId=" << userId 
             << "resourceName=" << resourceName 
             << "isFavorited=" << result.isFavorited;
    return result;
}
