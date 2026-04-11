// clientworker.cpp
#include "clientworker.h"
#include "authservice.h"
#include "dbmanager.h"
#include "commentservice.h"
#include <QByteArray>
#include <QDir>
#include <QDebug>
#include <QThread>

ClientWorker::ClientWorker(qintptr socketDescriptor, QObject *parent)
    : QObject(parent), m_sd(socketDescriptor)
{
}

void ClientWorker::start()
{
    //socket 必须在本线程创建
    m_socket = new QTcpSocket(this);

    //用 descriptor 接管 OS 连接（此时 socket 引擎属于本线程）
    if (!m_socket->setSocketDescriptor(m_sd)) {
        qDebug() << "[Worker] setSocketDescriptor failed:" << m_socket->errorString();
        emit finished();
        return;
    }

    //线程内初始化数据库连接（SQLite 每线程一连接）
    DBManager::instance().openForCurrentThread(m_dbPath);

    connect(m_socket, &QTcpSocket::readyRead, this, &ClientWorker::onReadyRead);
    connect(m_socket, &QTcpSocket::disconnected, this, &ClientWorker::onDisconnected);

    qDebug() << "[Worker] client connected:"
             << m_socket->peerAddress().toString()
             << m_socket->peerPort();
}

void ClientWorker::onReadyRead()
{
    m_buf += m_socket->readAll();

    //上传中：优先当二进制消费，禁止拆行（避免二进制里碰巧出现 '\n'）
    if (!m_isUploadIdle) {
        consumeUploadData();
        if (!m_isUploadIdle) return;
    }

    //空闲状态：按行解析命令
    tryProcessLines();

    //可能刚解析到 UPLOAD 头，缓冲区里已经粘了文件内容
    if (!m_isUploadIdle) {
        consumeUploadData();
    }
}

void ClientWorker::onDisconnected()
{
    qDebug() << "[Worker] client disconnected";

    if (m_uploadFile.isOpen())
        m_uploadFile.close();

    //通知外部退出线程（ThreadedTcpServer 里 connect 了 finished->quit）
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

        if (line == "LIST")
        {
            sendFileList();
        }
        else if (line.startsWith("DOWNLOAD##"))
        {
            const QString fn = line.section("##", 1, 1).trimmed();
            sendFile(fn);
        }
        else if (line.startsWith("UPLOAD##"))
        {
            // UPLOAD##fileName##fileSize
            QStringList p = line.split("##");
            m_uploadFileName = p.value(1).trimmed();
            m_uploadFileSize = p.value(2).toLongLong();
            m_uploadRecvSize = 0;

            QDir().mkpath(m_saveDir);
            const QString path = m_saveDir + m_uploadFileName;

            m_uploadFile.setFileName(path);
            if (!m_uploadFile.open(QIODevice::WriteOnly))
            {
                qDebug() << "[Worker] open upload file failed:" << path;
                m_isUploadIdle = true;
            }
            else
            {
                qDebug() << "[Worker] upload start:" << m_uploadFileName
                         << "size=" << m_uploadFileSize
                         << "path=" << path;

                // 进入上传二进制状态
                m_isUploadIdle = false;
                return;
            }
        }
        else if (line.startsWith("REGISTER##"))
        {
            handleRegister(line);
        }
        else if (line.startsWith("LOGIN##"))
        {
            handleLogin(line);
        }
        else if (line.startsWith("GET_AVATAR##"))
        {
            const qint64 uid = line.section("##", 1, 1).toLongLong();
            handleGetAvatar(uid);
        }
        else if (line.startsWith("COMMENT_LIST##"))
        {
            handleCommentList(line);
        }
        else if (line.startsWith("COMMENT_ADD##"))
        {
            handleCommentAdd(line);
        }
        else {
            //预留：收藏/我的上传
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

    //收满了“上传完成”
    if (m_uploadRecvSize >= m_uploadFileSize)
    {
        m_uploadFile.close();
        m_isUploadIdle = true;                 //恢复空闲，才能接收下一次 UPLOAD##

        //只发一次确认
        const QString ok = QString("UPLOAD_OK##%1\n").arg(m_uploadFileName);
        m_socket->write(ok.toUtf8());

        //只刷新一次列表
        sendFileList();
    }
}

void ClientWorker::sendFileList()
{
    QDir dir(m_saveDir);
    if (!dir.exists()) return;

    QStringList list = dir.entryList(QDir::Files);
    const QString data = "LIST##" + list.join("##") + "\n";
    m_socket->write(data.toUtf8());
}

void ClientWorker::sendFile(const QString &fileName)
{
    const QString path = m_saveDir + fileName;
    QFile f(path);

    if (!f.open(QIODevice::ReadOnly)) {
        qDebug() << "[Worker] open read failed:" << path;
        return;
    }

    const qint64 size = f.size();
    const QString head = QString("FILE##%1##%2\n").arg(fileName).arg(size);
    m_socket->write(head.toUtf8());

    while (!f.atEnd()) {
        m_socket->write(f.read(4096));
    }
}

void ClientWorker::handleRegister(const QString &line)
{
    const QString username = line.section("##", 1, 1);
    const QString password = line.section("##", 2, 2);

    AuthService service;
    auto res = service.registerUser(username, password);

    if (res.ok) m_socket->write("REGISTER_OK\n");
    else m_socket->write(QString("REGISTER_FAIL##%1\n").arg(res.reason).toUtf8());
}

void ClientWorker::handleLogin(const QString &line)
{
    const QString username = line.section("##", 1, 1);
    const QString password = line.section("##", 2, 2);

    AuthService service;
    auto res = service.login(username, password);

    if (res.ok) {
        const QString msg = QString("LOGIN_OK##%1##%2##%3\n")
            .arg(res.userId)
            .arg(res.username)
            .arg(res.avatar);
        m_socket->write(msg.toUtf8());
    } else {
        m_socket->write(QString("LOGIN_FAIL##%1\n").arg(res.reason).toUtf8());
    }
}

void ClientWorker::handleGetAvatar(qint64 userId)
{
    //头像文件不放数据库，只在 users.avatar 存相对路径（如 avatars/user_12.png）
    //服务端统一从 m_avatarDir 下读取，避免任意路径访问风险

    UserRepository repo;
    auto recOpt = repo.findById(userId);
    if (!recOpt.has_value())
    {
        m_socket->write("AVATAR_FAIL##USER_NOT_FOUND\n");
        return;
    }

    QString avatarRel = recOpt->avatar; //例子"avatars/default.png"
    if (avatarRel.isEmpty())
        avatarRel = "avatars/default.png";

    //只取文件名，防止 avatarRel 被注入 "../"
    const QString avatarFileName = QFileInfo(avatarRel).fileName();
    const QString path = QDir(m_avatarDir).filePath(avatarFileName);

    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
    {
        m_socket->write("AVATAR_FAIL##FILE_NOT_FOUND\n");
        return;
    }

    //复用已有的 FILE 协议，客户端 FileClient 不用新增解析逻辑
    const qint64 size = f.size();
    // 让客户端能识别这是头像文件，避免弹“下载完成”
    const QString outName = QString("avatar_%1_%2").arg(userId).arg(avatarFileName);
    // 例：avatar_12_default.png 或 avatar_12_user_12.png
    const QString head = QString("FILE##%1##%2\n").arg(outName).arg(size);
    m_socket->write(head.toUtf8());
    while (!f.atEnd())
        m_socket->write(f.read(4096));
}

//Base64 工具实现
QString ClientWorker::toB64(const QString &s)
{
    return QString::fromUtf8(s.toUtf8().toBase64());
}
QString ClientWorker::fromB64(const QString &b64)
{
    return QString::fromUtf8(QByteArray::fromBase64(b64.toUtf8()));
}

void ClientWorker::handleCommentList(const QString &line)
{
    const QString resourceName = line.section("##", 1, 1).trimmed();

    CommentService service;
    auto res = service.listComments(resourceName);

    if (!res.ok) {
        //也可以选择发 COMMENT_FAIL；这里先用 begin/end 返回空列表更简单
        m_socket->write(QString("COMMENT_BEGIN##%1\n").arg(resourceName).toUtf8());
        m_socket->write(QString("COMMENT_END##%1\n").arg(resourceName).toUtf8());
        return;
    }

    //分批多行返回：BEGIN -> ITEM... -> END
    m_socket->write(QString("COMMENT_BEGIN##%1\n").arg(resourceName).toUtf8());

    for (const auto &c : res.items)
    {
        // username/createdAt/content 全部 base64，避免中文/换行/分隔符影响行协议
        const QString msg = QString("COMMENT_ITEM##%1##%2##%3##%4##%5\n")
                                .arg(c.id)
                                .arg(c.userId)
                                .arg(toB64(c.username))
                                .arg(toB64(c.createdAt))
                                .arg(toB64(c.content));
        m_socket->write(msg.toUtf8());
    }

    m_socket->write(QString("COMMENT_END##%1\n").arg(resourceName).toUtf8());
}

void ClientWorker::handleCommentAdd(const QString &line)
{
    // COMMENT_ADD##userId##resourceName##content_b64
    const qint64 userId = line.section("##", 1, 1).toLongLong();
    const QString resourceName = line.section("##", 2, 2).trimmed();
    const QString contentB64 = line.section("##", 3); // 从第3段开始到末尾（content 可能包含 ## 的 b64 不会含 #，但这样更稳）

    const QString content = fromB64(contentB64);

    CommentService service;
    auto res = service.addComment(userId, resourceName, content);

    if (res.ok) {
        m_socket->write(QString("COMMENT_ADD_OK##%1\n").arg(res.commentId).toUtf8());
    } else {
        m_socket->write(QString("COMMENT_ADD_FAIL##%1\n").arg(res.reason).toUtf8());
    }
}
