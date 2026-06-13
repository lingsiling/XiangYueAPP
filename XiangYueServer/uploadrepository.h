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
    // 按 session_id 查询该批次的所有文件记录
    // 返回值是 resources 表中属于该批次的全部条目
    QList<ResourceRecord> listBySessionId(qint64 sessionId);

    // 查询所有批次（upload_sessions 表），按时间倒序
    struct SessionRow {
        qint64 id = 0;
        qint64 userId = 0;
        QString title;          // 批次名
        QString tags;
        QString description;    // 资源介绍
        int fileCount = 0;
        QString createdAt;
    };
    QList<SessionRow> listAllSessions();

    // 查询某个用户的全部批次（upload_sessions 表），按时间倒序
    // 用于"我的上传"——只列出当前登录用户自己上传的批次
    QList<SessionRow> listSessionsByUser(qint64 userId);

    // 根据 sessionId 查询上传者用户名
    QString uploaderNameBySessionId(qint64 sessionId);

    // 查询某个批次的所属用户ID（用于删除批次时做归属校验、定位磁盘目录）
    // 批次不存在时返回 std::nullopt
    std::optional<qint64> sessionUserId(qint64 sessionId);

    // 删除某批次在 uploads 表中的全部关联记录（按 session_id 整批清理）
    bool deleteUploadsBySessionId(qint64 sessionId);

    // 删除 upload_sessions 表中的批次记录本身
    bool deleteSessionRow(qint64 sessionId);
};

#endif // UPLOADREPOSITORY_H