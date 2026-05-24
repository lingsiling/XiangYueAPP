#include "uploadservice.h"
#include "dbconnectionpool.h"

#include <QFileInfo>
#include <QSqlDatabase>
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