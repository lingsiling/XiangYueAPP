// commentrepository.h
#ifndef COMMENTREPOSITORY_H
#define COMMENTREPOSITORY_H

#include "commentrecord.h"
#include <QString>
#include <QList>
#include <optional>

//CommentRepository：Repository 层只负责 SQL（增/查），不做业务规则，不关心网络协议
class CommentRepository
{
public:
    // 插入评论，成功返回 commentId，失败返回 nullopt
    std::optional<qint64> insert(const QString &resourceName,
                                 qint64 userId,
                                 const QString &content);

    // 根据资源名拉取评论列表（包含 username），默认最新在上
    QList<CommentRecord> listByResource(const QString &resourceName);
};

#endif // COMMENTREPOSITORY_H