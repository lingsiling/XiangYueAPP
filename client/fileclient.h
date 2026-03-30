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
    //接收缓冲区：解决TCP粘包/拆包（命令行、FILE头）
    QByteArray m_buf;

    QTcpSocket *tcpSocket;
    MainWindow *mainWindow;//用来操作UI
    //下载相关
    bool isDownloadStart = true; //是否开始接收文件
    QString fileName;
    qint64 fileSize = 0;
    qint64 recvSize = 0;
    QFile file;

signals:
    void resourcesUpdated(const QStringList &list); //服务端列表更新时发出

private:
    void handleDownload(QByteArray data); //下载处理
    void handleList(QByteArray data);     //列表处理

    //封装后的分发/处理
    void onReadyRead();          //readyRead 入口：只负责追加数据+调度
    void tryProcessLines();      //非下载状态：按 '\n' 拆行并分发
    void consumeDownloadData();  //下载状态：按 size 消费二进制
};

#endif // FILECLIENT_H
