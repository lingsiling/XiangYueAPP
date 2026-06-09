#ifndef UPLOADREPOSITORY_H
#define UPLOADREPOSITORY_H

#include "resourcerepository.h"

#include <QString>
#include <optional>

#include <QtGlobal>

/*
 * UploadRepository：上传记录的数据访问层。
 *
 * 说明：
 * - 这里只声明数据库操作接口，不参与 TCP 收发
 * - 上层是否在上传完成后调用，由 service/worker 决定
 * - 这样可以保持与文件传输逻辑低耦合
 */
class UploadRepository
{
public:
    std::optional<qint64> insert(qint64 userId, qint64 resourceId);

    // 按 session_id 查询该批次的所有文件记录
    // 返回值是 resources 表中属于该批次的全部条目
    QList<ResourceRecord> listBySessionId(qint64 sessionId);

    // 查询所有批次（upload_sessions 表），按时间倒序
    struct SessionRow {
        qint64 id = 0;
        qint64 userId = 0;
        QString tags;
        QString description;
        int fileCount = 0;
        QString createdAt;
    };
    QList<SessionRow> listAllSessions();

    std::optional<qint64> insertByFileName(qint64 userId, const QString &filename);
    bool deleteByResourceId(qint64 resourceId);
};

#endif // UPLOADREPOSITORY_H