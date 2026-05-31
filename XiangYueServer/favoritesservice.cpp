#include "favoritesservice.h"
#include "resourcerepository.h"
#include <QDebug>

FavoritesService::AddFavoriteResult FavoritesService::addFavorite(qint64 userId, const QString &resourceName)
{
    AddFavoriteResult result;

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

    //检查资源是否存在
    ResourceRepository resourceRepo;
    const auto optResource = resourceRepo.findByFileName(resourceName);

    if (!optResource.has_value()) {
        result.ok = false;
        result.reason = "RESOURCE_NOT_FOUND";
        return result;
    }

    const auto &resource = optResource.value();

    //检查是否已经收藏过
    if (m_repo.isFavorited(userId, resource.id)) {
        result.ok = false;
        result.reason = "ALREADY_FAVORITED";
        return result;
    }

    // 通过 Repository 执行数据库操作
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

FavoritesService::GetFavoritesResult FavoritesService::getFavorites(qint64 userId)
{
    GetFavoritesResult result;

    if (userId <= 0) {
        result.ok = false;
        result.reason = "UNAUTHORIZED";
        return result;
    }

    //直接从 Repository 查询
    result.favorites = m_repo.getFavoritesByUserId(userId);
    result.ok = true;
    qDebug() << "[FavoritesService] 获取收藏列表成功，userId=" << userId 
             << "数量=" << result.favorites.size();
    return result;
}
