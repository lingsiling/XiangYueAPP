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
#include <QVector>

class MainWindow;

// CommentDto：客户端用于展示的一条评论（已解码为明文，content 允许换行）
struct CommentDto
{
    qint64 id = 0;
    qint64 userId = 0;
    QString username;
    QString createdAt;
    QString content;
};

class FileClient : public QObject
{
    Q_OBJECT

public:
    explicit FileClient(QTcpSocket *socket, MainWindow *ui);

    //对外接口
    //资源文件相关
    void uploadFile(QString filePath);
    void requestList();
    void downloadFile(QString fileName);

    //评论相关（UI 只调这些接口，不关心协议
    void requestComments(const QString &resourceName);
    void addComment(qint64 userId, const QString &resourceName, const QString &content);
    // 删除评论：UI 只传 userId/commentId，不关心行协议细节
    void deleteComment(qint64 userId, qint64 commentId);
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

    //评论解析缓存（BEGIN -> ITEM... -> END）
    QString m_commentResource;
    QVector<CommentDto> m_pendingComments;

signals:
    void resourcesUpdated(const QStringList &list); //服务端列表更新时发出
    void fileReceived(const QString &fileName, const QString &localPath); //任何 FILE 下载完成通知

    // 评论列表拉取完成（一次性返回，UI 刷新更简单）
    void commentsUpdated(const QString &resourceName, const QVector<CommentDto> &comments);

    void commentAddOk(qint64 commentId);
    void commentAddFail(const QString &reason);

    void commentDelOk(qint64 commentId);
    void commentDelFail(const QString &reason);

private:
    void handleDownload(QByteArray data); //下载处理
    void handleList(QByteArray data);     //列表处理

    //封装后的分发/处理
    void onReadyRead();          //readyRead 入口：只负责追加数据+调度
    void tryProcessLines();      //非下载状态：按 '\n' 拆行并分发
    void consumeDownloadData();  //下载状态：按 size 消费二进制

    // 评论行解析
    void handleCommentBegin(const QByteArray &line);
    void handleCommentItem(const QByteArray &line);
    void handleCommentEnd(const QByteArray &line);

    // Base64 工具：
    // content 允许换行，必须 base64 后再放入行协议
    static QString toB64(const QString &s);
    static QString fromB64(const QString &b64);
};

#endif // FILECLIENT_H
