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
    QByteArray m_buf;  // 接收缓冲区：解决粘包/拆包（命令行/UPLOAD头）

    // 上传状态
    bool isUploadStart = true;
    QString fileName;
    qint64 fileSize = 0;
    qint64 recvSize = 0;
    QFile file;

    QString saveDir = "D:/Qt/Projects/XiangYueAPP/ServerSave/"; // 保存目录

private:
    //内部逻辑
    void handleUpload(QTcpSocket *client, QByteArray data);// 重写客户端数据
    void sendFile(QTcpSocket *client, const QString &fileName);// 发送文件
    void sendFileList(QTcpSocket *client);

    void tryProcessLines(QTcpSocket *client); //按 '\n' 拆行处理命令
    void consumeUploadData(QTcpSocket *client); //上传状态：按 size 写文件
};

#endif // FILESERVER_H
