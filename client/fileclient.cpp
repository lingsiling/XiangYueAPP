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
        if(pos < 0) break; //不够一行

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

            emit logLine(QString("上传完成：%1").arg(fn));

            //发出上传完成信号
            emit uploadFinished();

            //收到服务端确认后再刷新
            requestList();

            //多文件上传调度点
            //当前文件已完成：关闭当前上传文件并启动下一个任务
            if (m_uploadFile.isOpen())
                m_uploadFile.close();

            m_isUploading = false;
            startNextUpload();
        }
        else if (line.startsWith("BATCH_OK##"))
        {
            // ============================================================
            //  服务端批次入库成功回复：BATCH_OK##sessionId
            //  sessionId 对应 upload_sessions 表的 id，后续可查历史
            //  收到后自动请求 LIST 刷新主界面，让用户看到刚上传的全部文件
            // ============================================================
            const qint64 sessionId = QString::fromUtf8(line).section("##", 1, 1).toLongLong();
            emit logLine(QString("批次上传完成！批次ID: %1").arg(sessionId));
            emit batchUploadFinished(sessionId);     // 通知关注者
            requestAllSessions();                      // 新协议：刷新批次列表
        }
        else if (line.startsWith("BATCH_FAIL##"))
        {
            // 服务端批次入库失败：BATCH_FAIL##失败原因
            const QString reason = QString::fromUtf8(line).section("##", 1).trimmed();
            emit logLine(QString("批次上传失败: %1").arg(reason));
        }
        else if (line.startsWith("COMMENT_BEGIN##"))
        {
            handleCommentBegin(line);
        }
        else if (line.startsWith("SESSIONS_BEGIN##"))
        {
            // 解析服务端返回的批次列表
            handleSessionsBegin(line);
        }
        else if (line.startsWith("SESSION_ITEM##"))
        {
            handleSessionItem(line);
        }
        else if (line.startsWith("SESSIONS_END"))
        {
            handleSessionsEnd();
        }
        else if (line.startsWith("SESSION_FILES_BEGIN##"))
        {
            handleSessionFilesBegin(line);
        }
        else if (line.startsWith("FILE_ITEM##"))
        {
            handleFileItem(line);
        }
        else if (line.startsWith("SESSION_FILES_END##"))
        {
            handleSessionFilesEnd(line);
        }
        else if (line.startsWith("COMMENT_ITEM##"))
        {
            handleCommentItem(line);
        }
        else if (line.startsWith("COMMENT_END##"))
        {
            handleCommentEnd(line);
        }
        else if (line.startsWith("MY_UPLOADS_BEGIN##"))
        {
            handleMyUploadsBegin(line);
        }
        else if (line.startsWith("MY_UPLOADS_ITEM##"))
        {
            handleMyUploadsItem(line);
        }
        else if (line.startsWith("MY_UPLOADS_END##"))
        {
            handleMyUploadsEnd(line);
        }
        else if (line.startsWith("COMMENT_ADD_OK##"))
        {
            const qint64 id = QString::fromUtf8(line).section("##", 1, 1).toLongLong();
            emit commentAddOk(id);
        }
        else if (line.startsWith("COMMENT_ADD_FAIL##"))
        {
            const QString reason = QString::fromUtf8(line).section("##", 1).trimmed();
            emit commentAddFail(reason);
        }
        else if (line.startsWith("COMMENT_DEL_OK##"))
        {
            const qint64 id = QString::fromUtf8(line).section("##", 1, 1).toLongLong();
            emit commentDelOk(id);
        }
        else if (line.startsWith("COMMENT_DEL_FAIL##"))
        {
            const QString reason = QString::fromUtf8(line).section("##", 1).trimmed();
            emit commentDelFail(reason);
        }
        else if (line.startsWith("DELETE_RESOURCE_OK##"))
        {
            const QString fileName = QString::fromUtf8(line).section("##", 1, 1).trimmed();
            emit deleteMyUploadOk(fileName);
        }
        else if (line.startsWith("DELETE_RESOURCE_FAIL##"))
        {
            const QString reason = QString::fromUtf8(line).section("##", 1).trimmed();
            emit deleteMyUploadFail(reason);
        }
        else if (line.startsWith("ADD_FAVORITE_OK##"))
        {
            // 收藏成功：解析资源名，通知 UI
            const QString resourceName = fromB64(QString::fromUtf8(line).section("##", 1, 1));
            emit addFavoriteOk(resourceName);
        }
        else if (line.startsWith("ADD_FAVORITE_FAIL##"))
        {
            // 收藏失败：解析原因，通知 UI 展示错误
            const QString reason = QString::fromUtf8(line).section("##", 1).trimmed();
            emit addFavoriteFail(reason);
        }
        else if (line.startsWith("GET_FAVORITES_OK##"))
        {
            // 获取收藏列表成功：用 || 分隔多个资源名，通知 UI 刷新
            const QString favoritesData = fromB64(QString::fromUtf8(line).section("##", 1, 1));
            const QStringList favorites = favoritesData.split("||", Qt::SkipEmptyParts);
            emit favoritesUpdated(favorites);
        }
        else if (line.startsWith("GET_FAVORITES_FAIL##"))
        {
            // 获取收藏列表失败：返回空列表，避免 UI 卡死
            const QString reason = QString::fromUtf8(line).section("##", 1).trimmed();
            qWarning() << "[FileClient] 获取收藏列表失败:" << reason;
            emit favoritesUpdated(QStringList());
        }
        else if (line.startsWith("REMOVE_FAVORITE_OK##"))
        {
            // 取消收藏成功：解析资源名，通知 UI 更新按钮状态
            const QString resourceName = fromB64(QString::fromUtf8(line).section("##", 1, 1));
            emit removeFavoriteOk(resourceName);
        }
        else if (line.startsWith("REMOVE_FAVORITE_FAIL##"))
        {
            // 取消收藏失败：通知 UI 展示错误原因
            const QString reason = QString::fromUtf8(line).section("##", 1).trimmed();
            emit removeFavoriteFail(reason);
        }
        else if (line.startsWith("CHECK_FAVORITE_OK##"))
        {
            // 检查收藏状态返回：格式为 CHECK_FAVORITE_OK##资源名_b64##状态(0/1)\n
            const QString resourceName = fromB64(QString::fromUtf8(line).section("##", 1, 1));
            const int isFavorited = QString::fromUtf8(line).section("##", 2, 2).toInt();
            emit checkFavoriteOk(resourceName, isFavorited == 1);
        }
        else
        {
            //预留：其他命令
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

    //计算百分比并发出进度信号
    if (fileSize > 0) {
        int percentage = (int)((recvSize * 100) / fileSize);
        emit downloadProgress(fileName, recvSize, fileSize, percentage);
    }

    if(recvSize >= fileSize)
    {
        file.close();
        isDownloadStart = true;

        //通知 UI：某个文件已接收完成（头像也走这里）
        emit fileReceived(fileName, file.fileName());

        //发出下载完成信号
        emit downloadFinished();

        // 不弹窗：由 UI 层（ResourceDetailDialog）负责聚合多文件下载的提示
        // if (!fileName.startsWith("avatar_"))
        //     QMessageBox::information(mainWindow, "完成", "下载完成");
    }
}

//单文件上传：保持兼容（内部直接转到多文件上传）
void FileClient::uploadFile(QString filePath)
{
    uploadFiles(QStringList{filePath});
}

//多文件上传：把任务加入队列并启动
void FileClient::uploadFiles(const QStringList &filePaths)
{
    int added = 0;
    //低FileClient 只关心“路径能否打开/大小”，不关心 UI 选择逻辑
    for (const QString &p : filePaths)
    {
        const QString path = p.trimmed();
        if (path.isEmpty()) continue;

        QFileInfo info(path);
        if (!info.exists() || !info.isFile()) {
            emit logLine(QString("跳过：不存在或不是文件：%1").arg(path));
            continue;
        }

        UploadTask t;
        t.path = path;
        t.name = info.fileName();
        t.size = info.size();

        //简单过滤：空文件不能上传（也可以让服务端拒绝，但这里提前过滤掉，避免浪费上传时间）
        if (t.size < 0) {
            emit logLine(QString("跳过：文件大小异常：%1").arg(path));
            continue;
        }

        m_uploadQueue.enqueue(t);
        added++;
    }

    if (added > 0)
        emit logLine(QString("已加入上传队列：%1 个文件").arg(added));

    //如果当前没有在上传，则立即启动队列
    if (!m_isUploading)
        startNextUpload();
}

//启动队列中的下一个文件上传（一次只上传一个）
void FileClient::startNextUpload()
{
    if (m_isUploading) return;
    if (!tcpSocket || tcpSocket->state() != QAbstractSocket::ConnectedState) {
        emit logLine("上传失败：未连接服务器");
        return;
    }

    if (m_uploadQueue.isEmpty()) {
        emit logLine("上传队列已完成");
        return;
    }

    UploadTask t = m_uploadQueue.dequeue();

    m_uploadFile.setFileName(t.path);
    if (!m_uploadFile.open(QIODevice::ReadOnly))
    {
        //打不开：跳过，继续下一个
        emit logLine(QString("跳过：无法打开文件：%1").arg(t.path));
        m_isUploading = false;
        startNextUpload();
        return;
    }

    //先发头（必须带 '\n'，让服务端进入上传状态）
    //协议升级：UPLOAD##fileName##fileSize##userId
    const qint64 userId = (mainWindow ? mainWindow->currentUserId() : 0);
    const QString head = QString("UPLOAD##%1##%2##%3\n").arg(t.name).arg(t.size).arg(userId);
    tcpSocket->write(head.toUtf8());

    //按块发送文件，每块发送后更新进度
    qint64 sentSize = 0;
    const qint64 chunkSize = 4096;  // 每块 4KB

    //再发二进制
    while(!m_uploadFile.atEnd())
    {
        QByteArray chunk = m_uploadFile.read(chunkSize);
        if (chunk.isEmpty()) break;

        tcpSocket->write(chunk);
        sentSize += chunk.size();

        //发出上传进度信号
        if (t.size > 0) {
            int percentage = (int)((sentSize * 100) / t.size);
            emit uploadProgress(t.name, sentSize, t.size, percentage);
        }
    }
    //这里不立刻 close 文件、不立刻刷新 LIST；
    //必须等待服务端 UPLOAD_OK 再认为上传完成（避免网络缓冲导致“客户端认为发完了但服务端还没写完”）
    m_isUploading = true;
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

//添加 Base64 工具
QString FileClient::toB64(const QString &s)
{
    return QString::fromUtf8(s.toUtf8().toBase64());
}

QString FileClient::fromB64(const QString &b64)
{
    return QString::fromUtf8(QByteArray::fromBase64(b64.toUtf8()));
}

void FileClient::requestComments(const QString &resourceName)
{
    const QString rn = resourceName.trimmed();
    if (rn.isEmpty()) return;

    //  行协议：服务端会返回 COMMENT_BEGIN/ITEM/END
    const QString cmd = QString("COMMENT_LIST##%1\n").arg(rn);
    tcpSocket->write(cmd.toUtf8());
}

void FileClient::addComment(qint64 userId, const QString &resourceName, const QString &content)
{
    const QString rn = resourceName.trimmed();
    if (userId <= 0 || rn.isEmpty()) return;

    // content 允许换行/中文/特殊字符，必须 base64 避免破坏按行解析
    const QString contentB64 = toB64(content);
    const QString cmd = QString("COMMENT_ADD##%1##%2##%3\n").arg(userId).arg(rn).arg(contentB64);
    tcpSocket->write(cmd.toUtf8());
}

void FileClient::handleCommentBegin(const QByteArray &line)
{
    // COMMENT_BEGIN##resourceName
    m_commentResource = QString::fromUtf8(line).section("##", 1).trimmed();
    m_pendingComments.clear();
}

void FileClient::handleCommentItem(const QByteArray &line)
{
    // COMMENT_ITEM##commentId##userId##username_b64##createdAt_b64##content_b64
    const QString s = QString::fromUtf8(line);

    CommentDto c;
    c.id = s.section("##", 1, 1).toLongLong();
    c.userId = s.section("##", 2, 2).toLongLong();

    const QString usernameB64  = s.section("##", 3, 3);
    const QString createdAtB64 = s.section("##", 4, 4);
    const QString contentB64   = s.section("##", 5); // 余下全部是 content_b64

    c.username  = fromB64(usernameB64);
    c.createdAt = fromB64(createdAtB64);
    c.content   = fromB64(contentB64); // 这里会还原多行文本

    m_pendingComments.push_back(c);
}

void FileClient::handleCommentEnd(const QByteArray &line)
{
    // COMMENT_END##resourceName
    const QString rn = QString::fromUtf8(line).section("##", 1).trimmed();

    //只在匹配的批次结束时发信号（避免并发时乱序；目前是单 socket 单请求，已经够用）
    if (rn == m_commentResource)
        emit commentsUpdated(rn, m_pendingComments);

    m_commentResource.clear();
    m_pendingComments.clear();
}

void FileClient::deleteComment(qint64 userId, qint64 commentId)
{
    if (userId <= 0 || commentId <= 0) return;

    //行协议：删除评论（服务端会做“只能删除自己的”强校验）
    const QString cmd = QString("COMMENT_DEL##%1##%2\n").arg(userId).arg(commentId);
    tcpSocket->write(cmd.toUtf8());
}

void FileClient::deleteMyUpload(const QString &fileName)
{
    const QString name = fileName.trimmed();
    if (name.isEmpty()) return;

    //行协议：复用服务端现有资源删除入口，服务端会同步清理 resources/uploads
    const QString cmd = QString("DELETE_RESOURCE##%1\n").arg(name);
    tcpSocket->write(cmd.toUtf8());
}

// 发送收藏请求：客户端 UI 调用的入口，不关心协议细节
void FileClient::addFavorite(const QString &resourceName)
{
    const QString name = resourceName.trimmed();
    if (name.isEmpty()) return;

    // 行协议：ADD_FAVORITE##resourceName_b64
    const QString cmd = QString("ADD_FAVORITE##%1\n").arg(toB64(name));
    tcpSocket->write(cmd.toUtf8());
}

// 请求收藏列表：服务端返回所有 is_active = 1 的收藏资源
void FileClient::getFavorites(qint64 userId)
{
    if (userId <= 0) return;

    // 行协议：GET_FAVORITES##
    const QString cmd = QString("GET_FAVORITES##\n");
    tcpSocket->write(cmd.toUtf8());
}

// 发送取消收藏请求：软删除，服务端只更新 is_active = 0
void FileClient::removeFavorite(const QString &resourceName)
{
    const QString name = resourceName.trimmed();
    if (name.isEmpty()) return;

    // 行协议：REMOVE_FAVORITE##resourceName_b64
    const QString cmd = QString("REMOVE_FAVORITE##%1\n").arg(toB64(name));
    tcpSocket->write(cmd.toUtf8());
}

// 查询指定资源的收藏状态：打开详情页时调用，用于初始化按钮文字
void FileClient::checkFavorite(const QString &resourceName)
{
    const QString name = resourceName.trimmed();
    if (name.isEmpty()) return;

    // 行协议：CHECK_FAVORITE##resourceName_b64
    const QString cmd = QString("CHECK_FAVORITE##%1\n").arg(toB64(name));
    tcpSocket->write(cmd.toUtf8());
}

void FileClient::requestMyUploads(qint64 userId)
{
    if (userId <= 0) return;

    //行协议：请求“我的上传”列表
    const QString cmd = QString("MY_UPLOADS##%1\n").arg(userId);
    tcpSocket->write(cmd.toUtf8());
}

void FileClient::handleMyUploadsBegin(const QByteArray &line)
{
    // MY_UPLOADS_BEGIN##userId
    m_myUploadsUserId = QString::fromUtf8(line).section("##", 1, 1).toLongLong();
    m_pendingMyUploads.clear();
}

void FileClient::handleMyUploadsItem(const QByteArray &line)
{
    // MY_UPLOADS_ITEM##filename_b64##size##uploadedAt_b64
    const QString s = QString::fromUtf8(line);

    MyUploadDto item;
    item.fileName = fromB64(s.section("##", 1, 1));
    item.size = s.section("##", 2, 2).toLongLong();
    item.uploadedAt = fromB64(s.section("##", 3));
    m_pendingMyUploads.push_back(item);
}

void FileClient::handleMyUploadsEnd(const QByteArray &line)
{
    // MY_UPLOADS_END##userId
    const qint64 uid = QString::fromUtf8(line).section("##", 1, 1).toLongLong();

    //只在同一批次结束时发信号，避免污染其他请求
    if (uid == m_myUploadsUserId) {
        emit myUploadsUpdated(uid, m_pendingMyUploads);
    }

    m_myUploadsUserId = 0;
    m_pendingMyUploads.clear();
}

// ====== 批次上传：一次上传包含多个文件 + 标签 + 介绍 ======
// 协议顺序：
//   1. 先发 UPLOAD_BATCH##文件数##userId##标签(B64)##介绍(B64)\n
//   2. 然后逐个发 FILE##文件大小##文件名(B64)\n + 二进制数据
//   3. 全部发完后，服务端事务入库 → 回复 BATCH_OK##sessionId
//
// 与旧 uploadFiles 的区别：
//   - uploadFiles：逐个文件独立上传，每个文件立即入库、立即回复 UPLOAD_OK
//   - uploadBatch：  整个批次在服务端事务中统一入库，全部成功才回复 BATCH_OK
//
// 参数：
//   filePaths - 用户选中的全部文件绝对路径
//   userId    - 当前登录用户ID
//   tags      - 标签列表（如 {"数学", "PPT"}）
//   desc      - 资源介绍文字
void FileClient::uploadBatch(const QStringList &filePaths,
                             qint64 userId,
                             const QStringList &tags,
                             const QString &bname,
                             const QString &desc)
{
    if (!tcpSocket || tcpSocket->state() != QAbstractSocket::ConnectedState) {
        emit logLine("上传失败：未连接服务器");
        return;
    }
    if (filePaths.isEmpty()) return;

    // ====== 第1步：发送批次头 ======
    // 格式：UPLOAD_BATCH##文件数##userId##标签(B64)##介绍(B64)
    // 服务端收到后会记录 m_batchFileCount，进入"批次上传模式"
    const QString tagsStr = tags.join(',');
    const QString batchHead = QString("UPLOAD_BATCH##%1##%2##%3##%4##%5\n")
        .arg(filePaths.size())
        .arg(userId)
        .arg(toB64(bname))          // 批次名（新增字段）
        .arg(toB64(tagsStr))       // Base64 编码避免中文/逗号破坏协议
        .arg(toB64(desc));         // Base64 编码避免换行/## 破坏协议
    tcpSocket->write(batchHead.toUtf8());

    emit logLine(QString("开始批次上传：共 %1 个文件").arg(filePaths.size()));

    // ====== 第2步：逐个发送文件 ======
    for (const QString &filePath : filePaths) {
        QFileInfo info(filePath.trimmed());
        if (!info.exists() || !info.isFile()) {
            emit logLine(QString("跳过不存在文件：%1").arg(filePath));
            continue;
        }

        QFile f(filePath);
        if (!f.open(QIODevice::ReadOnly)) {
            emit logLine(QString("无法打开文件：%1").arg(filePath));
            continue;
        }

        const qint64 size = f.size();

        // 发送文件头：FILE##大小##文件名(B64)
        // 文件名用 Base64 编码，避免中文字符破坏协议解析
        const QString fileHead = QString("FILE##%1##%2\n")
            .arg(size)
            .arg(toB64(info.fileName()));
        tcpSocket->write(fileHead.toUtf8());

        // 发送文件二进制内容（4KB 分块）
        qint64 sent = 0;
        while (sent < size) {
            QByteArray chunk = f.read(4096);
            if (chunk.isEmpty()) break;
            tcpSocket->write(chunk);
            sent += chunk.size();
        }

        f.close();
        emit logLine(QString("  已发送：%1 (%2 字节)")
            .arg(info.fileName())
            .arg(size));
    }

    emit logLine("批次文件发送完毕，等待服务端确认...");
}

// ====== 请求所有上传批次列表（替代旧 LIST） ======
void FileClient::requestAllSessions()
{
    tcpSocket->write("LIST_SESSIONS\n");
}

// ====== 请求某个批次的文件列表（点击批次后） ======
void FileClient::requestSessionFiles(qint64 sessionId)
{
    if (sessionId <= 0) return;
    const QString cmd = QString("SESSION_FILES##%1\n").arg(sessionId);
    tcpSocket->write(cmd.toUtf8());
}

// ====== 解析批次列表 ======
void FileClient::handleSessionsBegin(const QByteArray & /*line*/)
{
    // SESSIONS_BEGIN##总数
    m_pendingSessions.clear();
}

void FileClient::handleSessionItem(const QByteArray &line)
{
    // SESSION_ITEM##id##userId##tags(B64)##desc(B64)##fileCount##createdAt(B64)
    const QString s = QString::fromUtf8(line);
    SessionDto dto;
    dto.id = s.section("##", 1, 1).toLongLong();
    dto.userId = s.section("##", 2, 2).toLongLong();
    dto.tags = fromB64(s.section("##", 3, 3));
    dto.description = fromB64(s.section("##", 4, 4));
    dto.fileCount = s.section("##", 5, 5).toInt();
    dto.createdAt = fromB64(s.section("##", 6, 6));
    m_pendingSessions.append(dto);
}

void FileClient::handleSessionsEnd()
{
    emit sessionsUpdated(m_pendingSessions);
    m_pendingSessions.clear();
}

// ====== 解析批次内文件列表 ======
void FileClient::handleSessionFilesBegin(const QByteArray &line)
{
    // SESSION_FILES_BEGIN##sessionId##文件数
    const QString s = QString::fromUtf8(line);
    m_pendingSessionId = s.section("##", 1, 1).toLongLong();
    m_pendingResources.clear();
}

void FileClient::handleFileItem(const QByteArray &line)
{
    // FILE_ITEM##id##filename(B64)##size##uploadedAt(B64)
    const QString s = QString::fromUtf8(line);
    ResourceDto dto;
    dto.id = s.section("##", 1, 1).toLongLong();
    dto.filename = fromB64(s.section("##", 2, 2));
    dto.size = s.section("##", 3, 3).toLongLong();
    dto.uploadedAt = fromB64(s.section("##", 4, 4));
    m_pendingResources.append(dto);
}

void FileClient::handleSessionFilesEnd(const QByteArray &line)
{
    // SESSION_FILES_END##sessionId
    const qint64 sid = QString::fromUtf8(line).section("##", 1, 1).toLongLong();
    if (sid == m_pendingSessionId) {
        emit sessionFilesUpdated(sid, m_pendingResources);
    }
    m_pendingSessionId = 0;
    m_pendingResources.clear();
}