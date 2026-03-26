#include "fileclient.h"
#include "mainwindow.h"
#include <QFileInfo>
#include <QListWidget>
#include <QMessageBox>

//负责时刻响应并处理服务端发送来的消息
FileClient::FileClient(QTcpSocket *socket,MainWindow *ui)
{
    tcpSocket = socket;
    mainWindow = ui;

    // 接收服务器数据
    connect(tcpSocket, &QTcpSocket::readyRead, this, [=]() {

        QByteArray data = tcpSocket->readAll();

        //文件列表
        if(data.startsWith("LIST##"))
        {
            handleList(data);
        }
        else if(data.startsWith("FILE##") || !isDownloadStart)
        {
            //文件下载
            handleDownload(data);
        }
    });
}

// 上传按钮实现(由mainwindow::ui的buttonUpLoad调用）
void FileClient::uploadFile(QString filePath)
{
    QFileInfo info(filePath);

    QString fileName = info.fileName();
    qint64 fileSize = info.size();

    QFile file(filePath);
    bool isOK = file.open(QIODevice::ReadOnly);
    if(false == isOK)
    {
        qDebug()<<"只读打开文件失败 fileclient43";
        return;
    }

    QString head = QString("UPLOAD##%1##%2").arg(fileName).arg(fileSize);

    tcpSocket->write(head.toUtf8());

    while(!file.atEnd())
    {
        tcpSocket->write(file.read(4096));
    }

    QMessageBox::information(mainWindow, "提示", "上传完成");

    requestList();
}

//向服务端发送请求列表
void FileClient::requestList()
{
    tcpSocket->write("LIST");
}

//下载文件
void FileClient::downloadFile(QString fileName)
{
    QString cmd = "DOWNLOAD##" + fileName;
    tcpSocket->write(cmd.toUtf8());
}

//处理列表刷新（服务端发送List后启用的）
void FileClient::handleList(QByteArray data)
{
    QStringList list = QString(data).split("##");
    list.removeFirst();

    mainWindow->findChild<QListWidget*>("listWidget")->clear();
    mainWindow->findChild<QListWidget*>("listWidget")->addItems(list);
}

//下载处理
void FileClient::handleDownload(QByteArray data)
{
    if(isDownloadStart)//第一次接收文件数据(文件信息)
    {
        isDownloadStart = false;

        QStringList list = QString(data).split("##");

        fileName = list[1];
        fileSize = list[2].toLongLong();
        recvSize = 0;

        QString path = "../../../../ClientSave/" + fileName;

        file.setFileName(path);
        bool isOK =file.open(QIODevice::WriteOnly);
        if(false == isOK)
        {
            qDebug()<<"无法打开文件";
        }

        return;
    }

    //后续接收文件数据（文件内容）
    qint64 len = file.write(data);
    recvSize += len;

    if(recvSize >= fileSize)
    {
        file.close();
        isDownloadStart = true;

        QMessageBox::information(mainWindow, "完成", "下载完成");
    }
}