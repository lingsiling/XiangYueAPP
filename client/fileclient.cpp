#include "fileclient.h"
#include "mainwindow.h"
#include <QFileInfo>
#include <QListWidget>
#include <QMessageBox>
#include <QDir>
#include <QCoreApplication>

//负责时刻响应并处理服务端发送来的消息
FileClient::FileClient(QTcpSocket *socket,MainWindow *ui)
{
    tcpSocket = socket;
    mainWindow = ui;

    // 接收服务器数据
    connect(tcpSocket, &QTcpSocket::readyRead, this, [=]() {

        //缓冲区+调度
        m_buf += tcpSocket->readAll();

        //正在下载：优先把缓冲区当二进制文件内容消费
        if(!isDownloadStart)
        {
            consumeDownloadData();
            if(!isDownloadStart) return; //别按行解析（避免二进制里出现 '\n'）
        }

        //不在下载状态：按行分发 LIST / FILE头 / 以后留言等
        tryProcessLines();

        //可能刚解析到 FILE 头，进入下载状态，缓冲区里已经粘了文件内容 -> 立即消费一次
        if(!isDownloadStart)
        {
            consumeDownloadData();
        }
    });
}

//非下载状态：按 '\n' 拆行并分发
void FileClient::tryProcessLines()
{
    while(true)
    {
        int pos = m_buf.indexOf('\n');
        if(pos < 0) break; // 不够一行

        QByteArray line = m_buf.left(pos); //不含 '\n'
        m_buf.remove(0, pos + 1);

        if(line.startsWith("LIST##"))
        {
            handleList(line);
        }
        else if(line.startsWith("FILE##"))
        {
            //FILE头是一行，handleDownload 负责解析头并打开文件
            handleDownload(line);

            //进入下载状态后就退出，让 onReadyRead() 去消费二进制内容
            if(!isDownloadStart) return;
        }
        else if(line.startsWith("UPLOAD_OK##"))
        {
            // UPLOAD_OK##fileName
            QString fn = QString::fromUtf8(line).section("##", 1, 1).trimmed();

            QMessageBox::information(mainWindow, "提示", "上传完成：" + fn);

            // 最稳：收到服务端确认后再刷新
            requestList();
        }
        else if (line.startsWith("COMMENT_BEGIN##"))
        {
            handleCommentBegin(line);
        }
        else if (line.startsWith("COMMENT_ITEM##"))
        {
            handleCommentItem(line);
        }
        else if (line.startsWith("COMMENT_END##"))
        {
            handleCommentEnd(line);
        }
        else if (line.startsWith("COMMENT_ADD_OK##"))
        {
            const qint64 id = QString::fromUtf8(line).section("##", 1, 1).toLongLong();
            emit commentAddOk(id);
        }
        else if (line.startsWith("COMMENT_ADD_FAIL##"))
        {
            const QString reason = QString::fromUtf8(line).section("##", 1).trimmed();
            emit commentAddFail(reason);
        }
        else if (line.startsWith("COMMENT_DEL_OK##"))
        {
            const qint64 id = QString::fromUtf8(line).section("##", 1, 1).toLongLong();
            emit commentDelOk(id);
        }
        else if (line.startsWith("COMMENT_DEL_FAIL##"))
        {
            const QString reason = QString::fromUtf8(line).section("##", 1).trimmed();
            emit commentDelFail(reason);
        }
        else
        {
            //预留：收藏等命令
        }
    }
}

//下载状态：按 fileSize 精确消费二进制
void FileClient::consumeDownloadData()
{
    if(isDownloadStart) return;

    qint64 need = fileSize - recvSize;
    qint64 canWrite = qMin<qint64>(need, m_buf.size());
    if(canWrite <= 0) return;

    qint64 len = file.write(m_buf.constData(), canWrite);
    recvSize += len;
    m_buf.remove(0, canWrite);

    if(recvSize >= fileSize)
    {
        file.close();
        isDownloadStart = true;

        // 通知 UI：某个文件已接收完成（头像也走这里）
        emit fileReceived(fileName, file.fileName());

        // 头像文件不弹提示（避免干扰用户）
        if (!fileName.startsWith("avatar_"))
            QMessageBox::information(mainWindow, "完成", "下载完成");
    }
}

//上传按钮实现(由mainwindow::ui的buttonUpLoad调用）
void FileClient::uploadFile(QString filePath)
{
    QFileInfo info(filePath);

    QString fileName = info.fileName();
    qint64 fileSize = info.size();

    QFile file(filePath);
    bool isOK = file.open(QIODevice::ReadOnly);
    if(false == isOK)
    {
        qDebug()<<"只读打开文件失败 fileclient 101";
        return;
    }

    // 先发头（必须带 '\n'，让服务端进入上传状态）
    QString head = QString("UPLOAD##%1##%2\n").arg(fileName).arg(fileSize);

    tcpSocket->write(head.toUtf8());

    // 再发二进制
    while(!file.atEnd())
    {
        tcpSocket->write(file.read(4096));
    }
}

//向服务端发送请求列表
void FileClient::requestList()
{
    tcpSocket->write("LIST\n");
}

//下载文件
void FileClient::downloadFile(QString fileName)
{
    QString cmd = "DOWNLOAD##" + fileName + "\n";
    tcpSocket->write(cmd.toUtf8());
}

//处理列表刷新（服务端发送List后启用的）
void FileClient::handleList(QByteArray data)
{
    QStringList list = QString::fromUtf8(data).split("##");
    list.removeFirst(); //去掉 "LIST"

    emit resourcesUpdated(list);
}

//下载处理只处理 FILE 头（行），不再处理二进制内容（交给 onReadyRead 的 consumeDownloadData）
//FILE##fileName##fileSize\n
void FileClient::handleDownload(QByteArray data)
{
    QStringList list = QString::fromUtf8(data).split("##");

    fileName = list.value(1).trimmed();
    fileSize = list.value(2).toLongLong();
    recvSize = 0;

    QDir().mkpath("../../../../ClientSave/");  //确保目录存在
    QString path = "../../../../ClientSave/" + fileName;

    file.setFileName(path);
    if(!file.open(QIODevice::WriteOnly))
    {
        qDebug() << "无法打开文件:" << path;
        isDownloadStart = true;
        return;
    }

    //进入下载状态：后续缓冲区内容按 size 写入
    isDownloadStart = false;
}

//添加 Base64 工具
QString FileClient::toB64(const QString &s)
{
    return QString::fromUtf8(s.toUtf8().toBase64());
}

QString FileClient::fromB64(const QString &b64)
{
    return QString::fromUtf8(QByteArray::fromBase64(b64.toUtf8()));
}

void FileClient::requestComments(const QString &resourceName)
{
    const QString rn = resourceName.trimmed();
    if (rn.isEmpty()) return;

    //  行协议：服务端会返回 COMMENT_BEGIN/ITEM/END
    const QString cmd = QString("COMMENT_LIST##%1\n").arg(rn);
    tcpSocket->write(cmd.toUtf8());
}

void FileClient::addComment(qint64 userId, const QString &resourceName, const QString &content)
{
    const QString rn = resourceName.trimmed();
    if (userId <= 0 || rn.isEmpty()) return;

    // content 允许换行/中文/特殊字符，必须 base64 避免破坏按行解析
    const QString contentB64 = toB64(content);
    const QString cmd = QString("COMMENT_ADD##%1##%2##%3\n").arg(userId).arg(rn).arg(contentB64);
    tcpSocket->write(cmd.toUtf8());
}

void FileClient::handleCommentBegin(const QByteArray &line)
{
    // COMMENT_BEGIN##resourceName
    m_commentResource = QString::fromUtf8(line).section("##", 1).trimmed();
    m_pendingComments.clear();
}

void FileClient::handleCommentItem(const QByteArray &line)
{
    // COMMENT_ITEM##commentId##userId##username_b64##createdAt_b64##content_b64
    const QString s = QString::fromUtf8(line);

    CommentDto c;
    c.id = s.section("##", 1, 1).toLongLong();
    c.userId = s.section("##", 2, 2).toLongLong();

    const QString usernameB64  = s.section("##", 3, 3);
    const QString createdAtB64 = s.section("##", 4, 4);
    const QString contentB64   = s.section("##", 5); // 余下全部是 content_b64

    c.username  = fromB64(usernameB64);
    c.createdAt = fromB64(createdAtB64);
    c.content   = fromB64(contentB64); // 这里会还原多行文本

    m_pendingComments.push_back(c);
}

void FileClient::handleCommentEnd(const QByteArray &line)
{
    // COMMENT_END##resourceName
    const QString rn = QString::fromUtf8(line).section("##", 1).trimmed();

    //只在匹配的批次结束时发信号（避免并发时乱序；目前是单 socket 单请求，已经够用）
    if (rn == m_commentResource)
        emit commentsUpdated(rn, m_pendingComments);

    m_commentResource.clear();
    m_pendingComments.clear();
}

void FileClient::deleteComment(qint64 userId, qint64 commentId)
{
    if (userId <= 0 || commentId <= 0) return;

    //行协议：删除评论（服务端会做“只能删除自己的”强校验）
    const QString cmd = QString("COMMENT_DEL##%1##%2\n").arg(userId).arg(commentId);
    tcpSocket->write(cmd.toUtf8());
}