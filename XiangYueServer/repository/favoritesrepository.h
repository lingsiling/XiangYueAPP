// favoritesrepository.h
#ifndef FAVORITESREPOSITORY_H
#define FAVORITESREPOSITORY_H

#include "favoriterecord.h"
#include "uploadrepository.h"   // 复用 SessionRow（收藏列表按“批次”返回）
#include <QString>
#include <QList>
#include <optional>

// FavoritesRepository：数据访问层（只负责 SQL 操作）
// 收藏粒度 = 上传批次(session)：favorites.session_id 关联 upload_sessions.id
class FavoritesRepository
{
public:
    // 添加收藏：已存在则重新置为有效(软删除可恢复)，否则插入。返回收藏记录 ID，失败返回 nullopt
    std::optional<qint64> addFavorite(qint64 userId, qint64 sessionId);

    // 检查某批次是否已被该用户收藏（仅看 is_active = 1）
    bool isFavorited(qint64 userId, qint64 sessionId);

    // 获取用户已收藏的批次列表（联表 upload_sessions，按收藏更新时间倒序）
    QList<UploadRepository::SessionRow> getFavoriteSessionsByUserId(qint64 userId);

    // 取消收藏：软删除，仅把 is_active 置 0，保留历史
    bool removeFavorite(qint64 userId, qint64 sessionId);

    // 硬删除某批次的全部收藏记录：批次被彻底删除时级联清理（在删除批次的事务内调用）
    bool removeBySessionId(qint64 sessionId);

    // 清理孤儿收藏：批次已不存在却仍残留的收藏记录一并删除
    // 对应需求“检测到没有该批次则删除收藏记录”，加载收藏列表前调用
    bool purgeOrphans();
};

#endif // FAVORITESREPOSITORY_H
