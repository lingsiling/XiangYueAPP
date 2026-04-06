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
        else
        {
            //预留：留言/收藏等命令
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