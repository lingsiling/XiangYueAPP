#ifndef UPLOADSERVICE_H
#define UPLOADSERVICE_H

#include "resourcerepository.h"
#include "uploadrepository.h"

#include <QString>

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

    // 上传完成后调用：由业务层统一决定如何入库
    RecordResult recordUploadedFile(const QString &filePath, qint64 userId);

private:
    ResourceRepository m_resourceRepo;
    UploadRepository m_repo;
};

#endif // UPLOADSERVICE_H