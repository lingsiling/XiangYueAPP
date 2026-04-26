#include "clientworker.h"
#include "authservice.h"
#include "commentservice.h"
#include "taskqueue.h"
#include "dbconnectionpool.h"
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
    m_saveDir("D:/Qt/Projects/XiangYueAPP/ServerSave/"),
    m_dbPath("D:/Qt/Projects/XiangYueAPP/database/xiangyue.db"),
    m_avatarDir("D:/Qt/Projects/XiangYueAPP/ServerAvatars/")
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
            //解析UPLOAD头：UPLOAD##fileName##fileSize
            QStringList p = line.split("##");
            m_uploadFileName = p.value(1).trimmed();
            m_uploadFileSize = p.value(2).toLongLong();
            m_uploadRecvSize = 0;

            QDir().mkpath(m_saveDir);
            const QString path = m_saveDir + m_uploadFileName;

            m_uploadFile.setFileName(path);
            if (!m_uploadFile.open(QIODevice::WriteOnly)) {
                qDebug() << "[Worker] 无法打开上传文件:" << path;
                m_isUploadIdle = true;
            } else {
                qDebug() << "[Worker] 上传开始:" << m_uploadFileName
                         << "大小=" << m_uploadFileSize;
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
            const qint64 uid = line.section("##", 1, 1).toLongLong();
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

        //发送确认
        const QString ok = QString("UPLOAD_OK##%1\n").arg(m_uploadFileName);
        m_socket->write(ok.toUtf8());

        //刷新文件列表
        sendFileList();

        //清理状态
        m_uploadFileName.clear();
        m_uploadFileSize = 0;
        m_uploadRecvSize = 0;
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
    }, TaskQueue::NORMAL, QString("AVATAR_%1").arg(uid));
}


void ClientWorker::handleCommentListCommand(const QString &line)
{
    // 异步处理：数据库查询
    const QString resourceName = line.section("##", 1, 1).trimmed();

    qDebug() << "[Worker] 提交获取评论列表任务:" << resourceName;

    m_taskQueue->enqueue([this, resourceName]() {
        CommentService service;
        auto res = service.listComments(resourceName);

        // 准备所有回复消息
        QStringList responses;
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

        // 一次性跨线程发送所有消息
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