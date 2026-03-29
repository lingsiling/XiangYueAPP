#include "fileserver.h"
#include <QDir>

FileServer::FileServer(QObject *parent) : QObject(parent)
{
}

//客户端数据（根据客户端发送数据的第一组关键词）
void FileServer::process(QTcpSocket *client, QByteArray data)
{
    qDebug() << "isUploadStart=" << isUploadStart
             << "recvSize/fileSize=" << recvSize << "/" << fileSize
             << "buf=" << m_buf.size();

    m_buf += data;

    //如果正在上传，优先消费二进制缓冲内容
    if(!isUploadStart)
    {
        consumeUploadData(client);
        if(!isUploadStart) return;
    }

    //按行处理命令（LIST/DOWNLOAD/UPLOAD头）
    tryProcessLines(client);

    //可能刚进入上传状态且缓冲区里已粘了内容
    if(!isUploadStart)
    {
        consumeUploadData(client);
    }
}

void FileServer::tryProcessLines(QTcpSocket *client)
{
    while(true)
    {
        int pos = m_buf.indexOf('\n');
        if(pos < 0) break;

        QByteArray line = m_buf.left(pos);
        m_buf.remove(0, pos + 1);

        QString str = QString::fromUtf8(line).trimmed();

        if(str.startsWith("UPLOAD##"))
        {
            isUploadStart = false;

            QStringList list = str.split("##");
            fileName = list.value(1).trimmed();
            fileSize = list.value(2).toLongLong();
            recvSize = 0;

            QDir().mkpath(saveDir);
            QString path = saveDir + fileName;

            file.setFileName(path);
            if(!file.open(QIODevice::WriteOnly))
            {
                qDebug() << "只写打开文件失败:" << path;
                isUploadStart = true;
            }

            //进入上传状态后退出，交给 consumeUploadData()
            if(!isUploadStart) return;
        }
        else if(str == "LIST")
        {
            sendFileList(client);
        }
        else if(str.startsWith("DOWNLOAD##"))
        {
            QString fn = str.split("##").value(1).trimmed();
            sendFile(client, fn);
        }
    }
}

void FileServer::consumeUploadData(QTcpSocket *client)
{
    if(isUploadStart) return;

    qint64 need = fileSize - recvSize;
    qint64 canWrite = qMin<qint64>(need, m_buf.size());
    if(canWrite <= 0) return;

    qint64 len = file.write(m_buf.constData(), canWrite);
    recvSize += len;
    m_buf.remove(0, canWrite);

    if(recvSize >= fileSize)
    {
        file.close();
        isUploadStart = true;
        sendFileList(client);
    }
}

// 发送文件列表（列表刷新）
void FileServer::sendFileList(QTcpSocket *client)
{
    QDir dir(saveDir);

    if (!dir.exists()) {
        qDebug() << "目录不存在:" << saveDir;
        return;
    }

    QStringList list = dir.entryList(QDir::Files);

    QString data = "LIST##" + list.join("##") + "\n";

    client->write(data.toUtf8());
}

//客户端请求文件下载
void FileServer::sendFile(QTcpSocket *client, const QString &fileName)
{
    QString path = saveDir + fileName;

    QFile file(path);

    if(!file.open(QIODevice::ReadOnly))
    {
        qDebug() << "只读打开文件失败 126";
        return;
    }

    qint64 size = file.size();

    QString head = QString("FILE##%1##%2\n").arg(fileName).arg(size);

    client->write(head.toUtf8());

    while(!file.atEnd())
    {
        client->write(file.read(4096));
    }
}
