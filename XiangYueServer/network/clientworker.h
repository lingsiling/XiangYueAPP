// clientworker.h — 单连接协议会话（Winsock-free，被 Connection 拥有）
#ifndef CLIENTWORKER_H
#define CLIENTWORKER_H

#include <QByteArray>
#include <QString>
#include <QStringList>
#include <QFile>

class Connection;

/*
 * ClientWorker：一条连接的【协议解析器 / 会话状态机】。
 *
 * 与重构前的本质变化：
 *   - 不再是 QObject、不再持有 QTcpSocket、不再每连接一个 TaskQueue。
 *   - 由 Connection 以 unique_ptr 拥有；只被"处理该连接 recv 完成的 IOCP 线程"访问。
 *     因为同一连接同时只有一个在途 WSARecv，recv 完成天然串行，故本类成员无需加锁。
 *
 * 职责划分（高并发的关键）：
 *   - 解析（廉价、在 IOCP 线程）：拆行、识别命令、解析参数、接收上传的二进制写盘。
 *   - 业务（耗时、在线程池）：凡是查库/读文件等，都通过 m_conn->post() 投递为业务任务，
 *     任务里调用 service，再用 m_conn->postSend() 把响应字节交回网络层发送。
 *     ★ 业务任务只捕获 Connection* 与值参数，绝不捕获 this(ClientWorker)，
 *       因为业务在线程池跑、与解析线程并发，捕获 this 既有生命周期风险也无必要。
 *
 * 协议保持与重构前【逐字节一致】，客户端 fileclient.cpp 无需改动。
 */
class ClientWorker
{
public:
    explicit ClientWorker(Connection *conn);
    ~ClientWorker();

    ClientWorker(const ClientWorker &) = delete;
    ClientWorker &operator=(const ClientWorker &) = delete;

    /*
     * 收到一批网络字节（由 Connection 在 recv 完成时调用）。
     * 追加到缓冲区后驱动解析：命令行 / 上传二进制。
     */
    void onDataReceived(const char *data, int len);

private:
    // —— 解析驱动（均在 IOCP 线程执行）——
    void tryProcessLines();                       // 按 '\n' 拆行并分发命令
    void consumeUploadData();                      // 接收上传的二进制并写盘
    void startReceivingFile(const QString &line);  // 解析 FILE## 头，进入接收态
    void handleBatchUploadCommand(const QString &line); // 解析 UPLOAD_BATCH## 头
    void finalizeBatchUpload();                    // 批次收尾：快照后投递入库任务

    // —— 命令处理：解析参数后 post 业务任务 ——
    void handleListSessionsCommand();
    void handleSessionFilesCommand(const QString &line);
    void handleDownloadCommand(const QString &fileName);
    void handlePreviewCommand(const QString &fileName);
    void handleRegisterCommand(const QString &line);
    void handleLoginCommand(const QString &line);
    void handleGetAvatarCommand(const QString &line);
    void handleCommentListCommand(const QString &line);
    void handleCommentAddCommand(const QString &line);
    void handleCommentDelCommand(const QString &line);
    void handleDeleteSessionCommand(const QString &line);
    void handleMyUploadsCommand(const QString &line);
    void handleAddFavoriteCommand(const QString &line);
    void handleGetFavoritesCommand(const QString &line);
    void handleRemoveFavoriteCommand(const QString &line);
    void handleCheckFavoriteCommand(const QString &line);

    // —— 工具 ——
    static QString toB64(const QString &s);
    static QString fromB64(const QString &b64);

private:
    Connection *m_conn;     // 所属连接（不持有；Connection 拥有本对象，生命周期更长）

    QByteArray m_buf;        // 接收缓冲：解决 TCP 粘包/拆包

    // —— 上传接收状态（仅解析线程访问）——
    bool    m_isUploadIdle = true;   // 是否空闲（非接收二进制态）
    QString m_uploadFileName;
    qint64  m_uploadFileSize = 0;
    qint64  m_uploadRecvSize = 0;
    QFile   m_uploadFile;

    // —— 批次上传状态 ——
    qint64      m_uploadUserId = 0;     // 本批次归属用户
    QStringList m_batchFilePaths;       // 已接收完成的文件绝对路径
    QStringList m_batchTags;            // 标签列表
    QString     m_batchName;            // 批次名
    QString     m_batchDesc;            // 资源介绍
    QString     m_batchSubDir;          // 存储子目录（user_<id>/batch_<ts>）
    qint64      m_batchFileCount = 0;   // 客户端声明的文件总数
    qint64      m_batchRecvCount = 0;   // 已接收完成的文件计数
};

#endif // CLIENTWORKER_H
