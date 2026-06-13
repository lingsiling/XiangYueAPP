#ifndef RESOURCESERVICE_H
#define RESOURCESERVICE_H

#include "resourcerepository.h"

#include <QString>
#include <optional>

// ResourceService：资源业务层。
// 负责把“文件系统上的文件”与“resources 表”同步起来，避免 SQL 逻辑散落到 Worker/UI 中。
class ResourceService
{
public:
    struct SyncResult {
        bool ok = false;
        QString reason;
        int touchedCount = 0;
        int removedCount = 0;
    };

    //同步整个目录：把目录中的文件写入 resources 表，并清理已经不存在的旧记录
    SyncResult syncDirectory(const QString &saveDir,
                             std::optional<qint64> uploaderUserId = std::nullopt);

    //同步单个文件：上传完成后调用
    SyncResult syncSingleFile(const QString &filePath,
                              std::optional<qint64> uploaderUserId = std::nullopt);

    // 更新资源路径子目录（批次重命名 batch_xxx → session_<id>）
    bool updateResourceServerPath(const QString &oldSubDir, const QString &newSubDir, qint64 sessionId);
private:
    ResourceRepository m_repo;
};

#endif // RESOURCESERVICE_H