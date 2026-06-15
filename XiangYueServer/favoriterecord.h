// favoriterecord.h
#ifndef FAVORITERECORD_H
#define FAVORITERECORD_H

#include <QString>

// FavoriteRecord：收藏记录数据结构（收藏作用在“上传批次/session”粒度上）
struct FavoriteRecord
{
    qint64 id = 0;
    qint64 userId = 0;
    qint64 sessionId = 0;   // 关联 upload_sessions.id（被收藏的批次）
    QString createdAt;
};

#endif // FAVORITERECORD_H
