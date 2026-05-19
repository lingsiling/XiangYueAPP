#ifndef FAVORITESERVICE_H
#define FAVORITESERVICE_H

#include "favoriterepository.h"

class FavoriteService
{
public:
    struct ToggleResult {
        bool ok = false;
        QString reason;
    };

    struct ListResult {
        bool ok = false;
        QString reason;
        QStringList items;
    };

    struct StatusResult {
        bool ok = false;
        QString reason;
        bool isFavorite = false;
    };

    ToggleResult addFavorite(qint64 userId, const QString &resourceName);
    ToggleResult removeFavorite(qint64 userId, const QString &resourceName);
    ListResult listFavorites(qint64 userId);
    StatusResult favoriteStatus(qint64 userId, const QString &resourceName);

private:
    FavoriteRepository m_repo;
};

#endif // FAVORITESERVICE_H
