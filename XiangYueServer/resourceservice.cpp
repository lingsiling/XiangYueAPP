#include "resourceservice.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QDebug>
#include <algorithm>

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

    // ====== 递归遍历 ServerSave 所有子目录下的文件 ======
    // 目录结构：ServerSave/user_<userId>/session_<id>/file.pdf
    QStringList allFilePaths;
    QStringList dirs;
    dirs.append(saveDir);

    while (!dirs.isEmpty()) {
        const QString curDir = dirs.takeFirst();
        QDir d(curDir);
        if (!d.exists()) continue;

        const QFileInfoList fileList = d.entryInfoList(QDir::Files | QDir::NoDotAndDotDot);
        for (const QFileInfo &info : fileList) {
            // 只用文件名做主键，子目录下的同名文件会被覆盖（符合 upsert 语义）
            if (m_repo.upsert(info.fileName(), info.absoluteFilePath(),
                              info.size(), uploaderUserId))
                ++r.touchedCount;
            allFilePaths.append(info.absoluteFilePath());
        }

        // 递归子目录
        const QStringList subDirs = d.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
        for (const QString &sd : subDirs) {
            dirs.append(curDir + "/" + sd);
        }
    }

    // 数据库里有、磁盘上没有的旧记录，直接清理掉，避免脏数据残留
    const QList<ResourceRecord> records = m_repo.listAll();
    for (const ResourceRecord &record : records) {
        const bool existsOnDisk = std::any_of(allFilePaths.begin(), allFilePaths.end(),
            [&](const QString &p) {
                return p.endsWith("/" + record.filename);
            });
        if (!existsOnDisk) {
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

    //上传完成后只同步当前文件，避免每次都扫描整个目录
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

bool ResourceService::updateResourceServerPath(const QString &oldSubDir, const QString &newSubDir, qint64 sessionId)
{
    return m_repo.updateServerPath(oldSubDir, newSubDir, sessionId);
}
