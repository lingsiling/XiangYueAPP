#include "commentservice.h"

CommentService::AddResult CommentService::addComment(qint64 userId,
                                                     const QString &resourceName,
                                                     const QString &content)
{
    AddResult r;

    const QString rn = resourceName.trimmed();
    if (userId <= 0 || rn.isEmpty() || content.isEmpty()) {
        r.ok = false;
        r.reason = "INVALID_FORMAT";
        return r;
    }

    // 允许换行，但建议限制长度防止恶意刷屏/爆内存（可按需求调整）
    if (content.size() > 2000) {
        r.ok = false;
        r.reason = "TOO_LONG";
        return r;
    }

    auto idOpt = m_repo.insert(rn, userId, content);
    if (!idOpt.has_value()) {
        r.ok = false;
        r.reason = "SERVER_ERROR";
        return r;
    }

    r.ok = true;
    r.commentId = *idOpt;
    return r;
}

CommentService::ListResult CommentService::listComments(const QString &resourceName)
{
    ListResult r;

    const QString rn = resourceName.trimmed();
    if (rn.isEmpty()) {
        r.ok = false;
        r.reason = "INVALID_FORMAT";
        return r;
    }

    r.items = m_repo.listByResource(rn);
    r.ok = true;
    return r;
}

CommentService::DeleteResult CommentService::deleteComment(qint64 userId, qint64 commentId)
{
    DeleteResult r;

    if (userId <= 0 || commentId <= 0) {
        r.ok = false;
        r.reason = "INVALID_FORMAT";
        return r;
    }

    // 先查是否存在
    auto recOpt = m_repo.findById(commentId);
    if (!recOpt.has_value()) {
        r.ok = false;
        r.reason = "NOT_FOUND";
        return r;
    }

    // 权限校验：只能删除自己发布的评论
    if (recOpt->userId != userId) {
        r.ok = false;
        r.reason = "NOT_OWNER";
        return r;
    }

    if (!m_repo.deleteById(commentId)) {
        r.ok = false;
        r.reason = "SERVER_ERROR";
        return r;
    }

    r.ok = true;
    return r;
}