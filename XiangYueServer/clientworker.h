// clientworker.h -（支持异步任务处理）
#ifndef CLIENTWORKER_H
#define CLIENTWORKER_H

#include <QFileInfo>
#include <QObject>
#include <QTcpSocket>
#include <QFile>
#include <memory>

class TaskQueue;

/*
 * ClientWorker：客户端连接处理器（运行在线程池的工作线程）
 *
 * 改进点：
 *   - 使用TaskQueue异步处理耗时业务（数据库、认证等）
 *   - 使用DBConnectionPool管理数据库连接（线程安全）
 *   - 保持网络IO在本线程，业务逻辑委托给线程池
 *   - 低耦合：UI、网络、业务完全分离
 *
 * 工作流程：
 *   1. socket readyRead -> tryProcessLines (解析命令)
 *   2. 耗时命令 -> 提交到TaskQueue (异步执行)
 *   3. 任务完成 -> 回调写回socket (发送结果)
 */
class ClientWorker : public QObject
{
    Q_OBJECT
public:
    explicit ClientWorker(qintptr socketDescriptor, QObject *parent = nullptr);
    ~ClientWorker();

signals:
    //通知外部worker已完成（用于线程退出）
    void finished();

public slots:
    //线程启动后调用：初始化socket和资源
    void start();

private slots:
    void onReadyRead();
    void onDisconnected();

    //异步任务完成回调
    void onTaskCompleted(const QString &taskType);
    void onTaskError(const QString &taskType, const QString &error);

private:
    //命令解析（网络IO线程执行，快速返回）
    void tryProcessLines();

    //上传处理
    void consumeUploadData();
    void sendFileList();
    void sendFile(const QString &fileName);

    //命令分发（分为同步和异步两类）
    //同步命令：直接执行（快速返回）
    void handleListCommand();
    void handleDownloadCommand(const QString &line);

    //异步命令：提交到TaskQueue（耗时操作）
    void handleRegisterCommand(const QString &line);
    void handleLoginCommand(const QString &line);
    void handleGetAvatarCommand(const QString &line);
    void handleCommentListCommand(const QString &line);
    void handleCommentAddCommand(const QString &line);
    void handleCommentDelCommand(const QString &line);

    //工具函数
    static QString toB64(const QString &s);
    static QString fromB64(const QString &b64);

    //向客户端发送回复（线程安全）
    void sendResponse(const QString &response);

private:
    qintptr m_sd;
    QTcpSocket *m_socket;
    std::shared_ptr<TaskQueue> m_taskQueue;  //异步任务队列

    //网络缓冲区
    QByteArray m_buf;

    //上传状态（每连接独立）
    bool m_isUploadIdle;
    QString m_uploadFileName;
    qint64 m_uploadFileSize;
    qint64 m_uploadRecvSize;
    QFile m_uploadFile;

    //路径配置
    QString m_saveDir;
    QString m_dbPath;
    QString m_avatarDir;
};

#endif // CLIENTWORKER_H