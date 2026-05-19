#include "favoriterepository.h"
#include "dbmanager.h"

#include <QSqlError>
#include <QSqlQuery>
#include <QVariant>
#include <QDebug>

std::optional<qint64> FavoriteRepository::ensureResource(const QString &resourceName)
{
    // 先查 resources 是否已有该 filename 的主键
    QSqlQuery findQuery(DBManager::instance().db());
    findQuery.prepare("SELECT id FROM resources WHERE filename = ?");
    findQuery.addBindValue(resourceName);

    if (!findQuery.exec()) {
        qDebug() << "[FavoriteRepo] ensureResource find failed:" << findQuery.lastError().text();
        return std::nullopt;
    }

    if (findQuery.next())
        return findQuery.value(0).toLongLong();

    // 若不存在则补建一条最小记录，保证 favorites 可落 resource_id
    // 注意：这里不写 uploader/size/path，因为收藏逻辑只依赖资源唯一标识
    QSqlQuery insertQuery(DBManager::instance().db());
    insertQuery.prepare("INSERT INTO resources(filename) VALUES(?)");
    insertQuery.addBindValue(resourceName);

    if (!insertQuery.exec()) {
        qDebug() << "[FavoriteRepo] ensureResource insert failed:" << insertQuery.lastError().text();
        return std::nullopt;
    }

    return insertQuery.lastInsertId().toLongLong();
}

bool FavoriteRepository::isFavorite(qint64 userId, qint64 resourceId)
{
    // 使用 EXISTS 等价查询（SELECT 1 + LIMIT 1），只关心是否存在，不拉整行
    QSqlQuery q(DBManager::instance().db());
    q.prepare("SELECT 1 FROM favorites WHERE user_id = ? AND resource_id = ? LIMIT 1");
    q.addBindValue(userId);
    q.addBindValue(resourceId);

    if (!q.exec()) {
        qDebug() << "[FavoriteRepo] isFavorite failed:" << q.lastError().text();
        return false;
    }

    return q.next();
}

bool FavoriteRepository::addFavorite(qint64 userId, qint64 resourceId)
{
    // 利用 UNIQUE(user_id, resource_id) + INSERT OR IGNORE 实现“幂等收藏”
    // 重复收藏不会报错，业务层可统一按成功处理
    QSqlQuery q(DBManager::instance().db());
    q.prepare("INSERT OR IGNORE INTO favorites(user_id, resource_id) VALUES(?, ?)");
    q.addBindValue(userId);
    q.addBindValue(resourceId);

    if (!q.exec()) {
        qDebug() << "[FavoriteRepo] addFavorite failed:" << q.lastError().text();
        return false;
    }

    return true;
}

bool FavoriteRepository::removeFavorite(qint64 userId, qint64 resourceId)
{
    // 删除同样按幂等语义处理：目标不存在时 DELETE 也视为成功
    QSqlQuery q(DBManager::instance().db());
    q.prepare("DELETE FROM favorites WHERE user_id = ? AND resource_id = ?");
    q.addBindValue(userId);
    q.addBindValue(resourceId);

    if (!q.exec()) {
        qDebug() << "[FavoriteRepo] removeFavorite failed:" << q.lastError().text();
        return false;
    }

    return true;
}

QStringList FavoriteRepository::listByUser(qint64 userId)
{
    QStringList out;

    // favorites 存的是 resource_id，展示层需要 filename
    // 因此在仓储层做关联查询，避免上层关心 SQL 细节
    QSqlQuery q(DBManager::instance().db());
    q.prepare(R"SQL(
        SELECT r.filename
        FROM favorites f
        INNER JOIN resources r ON r.id = f.resource_id
        WHERE f.user_id = ?
        ORDER BY f.created_at DESC, r.id DESC
    )SQL");
    q.addBindValue(userId);

    if (!q.exec()) {
        qDebug() << "[FavoriteRepo] listByUser failed:" << q.lastError().text();
        return out;
    }

    while (q.next()) {
        out << q.value(0).toString();
    }
    return out;
}
