#ifndef FAVORITESSERVICE_H
#define FAVORITESSERVICE_H

#include "favoritesrepository.h"
#include "uploadrepository.h"
#include <QString>
#include <QList>

// FavoritesService：业务层（校验参数、验证批次存在等）
// 收藏粒度 = 上传批次(session)
// - 不负责：TCP 拆包/回包（Worker 做）
// - 不负责：SQL 细节（Repository 做）
class FavoritesService
{
public:
    struct AddFavoriteResult {
        bool ok = false;
        QString reason;  // UNAUTHORIZED / INVALID_SESSION / SESSION_NOT_FOUND / ALREADY_FAVORITED / DATABASE_ERROR
    };

    struct GetFavoritesResult {
        bool ok = false;
        QList<UploadRepository::SessionRow> sessions;  // 已收藏的批次列表
        QString reason;
    };

    struct RemoveFavoriteResult {
        bool ok = false;
        QString reason;  // UNAUTHORIZED / SESSION_NOT_FOUND / DATABASE_ERROR
    };

    struct CheckFavoriteResult {
        bool ok = false;
        bool isFavorited = false;  // 是否已收藏
        QString reason;
    };

    // 添加收藏（按批次ID）
    AddFavoriteResult addFavorite(qint64 userId, qint64 sessionId);

    // 获取用户的收藏批次列表（加载前先清理孤儿收藏）
    GetFavoritesResult getFavorites(qint64 userId);

    // 取消收藏（按批次ID）
    RemoveFavoriteResult removeFavorite(qint64 userId, qint64 sessionId);

    // 检查某批次是否被该用户收藏
    CheckFavoriteResult checkFavorite(qint64 userId, qint64 sessionId);

private:
    FavoritesRepository m_repo;
    UploadRepository m_sessionRepo;   // 仅用于校验批次是否存在
};

#endif // FAVORITESSERVICE_H
