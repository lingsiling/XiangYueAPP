#include "clientworker.h"
#include "authservice.h"
#include "commentservice.h"
#include "resourceservice.h"
#include "favoritesservice.h"
#include "taskqueue.h"
#include "dbconnectionpool.h"
#include "uploadservice.h"
#include <QSqlQuery>
#include <QTcpSocket>
#include <QDebug>
#include <QThread>
#include <QDir>
#include <QDateTime>

ClientWorker::ClientWorker(qintptr socketDescriptor, QObject *parent)
    : QObject(parent),
    m_sd(socketDescriptor),
    m_socket(nullptr),
    m_isUploadIdle(true),
    m_uploadFileSize(0),
    m_uploadRecvSize(0),
    m_uploadUserId(0),
    m_currentUserId(0),
    m_saveDir("D:/Qt/Projects/XiangYueAPP/ServerSave/"),
    m_dbPath("D:/Qt/Projects/XiangYueAPP/database/xiangyue.db"),
    m_avatarDir("D:/Qt/Projects/XiangYueAPP/ServerAvatars/"),
    m_resourceDir("D:/Qt/Projects/XiangYueAPP/ServerSave/")
{
    //创建任务队列实例
    m_taskQueue = std::make_shared<TaskQueue>(this);

    //连接任务队列信号
    connect(m_taskQueue.get(), &TaskQueue::taskCompleted,
            this, &ClientWorker::onTaskCompleted);
    connect(m_taskQueue.get(), &TaskQueue::taskError,
            this, &ClientWorker::onTaskError);
}

ClientWorker::~ClientWorker()
{
    if (m_socket) {
        if (m_socket->isOpen())
            m_socket->close();
        delete m_socket;
    }

    if (m_uploadFile.isOpen())
        m_uploadFile.close();

    //释放线程本地数据库连接
    DBConnectionPool::instance().releaseConnection();

    qDebug() << "[Worker] 清理完成";
}

void ClientWorker::start()
{
    //socket 必须在本线程创建
    m_socket = new QTcpSocket(this);

    // 用 descriptor 接管 OS 连接
    if (!m_socket->setSocketDescriptor(m_sd)) {
        qDebug() << "[Worker] setSocketDescriptor失败:" << m_socket->errorString();
        emit finished();
        return;
    }

    //初始化线程本地数据库连接
    DBConnectionPool::instance().connection();

    //启动任务队列处理线程
    m_taskQueue->start();

    //连接网络信号
    connect(m_socket, &QTcpSocket::readyRead, this, &ClientWorker::onReadyRead);
    connect(m_socket, &QTcpSocket::disconnected, this, &ClientWorker::onDisconnected);

    qDebug() << "[Worker] 客户端已连接:"
             << m_socket->peerAddress().toString()
             << ":" << m_socket->peerPort();
}

void ClientWorker::onReadyRead()
{
    m_buf += m_socket->readAll();

    // 循环处理：当一批 TCP 包中含多个文件时，处理完一个继续下一个
    while (!m_buf.isEmpty()) {
        if (!m_isUploadIdle) {
            consumeUploadData();
        }

        // 仍在接收文件二进制，等下次 readyRead
        if (!m_isUploadIdle) return;

        // 空闲就继续解析命令（可能是下个 FILE## 头）
        if (m_buf.contains('\n')) {
            tryProcessLines();
        } else {
            break;  // 没有完整行，等更多数据
        }
    }
}

void ClientWorker::onDisconnected()
{
    qDebug() << "[Worker] 客户端已断开连接";

    if (m_uploadFile.isOpen())
        m_uploadFile.close();

    //停止任务队列
    if (m_taskQueue)
        m_taskQueue->stop();

    //通知外部退出线程
    emit finished();
}

void ClientWorker::tryProcessLines()
{
    while (true) {
        int pos = m_buf.indexOf('\n');
        if (pos < 0) break;

        QByteArray raw = m_buf.left(pos);
        m_buf.remove(0, pos + 1);

        const QString line = QString::fromUtf8(raw).trimmed();

        if (line == "LIST_SESSIONS") {
            handleListSessionsCommand();//返回所有批次列表
        }
        else if (line.startsWith("SESSION_FILES##")) { 
            handleSessionFilesCommand(line);//返回某批次的文件列表
        }
        else if (line.startsWith("DOWNLOAD##")) {
            const QString fn = line.section("##", 1, 1).trimmed();
            handleDownloadCommand(fn);
        }
        else if (line.startsWith("PREVIEW##")) {
            // 预览请求：把文件流给客户端（客户端存内存、不落盘）
            const QString fn = line.section("##", 1, 1).trimmed();
            handlePreviewCommand(fn);
        }

        // ------------------------------------------------------------------------
        // 协议识别：上传类命令
        //   UPLOAD_BATCH##N##uid##tags_b64##desc_b64  → 批次模式（N次 FILE 跟随）
        //   UPLOAD##name##size##uid                      → 旧单文件（向后兼容）
        //   FILE##size##name_b64                        → 新单文件/批次内文件
        // ------------------------------------------------------------------------

        else if (line.startsWith("UPLOAD_BATCH##")) {
            handleBatchUploadCommand(line);// 批次上传模式入口：记录文件总数、标签、介绍，后续 FILE## 头跟随
        }
        else if (line.startsWith("FILE##")) {
            startReceivingFile(line);
            if (!m_isUploadIdle) return; // 进入上传模式，退出 while 让 consumeUploadData 处理
        }
        // ============================================================
        //  注册 / 登录 / 头像 / 评论 / 收藏 等异步命令
        // ============================================================
        else if (line.startsWith("REGISTER##")) {
            handleRegisterCommand(line);
        }
        else if (line.startsWith("LOGIN##")) {
            handleLoginCommand(line);
        }
        else if (line.startsWith("GET_AVATAR##")) {
            handleGetAvatarCommand(line);
        }
        else if (line.startsWith("COMMENT_LIST##")) {
            handleCommentListCommand(line);
        }
        else if (line.startsWith("COMMENT_ADD##")) {
            handleCommentAddCommand(line);
        }
        else if (line.startsWith("COMMENT_DEL##")) {
            handleCommentDelCommand(line);
        }
        else if (line.startsWith("DELETE_SESSION##")) {
            handleDeleteSessionCommand(line);
        }
        else if (line.startsWith("MY_UPLOADS##")) {
            handleMyUploadsCommand(line);
        }
        else if (line.startsWith("ADD_FAVORITE##")) {
            handleAddFavoriteCommand(line);
        }
        else if (line.startsWith("GET_FAVORITES##")) {
            handleGetFavoritesCommand(line);
        }
        else if (line.startsWith("REMOVE_FAVORITE##")) {
            handleRemoveFavoriteCommand(line);
        }
        else if (line.startsWith("CHECK_FAVORITE##")) {
            handleCheckFavoriteCommand(line);
        }
    }
}

void ClientWorker::consumeUploadData()
{
    if (m_isUploadIdle) return;

    const qint64 need = m_uploadFileSize - m_uploadRecvSize;
    const qint64 canWrite = qMin<qint64>(need, m_buf.size());
    if (canWrite <= 0) return;

    const qint64 len = m_uploadFile.write(m_buf.constData(), canWrite);
    m_uploadRecvSize += len;
    m_buf.remove(0, canWrite);

    if (m_uploadRecvSize < m_uploadFileSize) return;

    // ====== 文件接收完成 ======
    m_uploadFile.close();
    m_isUploadIdle = true;

    m_batchFilePaths.append(m_saveDir + m_batchSubDir + "/" + m_uploadFileName);
    m_batchRecvCount++;

    qDebug() << "[Worker] 批次文件:" << m_uploadFileName
             << "(" << m_batchRecvCount << "/" << m_batchFileCount << ")";

    if (m_batchRecvCount >= m_batchFileCount) {
        finalizeBatchUpload();
    }

    m_uploadFileName.clear();
    m_uploadFileSize = 0;
    m_uploadRecvSize = 0;
}

void ClientWorker::sendFileList()
{
    // ====== 递归遍历 ServerSave 目录及其子目录，收集所有文件 ======
    // 目录结构：
    //   ServerSave/
    //   ├── old_file.pdf                    （旧格式——单文件直接放在根目录）
    //   ├── user_1/                         （用户1的目录）
    //   │   ├── session_3/                  （第3次上传的文件）
    //   │   │   ├── file1.pdf
    //   │   │   └── file2.ppt
    //   │   └── session_5/                  （第5次上传的文件）
    //   │       └── report.docx
    //   └── user_2/                         （用户2的目录）
    //       └── session_1/
    //           └── notes.txt
    QStringList list;

    QStringList dirs;
    dirs.append(m_saveDir);                     // 从根目录开始

    while (!dirs.isEmpty()) {
        const QString currentDir = dirs.takeFirst();
        QDir dir(currentDir);
        if (!dir.exists()) continue;

        // 当前目录下的所有文件
        const QStringList files = dir.entryList(QDir::Files);
        for (const QString &f : files) {
            // 文件路径相对于 ServerSave 根目录
            const QString relativePath = QDir(m_saveDir).relativeFilePath(
                currentDir + "/" + f);
            list.append(relativePath);
        }

        // 递归子目录
        const QStringList subDirs = dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
        for (const QString &sd : subDirs) {
            dirs.append(currentDir + "/" + sd);
        }
    }

    const QString data = "LIST##" + list.join("##") + "\n";
    m_socket->write(data.toUtf8());
}

void ClientWorker::sendFile(const QString &fileName, bool forPreview)
{
    // 预览与下载只是响应头关键字不同（客户端据此决定"存内存"还是"写盘"），
    // 文件查找、打开、分块发送逻辑完全一致。
    const char *dataTag = forPreview ? "PREVIEW_FILE" : "FILE";
    const char *failTag = forPreview ? "PREVIEW_FAIL" : "FILE_FAIL";

    // ====== 在 ServerSave 及子目录中递归查找文件 ======
    // 目录结构：ServerSave/user_<id>/session_<id>/file.pdf
    QString foundPath;
    QStringList dirs;
    dirs.append(m_saveDir);

    while (!dirs.isEmpty()) {
        const QString currentDir = dirs.takeFirst();
        QDir dir(currentDir);
        if (!dir.exists()) continue;

        // 检查当前目录中的文件
        const QFileInfoList files = dir.entryInfoList(QDir::Files);
        for (const QFileInfo &fi : files) {
            if (fi.fileName() == fileName) {
                foundPath = fi.absoluteFilePath();
                break;
            }
        }
        if (!foundPath.isEmpty()) break;

        // 递归子目录
        const QStringList subDirs = dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
        for (const QString &sd : subDirs) {
            dirs.append(currentDir + "/" + sd);
        }
    }

    if (foundPath.isEmpty()) {
        qDebug() << "[Worker]" << (forPreview ? "预览" : "下载") << "失败：文件不存在" << fileName;
        m_socket->write(QString("%1##FILE_NOT_FOUND\n").arg(failTag).toUtf8());
        return;
    }

    QFile f(foundPath);
    if (!f.open(QIODevice::ReadOnly)) {
        qDebug() << "[Worker] 无法打开文件:" << foundPath;
        m_socket->write(QString("%1##OPEN_FAIL\n").arg(failTag).toUtf8());
        return;
    }

    const qint64 size = f.size();
    const QString nameOnly = QFileInfo(fileName).fileName();
    const QString head = QString("%1##%2##%3\n").arg(dataTag).arg(nameOnly).arg(size);
    m_socket->write(head.toUtf8());

    while (!f.atEnd()) {
        m_socket->write(f.read(4096));
    }

    f.close();
    qDebug() << "[Worker]" << (forPreview ? "预览文件已发送:" : "文件已发送:") << nameOnly << "(" << size << "字节)";
}

void ClientWorker::handleDownloadCommand(const QString &fileName)
{
    qDebug() << "[Worker] 处理DOWNLOAD命令:" << fileName;
    sendFile(fileName);
}

void ClientWorker::handlePreviewCommand(const QString &fileName)
{
    // 预览：把文件流给客户端，由客户端存入内存渲染（不落盘）。复用 sendFile，仅切换响应头。
    qDebug() << "[Worker] 处理PREVIEW命令:" << fileName;
    sendFile(fileName, /*forPreview=*/true);
}

void ClientWorker::handleRegisterCommand(const QString &line)
{
    // 异步处理：注册涉及数据库写操作
    const QString username = line.section("##", 1, 1);
    const QString password = line.section("##", 2, 2);

    qDebug() << "[Worker] 提交注册任务:" << username;

    m_taskQueue->enqueue([this, username, password]() {
        // 在线程池执行：数据库操作
        AuthService service;
        auto res = service.registerUser(username, password);

        // 使用 QMetaObject::invokeMethod 跨线程调用，确保安全
        QMetaObject::invokeMethod(this, [this, res]() {
            if (res.ok) {
                sendResponse("REGISTER_OK\n");
            } else {
                sendResponse(QString("REGISTER_FAIL##%1\n").arg(res.reason));
            }
        }, Qt::QueuedConnection);
    }, TaskQueue::HIGH, QString("REGISTER_%1").arg(username));
}

void ClientWorker::handleLoginCommand(const QString &line)
{
    //异步处理：登录涉及认证
    const QString username = line.section("##", 1, 1);
    const QString password = line.section("##", 2, 2);

    qDebug() << "[Worker] 提交登录任务:" << username;

    m_taskQueue->enqueue([this, username, password]() {
        AuthService service;
        auto res = service.login(username, password);

        //使用 QMetaObject::invokeMethod 跨线程调用
        QMetaObject::invokeMethod(this, [this, res]() {
            if (res.ok) {
                m_currentUserId = res.userId;
                const QString msg = QString("LOGIN_OK##%1##%2##%3\n")
                .arg(res.userId)
                    .arg(res.username)
                    .arg(res.avatar);
                sendResponse(msg);
            } else {
                sendResponse(QString("LOGIN_FAIL##%1\n").arg(res.reason));
            }
        }, Qt::QueuedConnection);
    }, TaskQueue::HIGH, QString("LOGIN_%1").arg(username));
}

void ClientWorker::handleGetAvatarCommand(const QString &line)
{
    // 异步处理：涉及数据库查询和文件读取
    const qint64 uid = line.section("##", 1, 1).toLongLong();

    qDebug() << "[Worker] 提交获取头像任务，用户ID:" << uid;

    m_taskQueue->enqueue([this, uid]() {
        UserRepository repo;
        auto recOpt = repo.findById(uid);
        if (!recOpt.has_value()) {
            QMetaObject::invokeMethod(this, [this]() {
                sendResponse("AVATAR_FAIL##USER_NOT_FOUND\n");
            }, Qt::QueuedConnection);
            return;
        }

        QString avatarRel = recOpt->avatar;
        if (avatarRel.isEmpty())
            avatarRel = "avatars/default.png";

        const QString avatarFileName = QFileInfo(avatarRel).fileName();
        const QString path = QDir(m_avatarDir).filePath(avatarFileName);

        QFile f(path);
        if (!f.open(QIODevice::ReadOnly)) {
            QMetaObject::invokeMethod(this, [this]() {
                sendResponse("AVATAR_FAIL##FILE_NOT_FOUND\n");
            }, Qt::QueuedConnection);
            return;
        }

        const qint64 size = f.size();
        const QString outName = QString("avatar_%1_%2").arg(uid).arg(avatarFileName);
        const QString head = QString("FILE##%1##%2\n").arg(outName).arg(size);

        // 头像数据需要在socket线程发送
        QByteArray data;
        while (!f.atEnd()) {
            data.append(f.read(4096));
        }
        f.close();

        // 跨线程发送
        QMetaObject::invokeMethod(this, [this, head, data]() {
            if (m_socket && m_socket->isOpen()) {
                m_socket->write(head.toUtf8());
                m_socket->write(data);
            }
        }, Qt::QueuedConnection);
    }, TaskQueue::HIGH, QString("AVATAR_%1").arg(uid));
}

void ClientWorker::handleCommentListCommand(const QString &line)
{
    const QString resourceName = line.section("##", 1, 1).trimmed();

    qDebug() << "[Worker] 提交获取评论任务:" << resourceName;

    m_taskQueue->enqueue([this, resourceName]() {
        CommentService service;
        auto res = service.listComments(resourceName);

        QStringList responses;

        //先发送一个批次开始标识，客户端按 COMMENT_BEGIN/ITEM/END 批量接收
        responses << QString("COMMENT_BEGIN##%1\n").arg(resourceName);

        if (res.ok) {
            for (const auto &c : res.items) {
                const QString msg = QString("COMMENT_ITEM##%1##%2##%3##%4##%5\n")
                        .arg(c.id)
                        .arg(c.userId)
                        .arg(toB64(c.username))
                        .arg(toB64(c.createdAt))
                        .arg(toB64(c.content));
                responses << msg;
            }
        }

        responses << QString("COMMENT_END##%1\n").arg(resourceName);

        QMetaObject::invokeMethod(this, [this, responses]() {
            for (const QString &msg : responses) {
                sendResponse(msg);
            }
        }, Qt::QueuedConnection);
    }, TaskQueue::NORMAL, QString("COMMENT_LIST_%1").arg(resourceName));
}


void ClientWorker::handleCommentAddCommand(const QString &line)
{
    // 异步处理：数据库写操作
    const qint64 userId = line.section("##", 1, 1).toLongLong();
    const QString resourceName = line.section("##", 2, 2).trimmed();
    const QString contentB64 = line.section("##", 3);
    const QString content = fromB64(contentB64);

    qDebug() << "[Worker] 提交添加评论任务:" << resourceName;

    m_taskQueue->enqueue([this, userId, resourceName, content]() {
        CommentService service;
        auto res = service.addComment(userId, resourceName, content);

        QString response;
        if (res.ok) {
            response = QString("COMMENT_ADD_OK##%1\n").arg(res.commentId);
        } else {
            response = QString("COMMENT_ADD_FAIL##%1\n").arg(res.reason);
        }

        // 跨线程发送
        QMetaObject::invokeMethod(this, [this, response]() {
            sendResponse(response);
        }, Qt::QueuedConnection);
    }, TaskQueue::NORMAL, QString("COMMENT_ADD_%1").arg(resourceName));
}

void ClientWorker::handleCommentDelCommand(const QString &line)
{
    // 异步处理：数据库删除操作
    const qint64 userId = line.section("##", 1, 1).toLongLong();
    const qint64 commentId = line.section("##", 2, 2).toLongLong();

    qDebug() << "[Worker] 提交删除评论任务，评论ID:" << commentId;

    m_taskQueue->enqueue([this, userId, commentId]() {
        CommentService service;
        auto res = service.deleteComment(userId, commentId);

        QString response;
        if (res.ok) {
            response = QString("COMMENT_DEL_OK##%1\n").arg(commentId);
        } else {
            response = QString("COMMENT_DEL_FAIL##%1\n").arg(res.reason);
        }

        // 跨线程发送
        QMetaObject::invokeMethod(this, [this, response]() {
            sendResponse(response);
        }, Qt::QueuedConnection);
    }, TaskQueue::NORMAL, QString("COMMENT_DEL_%1").arg(commentId));
}

void ClientWorker::handleDeleteSessionCommand(const QString &line)
{
    //删除整个上传批次：行协议 DELETE_SESSION##sessionId
    const qint64 sessionId = line.section("##", 1, 1).trimmed().toLongLong();

    qDebug() << "[Worker] 处理批次删除命令: sessionId=" << sessionId
             << " userId=" << m_currentUserId;

    //业务层在一个事务里清理 文件/resources/uploads/favorites/upload_sessions，
    //Worker 只负责协议分发与归属用户传递（m_currentUserId 登录时已置位）
    UploadService service;
    const auto res = service.deleteSession(m_resourceDir, sessionId, m_currentUserId);

    if (res.ok) {
        sendResponse(QString("DELETE_SESSION_OK##%1\n").arg(sessionId));
    } else {
        sendResponse(QString("DELETE_SESSION_FAIL##%1\n").arg(res.reason));
    }
}

void ClientWorker::handleMyUploadsCommand(const QString &line)
{
    //行协议：MY_UPLOADS##userId —— 返回该用户的"上传批次"列表（不再是单个文件）
    const qint64 userId = line.section("##", 1, 1).toLongLong();

    qDebug() << "[Worker] 处理我的上传查询（按批次），userId=" << userId;

    UploadService service;
    const auto sessions = service.listSessionsByUser(userId);

    //固定 begin/end 包裹，客户端可以稳态刷新 UI
    //ITEM 字段顺序与 SESSION_ITEM 完全一致，客户端共用 SessionDto 解析
    sendResponse(QString("MY_UPLOADS_BEGIN##%1\n").arg(userId));

    for (const auto &s : sessions) {
        const QString msg = QString("MY_UPLOADS_ITEM##%1##%2##%3##%4##%5##%6##%7\n")
                .arg(s.id)
                .arg(s.userId)
                .arg(toB64(s.title))          // 批次名
                .arg(toB64(s.tags))
                .arg(toB64(s.description))    // 资源介绍
                .arg(s.fileCount)
                .arg(toB64(s.createdAt));
        sendResponse(msg);
    }

    sendResponse(QString("MY_UPLOADS_END##%1\n").arg(userId));
}

// 处理添加收藏命令：解析批次ID → 调用 Service → 返回结果
void ClientWorker::handleAddFavoriteCommand(const QString &line)
{
    // 行协议：ADD_FAVORITE##sessionId
    const qint64 sessionId = line.section("##", 1, 1).trimmed().toLongLong();

    qDebug() << "[Worker] 处理收藏请求，userId=" << m_currentUserId << "sessionId=" << sessionId;

    FavoritesService service;
    const auto res = service.addFavorite(m_currentUserId, sessionId);

    if (res.ok) {
        sendResponse(QString("ADD_FAVORITE_OK##%1\n").arg(sessionId));
    } else {
        sendResponse(QString("ADD_FAVORITE_FAIL##%1\n").arg(res.reason));
    }
}

// 处理获取收藏列表命令：返回已收藏的“批次”列表（BEGIN/ITEM/END，字段与 MY_UPLOADS 一致）
void ClientWorker::handleGetFavoritesCommand(const QString & /*line*/)
{
    // 行协议：GET_FAVORITES##
    qDebug() << "[Worker] 处理获取收藏列表请求，userId=" << m_currentUserId;

    // 固定 BEGIN/END 包裹，客户端可稳态刷新 UI；未登录时返回空列表
    sendResponse(QString("FAVORITES_BEGIN##%1\n").arg(m_currentUserId));

    if (m_currentUserId > 0) {
        FavoritesService service;
        const auto res = service.getFavorites(m_currentUserId);
        if (res.ok) {
            // ITEM 字段顺序与 SESSION_ITEM / MY_UPLOADS_ITEM 完全一致，客户端共用 SessionDto 解析
            for (const auto &s : res.sessions) {
                const QString msg = QString("FAVORITES_ITEM##%1##%2##%3##%4##%5##%6##%7\n")
                        .arg(s.id)
                        .arg(s.userId)
                        .arg(toB64(s.title))
                        .arg(toB64(s.tags))
                        .arg(toB64(s.description))
                        .arg(s.fileCount)
                        .arg(toB64(s.createdAt));
                sendResponse(msg);
            }
        }
    }

    sendResponse(QString("FAVORITES_END##%1\n").arg(m_currentUserId));
}

// 处理取消收藏命令：软删除，走 Service → Repository 更新 is_active = 0
void ClientWorker::handleRemoveFavoriteCommand(const QString &line)
{
    // 行协议：REMOVE_FAVORITE##sessionId
    const qint64 sessionId = line.section("##", 1, 1).trimmed().toLongLong();

    qDebug() << "[Worker] 处理取消收藏请求，userId=" << m_currentUserId << "sessionId=" << sessionId;

    FavoritesService service;
    const auto res = service.removeFavorite(m_currentUserId, sessionId);

    if (res.ok) {
        sendResponse(QString("REMOVE_FAVORITE_OK##%1\n").arg(sessionId));
    } else {
        sendResponse(QString("REMOVE_FAVORITE_FAIL##%1\n").arg(res.reason));
    }
}

// 处理检查收藏状态命令：返回 0/1，用于资源详情页初始化收藏按钮文字
void ClientWorker::handleCheckFavoriteCommand(const QString &line)
{
    // 行协议：CHECK_FAVORITE##sessionId
    const qint64 sessionId = line.section("##", 1, 1).trimmed().toLongLong();

    qDebug() << "[Worker] 检查收藏状态，userId=" << m_currentUserId << "sessionId=" << sessionId;

    if (m_currentUserId <= 0) {
        // 未登录，安全返回未收藏
        sendResponse(QString("CHECK_FAVORITE_OK##%1##0\n").arg(sessionId));
        return;
    }

    FavoritesService service;
    const auto res = service.checkFavorite(m_currentUserId, sessionId);

    // 查询出错时也默认返回未收藏，保证客户端按钮状态安全
    const int isFavorited = (res.ok && res.isFavorited) ? 1 : 0;
    sendResponse(QString("CHECK_FAVORITE_OK##%1##%2\n").arg(sessionId).arg(isFavorited));
}

void ClientWorker::onTaskCompleted(const QString &taskType)
{
    qDebug() << "[Worker] 任务完成:" << taskType;
}

// ====== 批次上传：解析 UPLOAD_BATCH 头 ======
// 协议：UPLOAD_BATCH##fileCount##userId##tags(B64)##desc(B64)
//       后面紧跟 FILE##fileSize##fileName(B64)\n + 二进制 的逐个文件
void ClientWorker::handleBatchUploadCommand(const QString &line)
{
    const QStringList parts = line.split("##");

    m_batchFileCount  = parts.value(1).toLongLong();          // 文件总数
    m_batchRecvCount  = 0;                                     // 已接收文件计数
    m_batchFilePaths.clear();

    const qint64 uid = parts.value(2).toLongLong();
    m_uploadUserId = (uid > 0 ? uid : m_currentUserId);

    m_batchName = fromB64(parts.value(3));                     // 批次名
    m_batchTags = fromB64(parts.value(4)).split(',',
        Qt::SkipEmptyParts);                                   // 逗号分隔的标签
    m_batchDesc = fromB64(parts.value(5));                      // 资源介绍

    // ====== 创建本批次的存储子目录 ======
    // 格式：ServerSave/user_<userId>/batch_<timestamp>/
    // 入库后重命名为 ServerSave/user_<userId>/session_<id>/
    m_batchSubDir = QString("user_%1/batch_%2")
        .arg(m_uploadUserId)
        .arg(QDateTime::currentMSecsSinceEpoch());
    QDir().mkpath(m_saveDir + m_batchSubDir + "/");

    qDebug() << "[Worker] 批次上传开始:"
             << "文件数=" << m_batchFileCount
             << "userId=" << m_uploadUserId
             << "子目录=" << m_batchSubDir
             << "标签=" << m_batchTags
             << "介绍=" << m_batchDesc;
}

// ====== 批次上传收尾：全部文件收完后入库并回复 BATCH_OK ======
void ClientWorker::finalizeBatchUpload()
{
    if (m_batchFilePaths.isEmpty()) return;

    UploadService service;
    auto res = service.recordBatchUploadedFiles(
        m_batchFilePaths, m_uploadUserId, m_batchName, m_batchTags, m_batchDesc);

    // ====== 将临时批次子目录重命名为正式的 user_<userId>/session_<id> ======
    if (res.ok && !m_batchSubDir.isEmpty()) {
        const QString oldDir = m_saveDir + m_batchSubDir;
        // 新目录：ServerSave/user_<userId>/session_<sessionId>/
        const QString newSubDir = QString("user_%1/session_%2")
            .arg(m_uploadUserId).arg(res.sessionId);
        const QString newDir = m_saveDir + newSubDir;
        QDir().mkpath(QFileInfo(newDir).absolutePath());  // 确保父目录存在
        QDir().rename(oldDir, newDir);

        // 通过 Service 更新资源路径（不直接操作 SQL）
        ResourceService resourceService;
        resourceService.updateResourceServerPath(m_batchSubDir, newSubDir, res.sessionId);

        qDebug() << "[Worker] 批次子目录重命名:" << oldDir << "→" << newDir;
    }

    if (res.ok) {
        const QString ok = QString("BATCH_OK##%1\n").arg(res.sessionId);
        m_socket->write(ok.toUtf8());
    } else {
        const QString fail = QString("BATCH_FAIL##%1\n").arg(res.reason);
        m_socket->write(fail.toUtf8());
    }

    sendFileList();

    m_batchFilePaths.clear();
    m_batchFileCount = 0;
    m_batchRecvCount = 0;
    m_batchTags.clear();
    m_batchDesc.clear();
    m_batchSubDir.clear();
    m_uploadUserId = 0;
}

void ClientWorker::onTaskError(const QString &taskType, const QString &error)
{
    qWarning() << "[Worker] 任务出错:" << taskType << "错误:" << error;
}

// ====== 返回所有上传批次列表（主界面展示用） ======
// 协议格式：SESSIONS_BEGIN##总数
//           SESSION_ITEM##id##userId##tags(B64)##desc(B64)##fileCount##createdAt
//           SESSIONS_END
void ClientWorker::handleListSessionsCommand()
{
    UploadService uploadService;
    const auto sessions = uploadService.listAllSessions();

    // 发送批次总数
    const QString begin = QString("SESSIONS_BEGIN##%1\n").arg(sessions.size());
    m_socket->write(begin.toUtf8());

    // 逐条发送每个批次的元信息
    for (const auto &s : sessions) {
        const QString item = QString("SESSION_ITEM##%1##%2##%3##%4##%5##%6##%7\n")
            .arg(s.id)
            .arg(s.userId)
            .arg(toB64(s.title))          // 批次名
            .arg(toB64(s.tags))
            .arg(toB64(s.description))    // 资源介绍
            .arg(s.fileCount)
            .arg(toB64(s.createdAt));
        m_socket->write(item.toUtf8());
    }

    const QString end = QString("SESSIONS_END\n");
    m_socket->write(end.toUtf8());
}

// ====== 返回某个批次的文件列表（点击批次后展示用） ======
// 协议格式：SESSION_FILES_BEGIN##sessionId##文件数
//           FILE_ITEM##id##filename(B64)##size##uploadedAt(B64)
//           SESSION_FILES_END##sessionId
void ClientWorker::handleSessionFilesCommand(const QString &line)
{
    const qint64 sid = line.section("##", 1, 1).toLongLong();
    if (sid <= 0) return;

    UploadService uploadService;
    const auto files = uploadService.listSessionFiles(sid);

    // 通过 service 查询上传者名称
    const QString uploaderName = uploadService.uploaderNameForSession(sid);

    // 发送批次总数 + 上传者名
    const QString begin = QString("SESSION_FILES_BEGIN##%1##%2\n")
        .arg(sid).arg(files.size());
    m_socket->write(begin.toUtf8());

    // 发送上传者名称
    if (!uploaderName.isEmpty()) {
        m_socket->write(QString("UPLOADER##%1\n").arg(toB64(uploaderName)).toUtf8());
    }

    for (const auto &f : files) {
        const QString item = QString("FILE_ITEM##%1##%2##%3##%4\n")
            .arg(f.id)
            .arg(toB64(f.filename))
            .arg(f.size)
            .arg(toB64(f.uploadedAt));
        m_socket->write(item.toUtf8());
    }

    const QString end = QString("SESSION_FILES_END##%1\n").arg(sid);
    m_socket->write(end.toUtf8());
}

QString ClientWorker::toB64(const QString &s)
{
    return QString::fromUtf8(s.toUtf8().toBase64());
}

QString ClientWorker::fromB64(const QString &b64)
{
    return QString::fromUtf8(QByteArray::fromBase64(b64.toUtf8()));
}

// ====== 解析文件上传头：FILE##size##fileName(B64) ======
// 所有上传都走这条路径
void ClientWorker::startReceivingFile(const QString &line)
{
    QStringList p = line.split("##");
    m_uploadFileSize = p.value(1).toLongLong();       // 文件大小
    m_uploadFileName = fromB64(p.value(2));           // Base64 解码文件名
    m_uploadRecvSize = 0;

    // 存入批次子目录
    const QString path = m_saveDir + m_batchSubDir + "/" + m_uploadFileName;
    m_uploadFile.setFileName(path);
    if (!m_uploadFile.open(QIODevice::WriteOnly)) {
        qDebug() << "[Worker] 无法打开文件:" << path;
        m_isUploadIdle = true;
    } else {
        qDebug() << "[Worker] 接收文件:" << m_uploadFileName
                 << "大小=" << m_uploadFileSize
                 << "路径=" << path;
        m_isUploadIdle = false;
        return;   // 退出 tryProcessLines，后续由 consumeUploadData 消费二进制
    }
}

void ClientWorker::sendResponse(const QString &response)
{
    //这个函数现在保证在socket的事件循环线程中调用
    if (m_socket && m_socket->isOpen()) {
        m_socket->write(response.toUtf8());
        qDebug() << "[Worker] 发送回复:" << response.trimmed();
    }
}