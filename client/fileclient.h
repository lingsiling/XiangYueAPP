/*
 * FileClient：客户端网络模块
 * 纯逻辑类
 * 作用：
 * 1. 上传文件
 * 2. 请求文件列表
 * 3. 下载文件
 */

#ifndef FILECLIENT_H
#define FILECLIENT_H

#include <QObject>
#include <QTcpSocket>
#include <QFile>
#include <QTimer>

class MainWindow;

class FileClient : public QObject
{
    Q_OBJECT

public:
    explicit FileClient(QTcpSocket *socket, MainWindow *ui);

    //对外接口
    void uploadFile(QString filePath);
    void requestList();
    void downloadFile(QString fileName);

private:
    QTcpSocket *tcpSocket;
    MainWindow *mainWindow;//用来操作UI
    //下载相关
    bool isDownloadStart = true; //是否开始接收文件
    QString fileName;
    qint64 fileSize;
    qint64 recvSize;
    QFile file;

    void handleDownload(QByteArray data); //下载处理
    void handleList(QByteArray data);     //列表处理
};

#endif // FILECLIENT_H
