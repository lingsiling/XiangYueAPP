#include "uploadservice.h"
#include "dbconnectionpool.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QDebug>

// ====== 批次上传入库 ======
// 功能：在一次数据库事务中完成以下操作：
//   1. 在 upload_sessions 表创建一条"上传批次"记录（含标签、介绍、文件数）
//   2. 对每个文件：写入 resources 表 + 写入 uploads 表（关联 session_id）
//   3. 提交事务（如果任何一步失败，全部回滚，保证数据一致性）
//
// 参数：
//   filePaths - 本次上传的所有文件绝对路径（服务端已存盘）
//   userId    - 上传者ID
//   tags      - 用户输入的标签列表（如 {"数学", "PPT"}）
//   desc      - 用户输入的资源介绍文字
//
// 返回值：
//   RecordBatchResult.ok        - 是否成功
//   RecordBatchResult.sessionId - 新创建的 upload_sessions.id
//   RecordBatchResult.resourceIds - 所有文件的 resources.id 列表
// ============================================================
UploadService::RecordBatchResult UploadService::recordBatchUploadedFiles(
    const QStringList &filePaths,
    qint64 userId,
    const QString &bname,
    const QStringList &tags,
    const QString &desc)
{
    RecordBatchResult r;

    // ====== 参数校验 ======
    if (userId <= 0 || filePaths.isEmpty()) {
        r.reason = "INVALID_PARAM";
        return r;
    }
    if (bname.trimmed().isEmpty()) {
        r.reason = "RESOURCE_NAME_EMPTY";       // 资源名称必填
        return r;
    }
    if (bname.length() > 100) {
        r.reason = "RESOURCE_NAME_TOO_LONG";    // 名称过长（数据库友好）
        return r;
    }
    // 标签至少一个
    if (tags.isEmpty()) {
        r.reason = "TAG_EMPTY";                 // 标签必填
        return r;
    }

    // 获取本线程的数据库连接（线程安全的连接池）
    QSqlDatabase db = DBConnectionPool::instance().connection();
    if (!db.isOpen() && !db.open()) {
        r.reason = "DB_OPEN_FAIL";
        return r;
    }

    // 开启事务：保证批次元数据 + 全部文件 + 全部上传记录 要么全写、要么全回滚
    if (!db.transaction()) {
        r.reason = "TX_BEGIN_FAIL";
        return r;
    }

    // ====== 第1步：创建 upload_sessions 记录（这一条代表"一次上传"） ======
    QSqlQuery q(db);
    q.prepare("INSERT INTO upload_sessions (user_id, title, tags, description, file_count)"
              " VALUES (?, ?, ?, ?, ?)");
    q.addBindValue(userId);
    q.addBindValue(bname.isEmpty() ? QString() : bname);           // 批次名（新列）
    q.addBindValue(tags.isEmpty() ? QString() : tags.join('|'));   // 标签
    q.addBindValue(desc.isEmpty() ? QString() : desc);             // 资源介绍
    q.addBindValue(filePaths.size());                               // 文件数

    if (!q.exec()) {
        db.rollback();                                            // 失败回滚
        r.reason = "SESSION_INSERT_FAIL";
        return r;
    }

    const qint64 sessionId = q.lastInsertId().toLongLong();       // 记住批次ID

    // ====== 第2步：逐个文件写入 resources + uploads（全部关联同一个 session_id） ======
    for (const QString &filePath : filePaths) {
        QFileInfo info(filePath.trimmed());

        // 文件不存在则回滚整个批次（保证数据一致性）
        if (!info.exists() || !info.isFile()) {
            db.rollback();
            r.reason = "FILE_NOT_FOUND:" + info.fileName();
            return r;
        }

        // 2a. 写入 resources 表（记录文件元数据：文件名、路径、大小、上传者）
        if (!m_resourceRepo.upsert(info.fileName(), info.absoluteFilePath(),
                                   info.size(), userId)) {
            db.rollback();
            r.reason = "RESOURCE_SAVE_FAIL:" + info.fileName();
            return r;
        }

        // 2b. 查找刚写入的 resource_id（upsert 后按文件名查回主键）
        const auto resOpt = m_resourceRepo.findByFileName(info.fileName());
        if (!resOpt.has_value()) {
            db.rollback();
            r.reason = "RESOURCE_LOOKUP_FAIL:" + info.fileName();
            return r;
        }

        // 2c. 写入 uploads 表（记录上传行为：谁、哪个文件、哪个批次）
        q.prepare("INSERT INTO uploads (user_id, resource_id, session_id) VALUES (?, ?, ?)");
        q.addBindValue(userId);
        q.addBindValue(resOpt->id);                                  // resources.id
        q.addBindValue(sessionId);                                   // upload_sessions.id

        if (!q.exec()) {
            db.rollback();
            r.reason = "UPLOAD_INSERT_FAIL:" + info.fileName();
            return r;
        }

        // 记录文件ID供调用方使用
        r.resourceIds.append(resOpt->id);
    }

    // ====== 第3步：提交事务 ======
    // 全部文件写成功 → commit；任何一步失败 → rollback（前面已处理）
    if (!db.commit()) {
        db.rollback();
        r.reason = "TX_COMMIT_FAIL";
        return r;
    }

    r.ok = true;
    r.sessionId = sessionId;     // 返回批次ID给调用方（ClientWorker 用于回复 BATCH_OK##sessionId）
    return r;
}

// ====== 查询：列出所有批次（委托给 UploadRepository） ======
QList<UploadRepository::SessionRow> UploadService::listAllSessions()
{
    return m_repo.listAllSessions();
}

// ====== 查询：列出某个用户上传的批次（"我的上传"用） ======
QList<UploadRepository::SessionRow> UploadService::listSessionsByUser(qint64 userId)
{
    return m_repo.listSessionsByUser(userId);
}

// ====== 删除整个批次 ======
// 功能：把"一次上传"产生的全部痕迹彻底清理干净，包括：
//   - 磁盘：ServerSave/user_<uid>/session_<sid>/ 目录及其中所有文件
//   - 数据库：该批次的 resources 行、favorites 行、uploads 关联、upload_sessions 批次记录
//
// 设计要点：
//   1. 数据库改动放在一个事务里，任一步失败即全部回滚，保证不会删一半。
//   2. 先提交数据库事务、再删磁盘文件：若先删文件而事务回滚，数据库就会指向已不存在的文件；
//      反过来，提交后即便磁盘清理失败，最坏只是留下无主文件，不会破坏数据一致性。
//   3. 归属校验：只能删除 requestUserId 自己上传的批次，避免越权删除他人资源。
//
// 关于 deleteByFileName：resources.filename 全局唯一，同名文件在表中只有一行，
// 因此按文件名删除与既有的单文件删除行为一致，不是本次引入的新风险。
UploadService::DeleteSessionResult UploadService::deleteSession(const QString &saveDir,
                                                                qint64 sessionId,
                                                                qint64 requestUserId)
{
    DeleteSessionResult r;

    // ====== 第1步：参数校验 ======
    if (sessionId <= 0 || requestUserId <= 0) {
        r.reason = "INVALID_PARAM";
        return r;
    }

    // ====== 第2步：归属校验（只能删自己的批次） ======
    const auto ownerOpt = m_repo.sessionUserId(sessionId);
    if (!ownerOpt.has_value()) {
        r.reason = "SESSION_NOT_FOUND";
        return r;
    }
    const qint64 ownerId = *ownerOpt;
    if (ownerId != requestUserId) {
        r.reason = "NOT_OWNER";
        return r;
    }

    // ====== 第3步：先取出该批次的全部文件（filename / serverPath 供后续删磁盘用） ======
    const QList<ResourceRecord> files = m_repo.listBySessionId(sessionId);

    // ====== 第4步：开启事务，统一清理数据库 ======
    QSqlDatabase db = DBConnectionPool::instance().connection();
    if (!db.isOpen() && !db.open()) {
        r.reason = "DB_OPEN_FAIL";
        return r;
    }
    if (!db.transaction()) {
        r.reason = "TX_BEGIN_FAIL";
        return r;
    }

    // 4a. 逐个删除文件对应的 resources 行（deleteByFileName 内部已级联删 favorites）
    for (const ResourceRecord &rec : files) {
        if (!m_resourceRepo.deleteByFileName(rec.filename)) {
            db.rollback();
            r.reason = "RESOURCE_DELETE_FAIL:" + rec.filename;
            return r;
        }
    }

    // 4b. 删除该批次在 uploads 表中的全部关联记录
    if (!m_repo.deleteUploadsBySessionId(sessionId)) {
        db.rollback();
        r.reason = "UPLOAD_DELETE_FAIL";
        return r;
    }

    // 4c. 删除批次记录本身
    if (!m_repo.deleteSessionRow(sessionId)) {
        db.rollback();
        r.reason = "SESSION_DELETE_FAIL";
        return r;
    }

    // ====== 第5步：提交事务 ======
    if (!db.commit()) {
        db.rollback();
        r.reason = "TX_COMMIT_FAIL";
        return r;
    }

    // ====== 第6步：提交成功后再清理磁盘（尽力而为，失败不影响整体结果） ======
    for (const ResourceRecord &rec : files) {
        const QString path = rec.serverPath.trimmed();
        if (!path.isEmpty() && QFileInfo::exists(path))
            QFile::remove(path);
    }
    // 整批文件都在同一个 session 目录下，递归删掉该目录（连同可能残留的文件）
    const QString sessionDir = QDir(saveDir).filePath(
        QString("user_%1/session_%2").arg(ownerId).arg(sessionId));
    QDir dir(sessionDir);
    if (dir.exists())
        dir.removeRecursively();

    r.ok = true;
    return r;
}

// ====== 查询：列出某个批次的文件 ======
QList<ResourceRecord> UploadService::listSessionFiles(qint64 sessionId)
{
    return m_repo.listBySessionId(sessionId);
}

// ====== 查询：获取批次上传者用户名 ======
QString UploadService::uploaderNameForSession(qint64 sessionId)
{
    return m_repo.uploaderNameBySessionId(sessionId);
}