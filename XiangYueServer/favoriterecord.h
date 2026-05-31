// favoriterecord.h
#ifndef FAVORITERECORD_H
#define FAVORITERECORD_H

#include <QString>

// FavoriteRecord：收藏记录数据结构
struct FavoriteRecord
{
    qint64 id = 0;
    qint64 userId = 0;
    qint64 resourceId = 0;
    QString createdAt;
};

#endif // FAVORITERECORD_H
