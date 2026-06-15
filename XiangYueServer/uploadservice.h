#ifndef UPLOADSERVICE_H
#define UPLOADSERVICE_H

#include "resourcerepository.h"
#include "uploadrepository.h"
#include "favoritesrepository.h"

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
    // 批次上传的结果
    struct RecordBatchResult {
        bool ok = false;
        QString reason;
        qint64 sessionId = 0;          // upload_sessions.id（本次上传会话）
        QVector<qint64> resourceIds;   // 该批次所有文件的 resource.id
    };

    // 删除整个批次的结果
    struct DeleteSessionResult {
        bool ok = false;
        QString reason;
    };

    // 批次上传：在事务内逐个写入文件记录 + 一条 upload_sessions 记录
    RecordBatchResult recordBatchUploadedFiles(const QStringList &filePaths,
                                               qint64 userId,
                                               const QString &bname,
                                               const QStringList &tags,
                                               const QString &desc);

    // 删除整个批次：磁盘文件 + resources + uploads + favorites + upload_sessions 一并清理
    // requestUserId 用于归属校验，只能删除自己上传的批次
    DeleteSessionResult deleteSession(const QString &saveDir,
                                      qint64 sessionId,
                                      qint64 requestUserId);

    // 查询：列出所有批次
    QList<UploadRepository::SessionRow> listAllSessions();

    // 查询：列出某个用户上传的批次（"我的上传"用）
    QList<UploadRepository::SessionRow> listSessionsByUser(qint64 userId);

    // 查询：列出某个批次的文件
    QList<ResourceRecord> listSessionFiles(qint64 sessionId);

    // 查询：获取批次上传者用户名
    QString uploaderNameForSession(qint64 sessionId);

private:
    ResourceRepository m_resourceRepo;
    UploadRepository m_repo;
    FavoritesRepository m_favoritesRepo;   // 删除批次时级联清理该批次的收藏记录
};

#endif // UPLOADSERVICE_H