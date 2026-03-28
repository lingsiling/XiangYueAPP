#ifndef FILESERVER_H
#define FILESERVER_H

#include <QTcpServer>
#include <QTcpSocket>
#include <QFile>

class FileServer : public QObject
{
    Q_OBJECT
public:
    explicit FileServer(QObject *parent = nullptr);

    //对外接口（由serverwidget调用）
    void process(QTcpSocket *client, QByteArray data);
private:
    // 上传状态
    bool isUploadStart = true;
    QString fileName;
    qint64 fileSize = 0;
    qint64 recvSize = 0;
    QFile file;

    QString saveDir = "D:/Qt/Projects/XiangYueAPP/ServerSave/"; // 保存目录

    //内部逻辑
    void handleUpload(QTcpSocket *client, QByteArray data);// 重写客户端数据
    void sendFile(QTcpSocket *client, const QString &fileName);// 发送文件
    void sendFileList(QTcpSocket *client);
};

#endif // FILESERVER_H
