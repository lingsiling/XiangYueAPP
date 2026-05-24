#ifndef UPLOADREPOSITORY_H
#define UPLOADREPOSITORY_H

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
    // 新增一条上传记录：由上层传入用户 ID 和资源 ID
    std::optional<qint64> insert(qint64 userId, qint64 resourceId);

    // 按文件名新增上传记录：后续如果改成按文件名关联，可在 service 层直接调用
    std::optional<qint64> insertByFileName(qint64 userId, const QString &filename);

    // 按资源 ID 删除上传记录：用于资源删除时同步清理 uploads
    bool deleteByResourceId(qint64 resourceId);
};

#endif // UPLOADREPOSITORY_H