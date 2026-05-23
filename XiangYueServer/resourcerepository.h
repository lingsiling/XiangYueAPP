#ifndef RESOURCEREPOSITORY_H
#define RESOURCEREPOSITORY_H

#include <QString>
#include <QList>
#include <optional>

/*
 * ResourceRecord：与 resources 表字段对应的数据结构。
 * Repository 层只负责 SQL，不关心文件从哪里来、为什么要删。
 */
struct ResourceRecord
{
    qint64 id = 0;
    QString filename;
    QString serverPath;
    qint64 size = 0;
    std::optional<qint64> uploaderUserId;
    QString uploadedAt;
};

class ResourceRepository
{
public:
    //按文件名查资源记录；不存在返回 nullopt
    std::optional<ResourceRecord> findByFileName(const QString &filename);

    //拉取全部资源记录，用于启动时和目录同步时做清理
    QList<ResourceRecord> listAll();

    //新增或更新资源记录，避免同名文件重复上传时产生脏数据
    bool upsert(const QString &filename,
                const QString &serverPath,
                qint64 size,
                std::optional<qint64> uploaderUserId = std::nullopt);

    // 按文件名删除资源记录
    bool deleteByFileName(const QString &filename);
};

#endif // RESOURCEREPOSITORY_H