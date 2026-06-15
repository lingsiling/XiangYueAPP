// commentservice.h
#ifndef COMMENTSERVICE_H
#define COMMENTSERVICE_H

#include "commentrepository.h"
#include <QString>
#include <QList>

// CommentService：业务层（校验参数、限制长度、返回错误码）
// - 不负责：TCP 拆包/回包（Worker 做）
// - 不负责：SQL 细节（Repository 做）
class CommentService
{
public:
    struct AddResult {
        bool ok = false;
        QString reason;      // INVALID_FORMAT / TOO_LONG / SERVER_ERROR
        qint64 commentId = 0;
    };

    struct ListResult {
        bool ok = false;
        QString reason;      // INVALID_FORMAT / SERVER_ERROR
        QList<CommentRecord> items;
    };

    struct DeleteResult {
        bool ok = false;
        QString reason;  // INVALID_FORMAT / NOT_FOUND / NOT_OWNER / SERVER_ERROR
    };

    AddResult addComment(qint64 userId, const QString &resourceName, const QString &content);
    ListResult listComments(const QString &resourceName);
    DeleteResult deleteComment(qint64 userId, qint64 commentId);

private:
    CommentRepository m_repo;
};

#endif // COMMENTSERVICE_H