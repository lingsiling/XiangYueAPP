#include "fileserver.h"
#include <QDir>

FileServer::FileServer(QObject *parent) : QObject(parent)
{
}

// 重写客户端数据（根据客户端发送数据的第一组关键词）
void FileServer::process(QTcpSocket *client, QByteArray data)
{
    QString str = QString::fromUtf8(data);

    if(str.startsWith("UPLOAD##") || !isUploadStart)
    {
        handleUpload(client, data);
    }
    else if(str == "LIST")
    {
        sendFileList(client);
    }
    else if(str.startsWith("DOWNLOAD##"))
    {
        QString fileName = str.split("##")[1];
        sendFile(client, fileName);
    }
}

//客户端请求文件上传（分两步：第一步接收文件信息，第二步接收文件内容）
void FileServer::handleUpload(QTcpSocket *client, QByteArray data)
{
    if(isUploadStart)
    {
        isUploadStart = false;

        QStringList list = QString(data).split("##");

        fileName = list[1];
        fileSize = list[2].toLongLong();
        recvSize = 0;

        QDir().mkpath(saveDir);

        QString path = saveDir + fileName;

        file.setFileName(path);
        bool isOK = file.open(QIODevice::WriteOnly);
        if(false == isOK)
        {
            qDebug()<<"只写打开文件失败 fileserver49";
        }
        return;
    }

    qint64 len = file.write(data);
    recvSize += len;

    if(recvSize >= fileSize)
    {
        file.close();
        isUploadStart = true;

        sendFileList(client); // 自动刷新
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

    QString data = "LIST##" + list.join("##");

    client->write(data.toUtf8());
}

//客户端请求文件下载
void FileServer::sendFile(QTcpSocket *client, const QString &fileName)
{
    QString path = saveDir + fileName;

    QFile file(path);

    if(!file.open(QIODevice::ReadOnly))
    {
        qDebug() << "只读打开文件失败 fileserver87";
        return;
    }

    qint64 size = file.size();

    QString head = QString("FILE##%1##%2").arg(fileName).arg(size);

    client->write(head.toUtf8());

    while(!file.atEnd())
    {
        client->write(file.read(4096));
    }
}
