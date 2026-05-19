#include "favoriteservice.h"

FavoriteService::ToggleResult FavoriteService::addFavorite(qint64 userId, const QString &resourceName)
{
    ToggleResult r;

    // 业务层职责：先做参数校验，尽早失败，避免无意义 DB 访问
    const QString rn = resourceName.trimmed();
    if (userId <= 0 || rn.isEmpty()) {
        r.reason = "INVALID_FORMAT";
        return r;
    }

    // favorites 表依赖 resource_id，先保证 resources 中存在对应记录
    // 若不存在则由仓储层补建（仅补最小可用字段 filename）
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

    // 删除收藏同样依赖 resource_id 定位，流程与 add 对齐，保持对称
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

    // 业务层不拼协议，只返回结构化数据，协议格式由 ClientWorker 负责
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

    // 收藏状态查询流程：
    // 1) 解析资源名并拿到 resource_id
    // 2) 判断 favorites(user_id, resource_id) 是否存在
    const auto resourceIdOpt = m_repo.ensureResource(rn);
    if (!resourceIdOpt.has_value()) {
        r.reason = "SERVER_ERROR";
        return r;
    }

    r.isFavorite = m_repo.isFavorite(userId, *resourceIdOpt);
    r.ok = true;
    return r;
}
