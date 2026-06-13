/*
 * FileClient：客户端网络模块（纯逻辑类）
 * 作用：
 * 1. 批次上传文件（一次上传多个文件 + 标签 + 资源介绍）
 * 2. 请求批次列表 / 批次内文件列表
 * 3. 下载文件
 * 4. 评论、收藏、我的上传
 *
 * 设计要点（低耦合）：
 * - UI 只调用 uploadBatch()，不关心协议细节
 * - 协议解析（行协议 + 二进制下载）全部封装在本类内部
 */

#ifndef FILECLIENT_H
#define FILECLIENT_H

#include <QObject>
#include <QTcpSocket>
#include <QFile>
#include <QTimer>
#include <QVector>
#include <QFileInfo>

class MainWindow;

//CommentDto：客户端用于展示的一条评论（已解码为明文，content 允许换行）
struct CommentDto
{
    qint64 id = 0;
    qint64 userId = 0;
    QString username;
    QString createdAt;
    QString content;
};

// 上传批次DTO：主界面列表中的一条
struct SessionDto
{
    qint64 id = 0;
    qint64 userId = 0;
    QString title;          // 批次名
    QString tags;
    QString description;    // 资源介绍
    int fileCount = 0;
    QString createdAt;
};

// 批次内单个文件DTO
struct ResourceDto
{
    qint64 id = 0;
    QString filename;
    qint64 size = 0;
    QString uploadedAt;
};

class FileClient : public QObject
{
    Q_OBJECT

public:
    explicit FileClient(QTcpSocket *socket, MainWindow *ui);

    //对外接口
    //资源文件相关
    // ====== 批次上传：一次上传包含多个文件 + 标签 + 资源介绍 ======
    // 协议三步流程：
    //   1. UPLOAD_BATCH##文件数##userId##标签(B64)##介绍(B64)\n   …批次头
    //   2. FILE##文件大小##文件名(B64)\n + 二进制数据                  …逐文件发送
    //   3. 服务端回复 BATCH_OK##sessionId\n                         …入库确认
    void uploadBatch(const QStringList &filePaths,
                     qint64 userId,
                     const QStringList &tags,
                     const QString &bname,
                     const QString &desc);

    // ====== 新增：批次列表协议 ======
    void requestAllSessions();                          // 请求所有上传批次（替代旧 LIST）
    void requestSessionFiles(qint64 sessionId);         // 请求某批次的文件列表（点击后调用）
    void downloadFile(QString fileName);

    //评论相关（UI 只调这些接口，不关心协议）
    void requestComments(const QString &resourceName);
    void addComment(qint64 userId, const QString &resourceName, const QString &content);
    // 删除评论：UI 只传 userId/commentId，不关心行协议细节
    void deleteComment(qint64 userId, qint64 commentId);

    //我的上传：按用户ID请求上传记录
    void requestMyUploads(qint64 userId);

    //我的上传删除：只传批次ID，由服务端在一个事务里清理整批文件/记录
    void deleteMyUploadSession(qint64 sessionId);

    //收藏功能：添加收藏（收藏/取消收藏的入口之一）
    void addFavorite(const QString &resourceName);
    
    // 请求收藏列表：获取当前用户所有已收藏的资源名
    void getFavorites(qint64 userId);

    // 取消收藏：将指定资源从收藏列表中移除（软删除）
    void removeFavorite(const QString &resourceName);

    // 检查收藏状态：查询某资源是否已被当前用户收藏，用于初始化按钮
    void checkFavorite(const QString &resourceName);

private:
    //接收缓冲区：解决TCP粘包/拆包（命令行、FILE头）
    QByteArray m_buf;

    QTcpSocket *tcpSocket = nullptr;
    MainWindow *mainWindow = nullptr;//用来操作UI(提示框等等)

    //下载相关
    bool isDownloadStart = true; //是否开始接收文件
    QString fileName;
    qint64 fileSize = 0;
    qint64 recvSize = 0;
    QFile file;

    //评论解析缓存（BEGIN -> ITEM... -> END）
    QString m_commentResource;
    QVector<CommentDto> m_pendingComments;

    //我的上传解析缓存（BEGIN -> ITEM... -> END），现按"批次"组织，复用 SessionDto
    qint64 m_myUploadsUserId = 0;
    QVector<SessionDto> m_pendingMyUploads;

    //批次列表解析缓存
    QVector<SessionDto> m_pendingSessions;

    //批次内文件列表解析缓存
    qint64 m_pendingSessionId = 0;
    QVector<ResourceDto> m_pendingResources;

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
    void handleMyUploadsBegin(const QByteArray &line);
    void handleMyUploadsItem(const QByteArray &line);
    void handleMyUploadsEnd(const QByteArray &line);

    // 批次列表解析
    void handleSessionsBegin(const QByteArray &line);
    void handleSessionItem(const QByteArray &line);
    void handleSessionsEnd();

    // 批次内文件列表解析
    void handleSessionFilesBegin(const QByteArray &line);
    void handleFileItem(const QByteArray &line);
    void handleSessionFilesEnd(const QByteArray &line);

    // Base64 工具：
    // content 允许换行，必须 base64 后再放入行协议
    static QString toB64(const QString &s);
    static QString fromB64(const QString &b64);

signals:
    void resourcesUpdated(const QStringList &list);  // 服务端文件列表更新时发出
    void fileReceived(const QString &fileName, const QString &localPath);  // 文件下载完成

    // 批次上传完成通知（sessionId = upload_sessions.id）
    void batchUploadFinished(qint64 sessionId);

    // 批次列表更新（主界面展示用）
    void sessionsUpdated(const QVector<SessionDto> &sessions);

    // 某批次文件列表（ResourceDetailDialog 展示用）
    void sessionFilesUpdated(qint64 sessionId, const QVector<ResourceDto> &files);
    // 上传者名称收到（ResourceDetailDialog 展示用）
    void uploaderReceived(const QString &name);
    //评论列表拉取完成（一次性返回，UI 刷新更简单）
    void commentsUpdated(const QString &resourceName, const QVector<CommentDto> &comments);

    void commentAddOk(qint64 commentId);
    void commentAddFail(const QString &reason);

    void commentDelOk(qint64 commentId);
    void commentDelFail(const QString &reason);

    void deleteMyUploadOk(qint64 sessionId);
    void deleteMyUploadFail(const QString &reason);

    //"我的上传"列表刷新结果（按批次返回，复用 SessionDto）
    void myUploadsUpdated(qint64 userId, const QVector<SessionDto> &items);
    //收藏操作结果：成功时返回资源名，UI 据此更新按钮状态
    void addFavoriteOk(const QString &resourceName);
    void addFavoriteFail(const QString &reason);
    // 收藏列表刷新结果：打开收藏对话框时触发
    void favoritesUpdated(const QStringList &favorites);
    // 取消收藏结果：成功时返回资源名，UI 据此切换按钮文字
    void removeFavoriteOk(const QString &resourceName);
    void removeFavoriteFail(const QString &reason);
    // 检查收藏结果：打开资源详情时触发，用于初始化"收藏/已收藏"按钮
    void checkFavoriteOk(const QString &resourceName, bool isFavorited);
    //统一日志出口：UI 只负责显示（低耦合）
    void logLine(const QString &line);

    //下载进度信号
    void downloadProgress(const QString &fileName, qint64 recvSize,
                          qint64 totalSize, int percentage);

    //上传进度信号
    void uploadProgress(const QString &fileName, qint64 sentSize,
                        qint64 totalSize, int percentage);

    //下载完成信号
    void downloadFinished();

    //上传完成信号
    void uploadFinished();

    //取消下载信号（供 TransferDialog 调用）
    void cancelDownloadRequested();

    //取消上传信号（供 TransferDialog 调用）
    void cancelUploadRequested();
};

#endif // FILECLIENT_H
