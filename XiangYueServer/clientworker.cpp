#include "clientworker.h"
#include "authservice.h"
#include "commentservice.h"
#include "resourceservice.h"
#include "favoritesservice.h"
#include "taskqueue.h"
#include "dbconnectionpool.h"
#include "uploadservice.h"
#include <QTcpSocket>
#include <QDebug>
#include <QThread>
#include <QDir>

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

    //上传中：优先当二进制消费
    if (!m_isUploadIdle) {
        consumeUploadData();
        if (!m_isUploadIdle) return;
    }

    //空闲状态：按行解析命令
    tryProcessLines();

    //可能刚解析到上传头，缓冲区里已经粘了文件内容
    if (!m_isUploadIdle) {
        consumeUploadData();
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

        if (line == "LIST") {
            handleListCommand();
        }
        else if (line.startsWith("DOWNLOAD##")) {
            const QString fn = line.section("##", 1, 1).trimmed();
            handleDownloadCommand(fn);
        }
        else if (line.startsWith("UPLOAD##")) {
            //解析UPLOAD头：UPLOAD##fileName##fileSize##userId
            QStringList p = line.split("##");
            m_uploadFileName = p.value(1).trimmed();
            m_uploadFileSize = p.value(2).toLongLong();
            //兼容旧客户端：第4段缺失时回退到连接登录态
            const qint64 headerUserId = p.value(3).toLongLong();
            m_uploadUserId = (headerUserId > 0 ? headerUserId : m_currentUserId);
            m_uploadRecvSize = 0;

            QDir().mkpath(m_saveDir);
            const QString path = m_saveDir + m_uploadFileName;

            m_uploadFile.setFileName(path);
            if (!m_uploadFile.open(QIODevice::WriteOnly)) {
                qDebug() << "[Worker] 无法打开上传文件:" << path;
                m_isUploadIdle = true;
            } else {
                qDebug() << "[Worker] 上传开始:" << m_uploadFileName
                         << "大小=" << m_uploadFileSize
                         << "userId=" << m_uploadUserId;
                m_isUploadIdle = false;
                return;
            }
        }
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
        else if (line.startsWith("DELETE_RESOURCE##") || line.startsWith("DELETE_FILE##")) {
            handleDeleteResourceCommand(line);
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

    //文件接收完成
    if (m_uploadRecvSize >= m_uploadFileSize) {
        m_uploadFile.close();
        m_isUploadIdle = true;

        //上传完成后，统一走 UploadService：同时写 resources 和 uploads
        {
            UploadService service;
            const QString path = m_saveDir + m_uploadFileName;
            auto recordRes = service.recordUploadedFile(path, m_uploadUserId);
            if (!recordRes.ok) {
                qWarning() << "[Worker] 上传入库失败:" << recordRes.reason
                           << "file=" << m_uploadFileName
                           << "userId=" << m_uploadUserId;
            }
        }

        //发送确认
        const QString ok = QString("UPLOAD_OK##%1\n").arg(m_uploadFileName);
        m_socket->write(ok.toUtf8());

        //刷新文件列表
        sendFileList();

        //清理状态
        m_uploadFileName.clear();
        m_uploadFileSize = 0;
        m_uploadRecvSize = 0;
        m_uploadUserId = 0;
    }
}

void ClientWorker::sendFileList()
{
    //快速操作，不需要异步处理
    QDir dir(m_saveDir);
    if (!dir.exists()) return;

    QStringList list = dir.entryList(QDir::Files);
    const QString data = "LIST##" + list.join("##") + "\n";
    m_socket->write(data.toUtf8());
}

void ClientWorker::sendFile(const QString &fileName)
{
    //快速操作，不需要异步处理
    const QString path = m_saveDir + fileName;
    QFile f(path);

    if (!f.open(QIODevice::ReadOnly)) {
        qDebug() << "[Worker] 无法打开下载文件:" << path;
        return;
    }

    const qint64 size = f.size();
    const QString head = QString("FILE##%1##%2\n").arg(fileName).arg(size);
    m_socket->write(head.toUtf8());

    while (!f.atEnd()) {
        m_socket->write(f.read(4096));
    }

    f.close();
}

void ClientWorker::handleListCommand()
{
    qDebug() << "[Worker] 处理LIST命令";
    sendFileList();
}

void ClientWorker::handleDownloadCommand(const QString &fileName)
{
    qDebug() << "[Worker] 处理DOWNLOAD命令:" << fileName;
    sendFile(fileName);
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

void ClientWorker::handleDeleteResourceCommand(const QString &line)
{
    //资源删除命令：文件系统删文件，资源表删记录，保持两边一致
    const QString fileName = line.section("##", 1, 1).trimmed();

    qDebug() << "[Worker] 处理资源删除命令:" << fileName;

    ResourceService service;
    //由业务层统一处理删文件和删记录，Worker 只负责协议分发
    auto res = service.deleteFileAndUploadRecord(m_resourceDir, fileName);

    if (res.ok) {
        sendResponse(QString("DELETE_RESOURCE_OK##%1\n").arg(fileName));
    } else {
        sendResponse(QString("DELETE_RESOURCE_FAIL##%1\n").arg(res.reason));
    }
}

void ClientWorker::handleMyUploadsCommand(const QString &line)
{
    //行协议：MY_UPLOADS##userId
    const qint64 userId = line.section("##", 1, 1).toLongLong();

    qDebug() << "[Worker] 处理我的上传查询，userId=" << userId;

    ResourceService service;
    const auto res = service.listByUploader(userId);

    //固定 begin/end 包裹，客户端可以稳态刷新 UI
    sendResponse(QString("MY_UPLOADS_BEGIN##%1\n").arg(userId));

    if (res.ok) {
        for (const auto &item : res.items) {
            const QString msg = QString("MY_UPLOADS_ITEM##%1##%2##%3\n")
                    .arg(toB64(item.filename))
                    .arg(item.size)
                    .arg(toB64(item.uploadedAt));
            sendResponse(msg);
        }
    }

    sendResponse(QString("MY_UPLOADS_END##%1\n").arg(userId));
}

void ClientWorker::handleAddFavoriteCommand(const QString &line)
{
    // 行协议：ADD_FAVORITE##resourceName_b64
    const QString resourceName = fromB64(line.section("##", 1, 1));

    qDebug() << "[Worker] 处理收藏请求，userId=" << m_currentUserId << "resourceName=" << resourceName;

    FavoritesService service;
    const auto res = service.addFavorite(m_currentUserId, resourceName);

    if (res.ok) {
        sendResponse(QString("ADD_FAVORITE_OK##%1\n").arg(toB64(resourceName)));
    } else {
        sendResponse(QString("ADD_FAVORITE_FAIL##%1\n").arg(res.reason));
    }
}

void ClientWorker::handleGetFavoritesCommand(const QString &line)
{
    // 行协议：GET_FAVORITES##
    qDebug() << "[Worker] 处理获取收藏列表请求，userId=" << m_currentUserId;

    if (m_currentUserId <= 0) {
        sendResponse(QString("GET_FAVORITES_FAIL##UNAUTHORIZED\n"));
        return;
    }

    FavoritesService service;
    const auto res = service.getFavorites(m_currentUserId);

    if (res.ok) {
        // 将所有资源名用 || 分隔返回
        const QString favorites = res.favorites.join("||");
        sendResponse(QString("GET_FAVORITES_OK##%1\n").arg(toB64(favorites)));
    } else {
        sendResponse(QString("GET_FAVORITES_FAIL##%1\n").arg(res.reason));
    }
}

void ClientWorker::onTaskCompleted(const QString &taskType)
{
    qDebug() << "[Worker] 任务完成:" << taskType;
}

void ClientWorker::onTaskError(const QString &taskType, const QString &error)
{
    qWarning() << "[Worker] 任务出错:" << taskType << "错误:" << error;
    sendResponse("ERROR##SERVER_ERROR\n");
}

QString ClientWorker::toB64(const QString &s)
{
    return QString::fromUtf8(s.toUtf8().toBase64());
}

QString ClientWorker::fromB64(const QString &b64)
{
    return QString::fromUtf8(QByteArray::fromBase64(b64.toUtf8()));
}

void ClientWorker::sendResponse(const QString &response)
{
    //这个函数现在保证在socket的事件循环线程中调用
    if (m_socket && m_socket->isOpen()) {
        m_socket->write(response.toUtf8());
        qDebug() << "[Worker] 发送回复:" << response.trimmed();
    }
}