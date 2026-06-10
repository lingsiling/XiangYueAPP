#include "uploadservice.h"
#include "dbconnectionpool.h"

#include <QFileInfo>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QThread>

UploadService::RecordResult UploadService::recordUploadedFile(const QString &filePath, qint64 userId)
{
	RecordResult r;

	const QString path = filePath.trimmed();
	if (userId <= 0) {
		r.reason = "INVALID_USER";
		qWarning() << "[UploadService] invalid user id:" << userId << "for file" << path;
		return r;
	}

	QFileInfo info(path);
	if (!info.exists() || !info.isFile()) {
		r.reason = "FILE_NOT_FOUND";
		return r;
	}

	// 只在服务层做事务控制，Worker 不需要知道 SQL 细节
	QSqlDatabase db = DBConnectionPool::instance().connection();
	if (!db.isOpen() && !db.open()) {
		r.reason = "DB_OPEN_FAIL";
		qWarning() << "[UploadService] DB open fail for thread:" << QThread::currentThreadId();
		return r;
	}

	if (!db.transaction()) {
		r.reason = "TX_BEGIN_FAIL";
		qWarning() << "[UploadService] transaction begin failed";
		return r;
	}

	if (!m_resourceRepo.upsert(info.fileName(), info.absoluteFilePath(), info.size(), userId)) {
		db.rollback();
		r.reason = "RESOURCE_SAVE_FAIL";
		qWarning() << "[UploadService] resource upsert failed for" << info.fileName();
		return r;
	}

	const auto resourceOpt = m_resourceRepo.findByFileName(info.fileName());
	if (!resourceOpt.has_value()) {
		db.rollback();
		r.reason = "RESOURCE_LOOKUP_FAIL";
		qWarning() << "[UploadService] resource lookup failed after upsert for" << info.fileName();
		return r;
	}

	const auto uploadIdOpt = m_repo.insert(userId, resourceOpt->id);
	if (!uploadIdOpt.has_value()) {
		db.rollback();
		r.reason = "UPLOAD_SAVE_FAIL";
		qWarning() << "[UploadService] upload insert failed for resource id" << resourceOpt->id << "user" << userId;
		return r;
	}

	if (!db.commit()) {
		db.rollback();
		r.reason = "TX_COMMIT_FAIL";
		qWarning() << "[UploadService] tx commit failed";
		return r;
	}

	r.ok = true;
	r.resourceId = resourceOpt->id;
	r.uploadId = *uploadIdOpt;
	return r;
}

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

    // 参数校验：userId 必须有效，文件列表不能为空
    if (userId <= 0 || filePaths.isEmpty()) {
        r.reason = "INVALID_PARAM";
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
    q.prepare("INSERT INTO upload_sessions (user_id, tags, description, file_count)"
              " VALUES (?, ?, ?, ?)");
    q.addBindValue(userId);
    q.addBindValue(tags.isEmpty() ? QString() : tags.join('|'));
    // description 字段存入 "批次名|介绍" 格式，客户端可解析
    const QString fullDesc = desc.isEmpty()
        ? bname
        : QString("%1|%2").arg(bname, desc);
    q.addBindValue(fullDesc);
    q.addBindValue(filePaths.size());

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