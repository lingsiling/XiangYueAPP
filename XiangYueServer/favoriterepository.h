#ifndef FAVORITEREPOSITORY_H
#define FAVORITEREPOSITORY_H

#include <QString>
#include <QStringList>
#include <optional>

class FavoriteRepository
{
public:
    std::optional<qint64> ensureResource(const QString &resourceName);
    bool isFavorite(qint64 userId, qint64 resourceId);
    bool addFavorite(qint64 userId, qint64 resourceId);
    bool removeFavorite(qint64 userId, qint64 resourceId);
    QStringList listByUser(qint64 userId);
};

#endif // FAVORITEREPOSITORY_H
