// clientworker.cpp — 单连接协议会话实现
//
// 本文件【不含 Winsock】：解析协议、把业务投递给 Connection 的串行执行器，
// 并用 Connection::postSend() 交回响应字节。网络收发的细节全在 Connection 内。
#include "clientworker.h"
#include "connection.h"
#include "serverconfig.h"

#include "authservice.h"
#include "commentservice.h"
#include "resourceservice.h"
#include "favoritesservice.h"
#include "uploadservice.h"
#include "userrepository.h"

#include <QDir>
#include <QFileInfo>
#include <QDateTime>
#include <QDebug>

// ============================================================================
//  文件级静态助手（不依赖 ClientWorker 实例，可安全在业务线程的任务里调用）
// ============================================================================
namespace {

// 把一个已打开的文件的内容按 64KB 分块经发送队列串流给客户端。
// 在业务线程执行：每次 postSend 都是线程安全入队；背压会在队列积压时自动节流。
void streamFileBody(Connection *c, QFile &f)
{
    while (!f.atEnd()) {
        const QByteArray chunk = f.read(64 * 1024);
        if (chunk.isEmpty())
            break;
        c->postSend(chunk);
    }
}

// 在 saveDir 及其所有子目录中递归查找名为 fileName 的文件，找到后把
// "响应头 + 文件二进制"串流给客户端。forPreview 决定响应头关键字（预览/下载），
// 与重构前 ClientWorker::sendFile 的字节输出完全一致。
void sendFileToConn(Connection *c, const QString &saveDir,
                    const QString &fileName, bool forPreview)
{
    const char *dataTag = forPreview ? "PREVIEW_FILE" : "FILE";
    const char *failTag = forPreview ? "PREVIEW_FAIL" : "FILE_FAIL";

    // 递归查找文件（目录结构：ServerSave/user_<id>/session_<id>/file）
    QString foundPath;
    QStringList dirs;
    dirs.append(saveDir);
    while (!dirs.isEmpty()) {
        const QString currentDir = dirs.takeFirst();
        QDir dir(currentDir);
        if (!dir.exists())
            continue;

        const QFileInfoList files = dir.entryInfoList(QDir::Files);
        for (const QFileInfo &fi : files) {
            if (fi.fileName() == fileName) {
                foundPath = fi.absoluteFilePath();
                break;
            }
        }
        if (!foundPath.isEmpty())
            break;

        const QStringList subDirs = dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
        for (const QString &sd : subDirs)
            dirs.append(currentDir + "/" + sd);
    }

    if (foundPath.isEmpty()) {
        c->postSend(QString("%1##FILE_NOT_FOUND\n").arg(failTag).toUtf8());
        return;
    }

    QFile f(foundPath);
    if (!f.open(QIODevice::ReadOnly)) {
        c->postSend(QString("%1##OPEN_FAIL\n").arg(failTag).toUtf8());
        return;
    }

    const qint64 size = f.size();
    const QString nameOnly = QFileInfo(fileName).fileName();
    // 响应头：FILE##name##size\n 或 PREVIEW_FILE##name##size\n
    c->postSend(QString("%1##%2##%3\n").arg(dataTag).arg(nameOnly).arg(size).toUtf8());
    streamFileBody(c, f);
    f.close();
}

// 递归遍历 saveDir，构造 "LIST##rel1##rel2##...\n" 完整资源列表消息。
QByteArray buildFileListMessage(const QString &saveDir)
{
    QStringList list;
    QStringList dirs;
    dirs.append(saveDir);
    while (!dirs.isEmpty()) {
        const QString currentDir = dirs.takeFirst();
        QDir dir(currentDir);
        if (!dir.exists())
            continue;

        const QStringList files = dir.entryList(QDir::Files);
        for (const QString &fn : files) {
            const QString relativePath =
                QDir(saveDir).relativeFilePath(currentDir + "/" + fn);
            list.append(relativePath);
        }
        const QStringList subDirs = dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
        for (const QString &sd : subDirs)
            dirs.append(currentDir + "/" + sd);
    }
    return (QString("LIST##") + list.join("##") + "\n").toUtf8();
}

} // namespace

// ============================================================================
//  ClientWorker
// ============================================================================
ClientWorker::ClientWorker(Connection *conn)
    : m_conn(conn)
{
}

ClientWorker::~ClientWorker()
{
    if (m_uploadFile.isOpen())
        m_uploadFile.close();
}

void ClientWorker::onDataReceived(const char *data, int len)
{
    m_buf.append(data, len);

    // 循环处理：一批 TCP 数据里可能粘有多条命令 / 多个文件
    while (!m_buf.isEmpty()) {
        if (!m_isUploadIdle)
            consumeUploadData();

        // 仍在接收文件二进制，等下一批数据
        if (!m_isUploadIdle)
            return;

        // 空闲态：有完整行就继续解析命令，否则等更多数据
        if (m_buf.contains('\n'))
            tryProcessLines();
        else
            break;
    }
}

void ClientWorker::tryProcessLines()
{
    while (true) {
        int pos = m_buf.indexOf('\n');
        if (pos < 0)
            break;

        const QByteArray raw = m_buf.left(pos);
        m_buf.remove(0, pos + 1);

        const QString line = QString::fromUtf8(raw).trimmed();

        if (line == "LIST_SESSIONS") {
            handleListSessionsCommand();
        } else if (line.startsWith("SESSION_FILES##")) {
            handleSessionFilesCommand(line);
        } else if (line.startsWith("DOWNLOAD##")) {
            handleDownloadCommand(line.section("##", 1, 1).trimmed());
        } else if (line.startsWith("PREVIEW##")) {
            handlePreviewCommand(line.section("##", 1, 1).trimmed());
        }
        // —— 上传类命令 ——
        else if (line.startsWith("UPLOAD_BATCH##")) {
            handleBatchUploadCommand(line);   // 批次模式入口，后续 FILE## 头跟随
        } else if (line.startsWith("FILE##")) {
            startReceivingFile(line);
            if (!m_isUploadIdle)
                return;   // 进入二进制接收态，交回 onDataReceived 让 consumeUploadData 处理
        }
        // —— 注册/登录/头像/评论/收藏等 ——
        else if (line.startsWith("REGISTER##")) {
            handleRegisterCommand(line);
        } else if (line.startsWith("LOGIN##")) {
            handleLoginCommand(line);
        } else if (line.startsWith("GET_AVATAR##")) {
            handleGetAvatarCommand(line);
        } else if (line.startsWith("COMMENT_LIST##")) {
            handleCommentListCommand(line);
        } else if (line.startsWith("COMMENT_ADD##")) {
            handleCommentAddCommand(line);
        } else if (line.startsWith("COMMENT_DEL##")) {
            handleCommentDelCommand(line);
        } else if (line.startsWith("DELETE_SESSION##")) {
            handleDeleteSessionCommand(line);
        } else if (line.startsWith("MY_UPLOADS##")) {
            handleMyUploadsCommand(line);
        } else if (line.startsWith("ADD_FAVORITE##")) {
            handleAddFavoriteCommand(line);
        } else if (line.startsWith("GET_FAVORITES##")) {
            handleGetFavoritesCommand(line);
        } else if (line.startsWith("REMOVE_FAVORITE##")) {
            handleRemoveFavoriteCommand(line);
        } else if (line.startsWith("CHECK_FAVORITE##")) {
            handleCheckFavoriteCommand(line);
        }
    }
}

// ====== 上传：接收二进制并写盘（在 IOCP 线程执行，单连接串行，无需加锁）======
void ClientWorker::consumeUploadData()
{
    if (m_isUploadIdle)
        return;

    const qint64 need = m_uploadFileSize - m_uploadRecvSize;
    const qint64 canWrite = qMin<qint64>(need, m_buf.size());
    if (canWrite <= 0)
        return;

    const qint64 len = m_uploadFile.write(m_buf.constData(), canWrite);
    m_uploadRecvSize += len;
    m_buf.remove(0, canWrite);

    if (m_uploadRecvSize < m_uploadFileSize)
        return;

    // —— 单个文件接收完成 ——
    m_uploadFile.close();
    m_isUploadIdle = true;

    m_batchFilePaths.append(ServerConfig::saveDir() + m_batchSubDir + "/" + m_uploadFileName);
    m_batchRecvCount++;

    qDebug() << "[ClientWorker] 批次文件接收:" << m_uploadFileName
             << "(" << m_batchRecvCount << "/" << m_batchFileCount << ")";

    if (m_batchRecvCount >= m_batchFileCount)
        finalizeBatchUpload();

    m_uploadFileName.clear();
    m_uploadFileSize = 0;
    m_uploadRecvSize = 0;
}

// ====== 解析 FILE## 头：FILE##size##fileName(B64) ======
void ClientWorker::startReceivingFile(const QString &line)
{
    const QStringList p = line.split("##");
    m_uploadFileSize = p.value(1).toLongLong();
    m_uploadFileName = fromB64(p.value(2));
    m_uploadRecvSize = 0;

    const QString path = ServerConfig::saveDir() + m_batchSubDir + "/" + m_uploadFileName;
    m_uploadFile.setFileName(path);
    if (!m_uploadFile.open(QIODevice::WriteOnly)) {
        qWarning() << "[ClientWorker] 无法打开上传文件:" << path;
        m_isUploadIdle = true;   // 打开失败：放弃本文件，回到空闲态
    } else {
        m_isUploadIdle = false;  // 进入二进制接收态
    }
}

// ====== 解析 UPLOAD_BATCH## 头 ======
// UPLOAD_BATCH##fileCount##userId##name(B64)##tags(B64)##desc(B64)
void ClientWorker::handleBatchUploadCommand(const QString &line)
{
    const QStringList parts = line.split("##");

    m_batchFileCount = parts.value(1).toLongLong();
    m_batchRecvCount = 0;
    m_batchFilePaths.clear();

    const qint64 uid = parts.value(2).toLongLong();
    m_uploadUserId = (uid > 0 ? uid : m_conn->userId());

    m_batchName = fromB64(parts.value(3));
    m_batchTags = fromB64(parts.value(4)).split(',', Qt::SkipEmptyParts);
    m_batchDesc = fromB64(parts.value(5));

    // 本批次临时子目录：ServerSave/user_<id>/batch_<时间戳>/（入库后再改名为 session_<id>）
    m_batchSubDir = QString("user_%1/batch_%2")
                        .arg(m_uploadUserId)
                        .arg(QDateTime::currentMSecsSinceEpoch());
    QDir().mkpath(ServerConfig::saveDir() + m_batchSubDir + "/");

    qDebug() << "[ClientWorker] 批次上传开始: 文件数=" << m_batchFileCount
             << "userId=" << m_uploadUserId << "子目录=" << m_batchSubDir;
}

// ====== 批次收尾：快照状态 → 投递入库任务（DB/改名/列表都在线程池里做）======
void ClientWorker::finalizeBatchUpload()
{
    if (m_batchFilePaths.isEmpty())
        return;

    // 把批次状态【快照】到局部，随后立即重置成员，以便解析线程继续处理后续命令。
    Connection *c = m_conn;
    const QStringList filePaths = m_batchFilePaths;
    const qint64 userId = m_uploadUserId;
    const QString batchName = m_batchName;
    const QStringList tags  = m_batchTags;
    const QString desc = m_batchDesc;
    const QString subDir = m_batchSubDir;
    const QString saveDir = ServerConfig::saveDir();

    m_batchFilePaths.clear();
    m_batchFileCount = 0;
    m_batchRecvCount = 0;
    m_batchTags.clear();
    m_batchDesc.clear();
    m_batchName.clear();
    m_batchSubDir.clear();
    m_uploadUserId = 0;

    // 入库 + 目录改名 + 资源路径更新 + 回复，全部放到线程池里执行
    m_conn->post([c, filePaths, userId, batchName, tags, desc, subDir, saveDir]() {
        UploadService service;
        auto res = service.recordBatchUploadedFiles(filePaths, userId, batchName, tags, desc);

        // 把临时 batch_<ts> 目录改名为正式的 session_<id>
        if (res.ok && !subDir.isEmpty()) {
            const QString oldDir = saveDir + subDir;
            const QString newSubDir = QString("user_%1/session_%2").arg(userId).arg(res.sessionId);
            const QString newDir = saveDir + newSubDir;
            QDir().mkpath(QFileInfo(newDir).absolutePath());
            QDir().rename(oldDir, newDir);

            ResourceService resourceService;
            resourceService.updateResourceServerPath(subDir, newSubDir, res.sessionId);
            qDebug() << "[ClientWorker] 批次子目录改名:" << oldDir << "→" << newDir;
        }

        QByteArray out;
        if (res.ok)
            out += QString("BATCH_OK##%1\n").arg(res.sessionId).toUtf8();
        else
            out += QString("BATCH_FAIL##%1\n").arg(res.reason).toUtf8();
        c->postSend(out);

        // 入库后补发一次完整资源列表，便于客户端立即刷新
        c->postSend(buildFileListMessage(saveDir));
    });
}

// ====== 同步类命令（重构前在 socket 线程同步查库，现统一改为线程池任务）======

void ClientWorker::handleListSessionsCommand()
{
    Connection *c = m_conn;
    m_conn->post([c]() {
        UploadService uploadService;
        const auto sessions = uploadService.listAllSessions();

        QByteArray out;
        out += QString("SESSIONS_BEGIN##%1\n").arg(sessions.size()).toUtf8();
        for (const auto &s : sessions) {
            out += QString("SESSION_ITEM##%1##%2##%3##%4##%5##%6##%7\n")
                       .arg(s.id).arg(s.userId).arg(toB64(s.title)).arg(toB64(s.tags))
                       .arg(toB64(s.description)).arg(s.fileCount).arg(toB64(s.createdAt))
                       .toUtf8();
        }
        out += QByteArrayLiteral("SESSIONS_END\n");
        c->postSend(out);
    });
}

void ClientWorker::handleSessionFilesCommand(const QString &line)
{
    const qint64 sid = line.section("##", 1, 1).toLongLong();
    if (sid <= 0)
        return;

    Connection *c = m_conn;
    m_conn->post([c, sid]() {
        UploadService uploadService;
        const auto files = uploadService.listSessionFiles(sid);
        const QString uploaderName = uploadService.uploaderNameForSession(sid);

        QByteArray out;
        out += QString("SESSION_FILES_BEGIN##%1##%2\n").arg(sid).arg(files.size()).toUtf8();
        if (!uploaderName.isEmpty())
            out += QString("UPLOADER##%1\n").arg(toB64(uploaderName)).toUtf8();
        for (const auto &f : files) {
            out += QString("FILE_ITEM##%1##%2##%3##%4\n")
                       .arg(f.id).arg(toB64(f.filename)).arg(f.size).arg(toB64(f.uploadedAt))
                       .toUtf8();
        }
        out += QString("SESSION_FILES_END##%1\n").arg(sid).toUtf8();
        c->postSend(out);
    });
}

void ClientWorker::handleDownloadCommand(const QString &fileName)
{
    Connection *c = m_conn;
    const QString saveDir = ServerConfig::saveDir();
    const QString fn = fileName;
    m_conn->post([c, saveDir, fn]() { sendFileToConn(c, saveDir, fn, /*forPreview=*/false); });
}

void ClientWorker::handlePreviewCommand(const QString &fileName)
{
    Connection *c = m_conn;
    const QString saveDir = ServerConfig::saveDir();
    const QString fn = fileName;
    m_conn->post([c, saveDir, fn]() { sendFileToConn(c, saveDir, fn, /*forPreview=*/true); });
}

void ClientWorker::handleRegisterCommand(const QString &line)
{
    const QString username = line.section("##", 1, 1);
    const QString password = line.section("##", 2, 2);

    Connection *c = m_conn;
    m_conn->post([c, username, password]() {
        AuthService service;
        auto res = service.registerUser(username, password);
        if (res.ok)
            c->postSend(QByteArrayLiteral("REGISTER_OK\n"));
        else
            c->postSend(QString("REGISTER_FAIL##%1\n").arg(res.reason).toUtf8());
    });
}

void ClientWorker::handleLoginCommand(const QString &line)
{
    const QString username = line.section("##", 1, 1);
    const QString password = line.section("##", 2, 2);

    Connection *c = m_conn;
    m_conn->post([c, username, password]() {
        AuthService service;
        auto res = service.login(username, password);
        if (res.ok) {
            // 把会话用户ID写到 Connection（原子）；后续命令在解析线程读取
            c->setUserId(res.userId);
            c->postSend(QString("LOGIN_OK##%1##%2##%3\n")
                            .arg(res.userId).arg(res.username).arg(res.avatar).toUtf8());
        } else {
            c->postSend(QString("LOGIN_FAIL##%1\n").arg(res.reason).toUtf8());
        }
    });
}

void ClientWorker::handleGetAvatarCommand(const QString &line)
{
    const qint64 uid = line.section("##", 1, 1).toLongLong();

    Connection *c = m_conn;
    const QString avatarDir = ServerConfig::avatarDir();
    m_conn->post([c, uid, avatarDir]() {
        UserRepository repo;
        auto recOpt = repo.findById(uid);
        if (!recOpt.has_value()) {
            c->postSend(QByteArrayLiteral("AVATAR_FAIL##USER_NOT_FOUND\n"));
            return;
        }

        QString avatarRel = recOpt->avatar;
        if (avatarRel.isEmpty())
            avatarRel = "avatars/default.png";

        const QString avatarFileName = QFileInfo(avatarRel).fileName();
        const QString path = QDir(avatarDir).filePath(avatarFileName);

        QFile f(path);
        if (!f.open(QIODevice::ReadOnly)) {
            c->postSend(QByteArrayLiteral("AVATAR_FAIL##FILE_NOT_FOUND\n"));
            return;
        }

        const qint64 size = f.size();
        const QString outName = QString("avatar_%1_%2").arg(uid).arg(avatarFileName);
        // 头像复用普通文件下载协议：FILE##name##size\n + 二进制
        c->postSend(QString("FILE##%1##%2\n").arg(outName).arg(size).toUtf8());
        streamFileBody(c, f);
        f.close();
    });
}

void ClientWorker::handleCommentListCommand(const QString &line)
{
    const QString resourceName = line.section("##", 1, 1).trimmed();

    Connection *c = m_conn;
    m_conn->post([c, resourceName]() {
        CommentService service;
        auto res = service.listComments(resourceName);

        QByteArray out;
        out += QString("COMMENT_BEGIN##%1\n").arg(resourceName).toUtf8();
        if (res.ok) {
            for (const auto &cm : res.items) {
                out += QString("COMMENT_ITEM##%1##%2##%3##%4##%5\n")
                           .arg(cm.id).arg(cm.userId).arg(toB64(cm.username))
                           .arg(toB64(cm.createdAt)).arg(toB64(cm.content))
                           .toUtf8();
            }
        }
        out += QString("COMMENT_END##%1\n").arg(resourceName).toUtf8();
        c->postSend(out);
    });
}

void ClientWorker::handleCommentAddCommand(const QString &line)
{
    const qint64 userId = line.section("##", 1, 1).toLongLong();
    const QString resourceName = line.section("##", 2, 2).trimmed();
    const QString content = fromB64(line.section("##", 3));

    Connection *c = m_conn;
    m_conn->post([c, userId, resourceName, content]() {
        CommentService service;
        auto res = service.addComment(userId, resourceName, content);
        if (res.ok)
            c->postSend(QString("COMMENT_ADD_OK##%1\n").arg(res.commentId).toUtf8());
        else
            c->postSend(QString("COMMENT_ADD_FAIL##%1\n").arg(res.reason).toUtf8());
    });
}

void ClientWorker::handleCommentDelCommand(const QString &line)
{
    const qint64 userId = line.section("##", 1, 1).toLongLong();
    const qint64 commentId = line.section("##", 2, 2).toLongLong();

    Connection *c = m_conn;
    m_conn->post([c, userId, commentId]() {
        CommentService service;
        auto res = service.deleteComment(userId, commentId);
        if (res.ok)
            c->postSend(QString("COMMENT_DEL_OK##%1\n").arg(commentId).toUtf8());
        else
            c->postSend(QString("COMMENT_DEL_FAIL##%1\n").arg(res.reason).toUtf8());
    });
}

void ClientWorker::handleDeleteSessionCommand(const QString &line)
{
    const qint64 sessionId = line.section("##", 1, 1).trimmed().toLongLong();
    const qint64 uid = m_conn->userId();      // 登录态由 Connection 持有

    Connection *c = m_conn;
    const QString resourceDir = ServerConfig::resourceDir();
    m_conn->post([c, sessionId, uid, resourceDir]() {
        UploadService service;
        const auto res = service.deleteSession(resourceDir, sessionId, uid);
        if (res.ok)
            c->postSend(QString("DELETE_SESSION_OK##%1\n").arg(sessionId).toUtf8());
        else
            c->postSend(QString("DELETE_SESSION_FAIL##%1\n").arg(res.reason).toUtf8());
    });
}

void ClientWorker::handleMyUploadsCommand(const QString &line)
{
    const qint64 userId = line.section("##", 1, 1).toLongLong();

    Connection *c = m_conn;
    m_conn->post([c, userId]() {
        UploadService service;
        const auto sessions = service.listSessionsByUser(userId);

        QByteArray out;
        out += QString("MY_UPLOADS_BEGIN##%1\n").arg(userId).toUtf8();
        for (const auto &s : sessions) {
            out += QString("MY_UPLOADS_ITEM##%1##%2##%3##%4##%5##%6##%7\n")
                       .arg(s.id).arg(s.userId).arg(toB64(s.title)).arg(toB64(s.tags))
                       .arg(toB64(s.description)).arg(s.fileCount).arg(toB64(s.createdAt))
                       .toUtf8();
        }
        out += QString("MY_UPLOADS_END##%1\n").arg(userId).toUtf8();
        c->postSend(out);
    });
}

void ClientWorker::handleAddFavoriteCommand(const QString &line)
{
    const qint64 sessionId = line.section("##", 1, 1).trimmed().toLongLong();
    const qint64 uid = m_conn->userId();

    Connection *c = m_conn;
    m_conn->post([c, sessionId, uid]() {
        FavoritesService service;
        const auto res = service.addFavorite(uid, sessionId);
        if (res.ok)
            c->postSend(QString("ADD_FAVORITE_OK##%1\n").arg(sessionId).toUtf8());
        else
            c->postSend(QString("ADD_FAVORITE_FAIL##%1\n").arg(res.reason).toUtf8());
    });
}

void ClientWorker::handleGetFavoritesCommand(const QString & /*line*/)
{
    const qint64 uid = m_conn->userId();

    Connection *c = m_conn;
    m_conn->post([c, uid]() {
        QByteArray out;
        out += QString("FAVORITES_BEGIN##%1\n").arg(uid).toUtf8();
        if (uid > 0) {
            FavoritesService service;
            const auto res = service.getFavorites(uid);
            if (res.ok) {
                for (const auto &s : res.sessions) {
                    out += QString("FAVORITES_ITEM##%1##%2##%3##%4##%5##%6##%7\n")
                               .arg(s.id).arg(s.userId).arg(toB64(s.title)).arg(toB64(s.tags))
                               .arg(toB64(s.description)).arg(s.fileCount).arg(toB64(s.createdAt))
                               .toUtf8();
                }
            }
        }
        out += QString("FAVORITES_END##%1\n").arg(uid).toUtf8();
        c->postSend(out);
    });
}

void ClientWorker::handleRemoveFavoriteCommand(const QString &line)
{
    const qint64 sessionId = line.section("##", 1, 1).trimmed().toLongLong();
    const qint64 uid = m_conn->userId();

    Connection *c = m_conn;
    m_conn->post([c, sessionId, uid]() {
        FavoritesService service;
        const auto res = service.removeFavorite(uid, sessionId);
        if (res.ok)
            c->postSend(QString("REMOVE_FAVORITE_OK##%1\n").arg(sessionId).toUtf8());
        else
            c->postSend(QString("REMOVE_FAVORITE_FAIL##%1\n").arg(res.reason).toUtf8());
    });
}

void ClientWorker::handleCheckFavoriteCommand(const QString &line)
{
    const qint64 sessionId = line.section("##", 1, 1).trimmed().toLongLong();
    const qint64 uid = m_conn->userId();

    Connection *c = m_conn;
    // 未登录：也走 post() 以保持与其它响应的严格先后顺序（不在解析线程直接发送）
    if (uid <= 0) {
        m_conn->post([c, sessionId]() {
            c->postSend(QString("CHECK_FAVORITE_OK##%1##0\n").arg(sessionId).toUtf8());
        });
        return;
    }

    m_conn->post([c, sessionId, uid]() {
        FavoritesService service;
        const auto res = service.checkFavorite(uid, sessionId);
        const int isFavorited = (res.ok && res.isFavorited) ? 1 : 0;
        c->postSend(QString("CHECK_FAVORITE_OK##%1##%2\n").arg(sessionId).arg(isFavorited).toUtf8());
    });
}

QString ClientWorker::toB64(const QString &s)
{
    return QString::fromUtf8(s.toUtf8().toBase64());
}

QString ClientWorker::fromB64(const QString &b64)
{
    return QString::fromUtf8(QByteArray::fromBase64(b64.toUtf8()));
}
