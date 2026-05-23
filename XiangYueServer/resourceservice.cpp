#include "resourceservice.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QDebug>

ResourceService::SyncResult ResourceService::syncDirectory(const QString &saveDir,
                                                           std::optional<qint64> uploaderUserId)
{
    SyncResult r;

    //启动时先确保目录存在，再把目录里的文件全部写入 resources 表
    QDir dir(saveDir);
    if (!dir.exists() && !QDir().mkpath(saveDir)) {
        r.ok = false;
        r.reason = "SAVE_DIR_CREATE_FAIL";
        return r;
    }

    const QFileInfoList files = dir.entryInfoList(QDir::Files | QDir::NoDotAndDotDot);
    for (const QFileInfo &info : files) {
        //逐个文件同步，保证历史文件也能入库
        if (m_repo.upsert(info.fileName(), info.absoluteFilePath(), info.size(), uploaderUserId))
            ++r.touchedCount;
    }

    //数据库里有、磁盘上没有的旧记录，直接清理掉，避免脏数据残留
    const QList<ResourceRecord> records = m_repo.listAll();
    for (const ResourceRecord &record : records) {
        if (!dir.exists(record.filename)) {
            if (m_repo.deleteByFileName(record.filename))
                ++r.removedCount;
        }
    }

    r.ok = true;
    return r;
}

ResourceService::SyncResult ResourceService::syncSingleFile(const QString &filePath,
                                                           std::optional<qint64> uploaderUserId)
{
    SyncResult r;

    // 上传完成后只同步当前文件，避免每次都扫描整个目录
    QFileInfo info(filePath);
    if (!info.exists() || !info.isFile()) {
        r.ok = false;
        r.reason = "FILE_NOT_FOUND";
        return r;
    }

    if (!m_repo.upsert(info.fileName(), info.absoluteFilePath(), info.size(), uploaderUserId)) {
        r.ok = false;
        r.reason = "SERVER_ERROR";
        return r;
    }

    r.ok = true;
    r.touchedCount = 1;
    return r;
}

ResourceService::DeleteResult ResourceService::deleteFileAndRecord(const QString &saveDir,
                                                                   const QString &fileName)
{
    DeleteResult r;

    //删除命令要同时处理文件和数据库，保证界面刷新后不会看到残留资源
    const QString name = fileName.trimmed();
    if (name.isEmpty()) {
        r.ok = false;
        r.reason = "INVALID_FORMAT";
        return r;
    }

    const QString fullPath = QDir(saveDir).filePath(name);

    //文件存在时先删文件；如果文件本来就没了，也继续清理数据库，避免残留脏记录
    if (QFileInfo::exists(fullPath)) {
        if (!QFile::remove(fullPath)) {
            r.ok = false;
            r.reason = "FILE_DELETE_FAIL";
            return r;
        }
    }

    if (!m_repo.deleteByFileName(name)) {
        r.ok = false;
        r.reason = "SERVER_ERROR";
        return r;
    }

    r.ok = true;
    return r;
}

bool ResourceService::removeRecordOnly(const QString &fileName)
{
    //仅在确实不需要磁盘文件时使用这个兜底接口
    return m_repo.deleteByFileName(fileName);
}