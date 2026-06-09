#ifndef UPLOADSERVICE_H
#define UPLOADSERVICE_H

#include "resourcerepository.h"
#include "uploadrepository.h"

#include <QString>
#include <QStringList>
#include <QVector>

/*
 * UploadService：上传入库业务层。
 *
 * 说明：
 * - 只封装“上传完成后如何记录数据库”的业务意图
 * - 不直接碰 TCP socket、文件读写循环、UI 进度条
 * - 后续如果需要加去重、统计、日志，都优先放在这一层
 */
class UploadService
{
public:
    struct RecordResult {
        bool ok = false;
        QString reason;
        qint64 resourceId = 0;
        qint64 uploadId = 0;
    };

    // 批次上传的结果
    struct RecordBatchResult {
        bool ok = false;
        QString reason;
        qint64 sessionId = 0;          // upload_sessions.id（本次上传会话）
        QVector<qint64> resourceIds;   // 该批次所有文件的 resource.id
    };

    // 单文件上传（保持向后兼容）
    RecordResult recordUploadedFile(const QString &filePath, qint64 userId);

    // 批次上传：在事务内逐个写入文件记录 + 一条 upload_sessions 记录
    RecordBatchResult recordBatchUploadedFiles(const QStringList &filePaths,
                                               qint64 userId,
                                               const QStringList &tags,
                                               const QString &desc);

private:
    ResourceRepository m_resourceRepo;
    UploadRepository m_repo;
};

#endif // UPLOADSERVICE_H