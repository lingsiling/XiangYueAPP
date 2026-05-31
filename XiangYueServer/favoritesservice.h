#ifndef FAVORITESSERVICE_H
#define FAVORITESSERVICE_H

#include "favoritesrepository.h"
#include <QString>
#include <QStringList>

// FavoritesService：业务层（校验参数、验证资源存在等）
// - 不负责：TCP 拆包/回包（Worker 做）
// - 不负责：SQL 细节（Repository 做）
class FavoritesService
{
public:
    struct AddFavoriteResult {
        bool ok = false;
        QString reason;  // UNAUTHORIZED / INVALID_RESOURCE / RESOURCE_NOT_FOUND / DATABASE_ERROR
    };

    struct GetFavoritesResult {
        bool ok = false;
        QStringList favorites;  // 收藏的资源文件名列表
        QString reason;  // 错误原因
    };

    // 添加收藏
    AddFavoriteResult addFavorite(qint64 userId, const QString &resourceName);

    // 获取用户的收藏列表
    GetFavoritesResult getFavorites(qint64 userId);

private:
    FavoritesRepository m_repo;
};

#endif // FAVORITESSERVICE_H
