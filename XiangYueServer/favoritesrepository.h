// favoritesrepository.h
#ifndef FAVORITESREPOSITORY_H
#define FAVORITESREPOSITORY_H

#include "favoriterecord.h"
#include <QString>
#include <optional>

// FavoritesRepository：数据访问层（只负责 SQL 操作）
class FavoritesRepository
{
public:
    // 添加收藏：返回成功的收藏记录 ID，失败返回 nullopt
    std::optional<qint64> addFavorite(qint64 userId, qint64 resourceId);

    // 检查是否已收藏
    bool isFavorited(qint64 userId, qint64 resourceId);

    // 获取用户的收藏资源列表（返回资源文件名列表）
    QStringList getFavoritesByUserId(qint64 userId);
};

#endif // FAVORITESREPOSITORY_H
