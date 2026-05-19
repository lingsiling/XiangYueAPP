#include "favoriteservice.h"

FavoriteService::ToggleResult FavoriteService::addFavorite(qint64 userId, const QString &resourceName)
{
    ToggleResult r;

    const QString rn = resourceName.trimmed();
    if (userId <= 0 || rn.isEmpty()) {
        r.reason = "INVALID_FORMAT";
        return r;
    }

    const auto resourceIdOpt = m_repo.ensureResource(rn);
    if (!resourceIdOpt.has_value()) {
        r.reason = "SERVER_ERROR";
        return r;
    }

    if (!m_repo.addFavorite(userId, *resourceIdOpt)) {
        r.reason = "SERVER_ERROR";
        return r;
    }

    r.ok = true;
    return r;
}

FavoriteService::ToggleResult FavoriteService::removeFavorite(qint64 userId, const QString &resourceName)
{
    ToggleResult r;

    const QString rn = resourceName.trimmed();
    if (userId <= 0 || rn.isEmpty()) {
        r.reason = "INVALID_FORMAT";
        return r;
    }

    const auto resourceIdOpt = m_repo.ensureResource(rn);
    if (!resourceIdOpt.has_value()) {
        r.reason = "SERVER_ERROR";
        return r;
    }

    if (!m_repo.removeFavorite(userId, *resourceIdOpt)) {
        r.reason = "SERVER_ERROR";
        return r;
    }

    r.ok = true;
    return r;
}

FavoriteService::ListResult FavoriteService::listFavorites(qint64 userId)
{
    ListResult r;

    if (userId <= 0) {
        r.reason = "INVALID_FORMAT";
        return r;
    }

    r.items = m_repo.listByUser(userId);
    r.ok = true;
    return r;
}

FavoriteService::StatusResult FavoriteService::favoriteStatus(qint64 userId, const QString &resourceName)
{
    StatusResult r;

    const QString rn = resourceName.trimmed();
    if (userId <= 0 || rn.isEmpty()) {
        r.reason = "INVALID_FORMAT";
        return r;
    }

    const auto resourceIdOpt = m_repo.ensureResource(rn);
    if (!resourceIdOpt.has_value()) {
        r.reason = "SERVER_ERROR";
        return r;
    }

    r.isFavorite = m_repo.isFavorite(userId, *resourceIdOpt);
    r.ok = true;
    return r;
}
