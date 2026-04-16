// clientworker.h
#ifndef CLIENTWORKER_H
#define CLIENTWORKER_H

#include <QFileInfo>
#include <QObject>
#include <QTcpSocket>
#include <QFile>

// ClientWorker：一个连接一个 worker（运行在独立线程）
// - QTcpSocket 必须在本线程创建（避免跨线程引擎错误）
// - m_buf/上传状态都是“每连接一份”，并发不串线
class ClientWorker : public QObject
{
    Q_OBJECT
public:
    explicit ClientWorker(qintptr socketDescriptor, QObject *parent = nullptr);

signals:
    // 通知 ThreadedTcpServer：worker 结束了，可以让线程退出
    void finished();

public slots:
    // 线程 started 后调用：创建 socket + 接管 descriptor + connect 信号
    void start();

private slots:
    void onReadyRead();
    void onDisconnected();

private:
    // 命令解析（空闲状态按行拆包）
    void tryProcessLines();

    // 上传状态：按 m_uploadFileSize 精确消费二进制
    void consumeUploadData();

    // 命令执行
    void sendFileList();
    void sendFile(const QString &fileName);

    void handleRegister(const QString &line);
    void handleLogin(const QString &line);

    void handleGetAvatar(qint64 userId);

    void handleCommentList(const QString &line);
    void handleCommentAdd(const QString &line);

    void handleCommentDel(const QString &line);

    // Base64 工具：
    // 行协议用 '\n' 结尾，content 允许换行/任意字符，所以传输时必须编码避免破坏按行解析。
    static QString toB64(const QString &s);
    static QString fromB64(const QString &b64);

private:
    qintptr m_sd;
    QTcpSocket *m_socket = nullptr;

    QByteArray m_buf;

    // 上传状态（每连接独立）
    bool m_isUploadIdle = true;
    QString m_uploadFileName;
    qint64 m_uploadFileSize = 0;
    qint64 m_uploadRecvSize = 0;
    QFile m_uploadFile;

    QString m_saveDir = "D:/Qt/Projects/XiangYueAPP/ServerSave/";
    QString m_dbPath  = "D:/Qt/Projects/XiangYueAPP/database/xiangyue.db";

    QString m_avatarDir = "D:/Qt/Projects/XiangYueAPP/ServerAvatars/"; //头像保存目录
};

#endif // CLIENTWORKER_H