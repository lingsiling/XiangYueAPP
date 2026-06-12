#include "resourcedetaildialog.h"
#include "ui_resourcedetaildialog.h"
#include "commentbubble.h"
#include <QMessageBox>
#include <QListWidget>
#include <QMenu>
#include <QAction>
#include <QFile>

//  自定义数据角色：在 QListWidgetItem 中存储评论元信息
//  避免从 item->text() 反向解析字符串
static constexpr int ROLE_COMMENT_ID = Qt::UserRole;       // 评论ID
static constexpr int ROLE_OWNER_UID  = Qt::UserRole + 1;   // 评论者ID


//  构造函数 — 初始化 UI + 绑定所有信号
ResourceDetailDialog::ResourceDetailDialog(QWidget *parent,
                                           const QString &resourceName,
                                           FileClient *fileClient,
                                           qint64 userId,
                                           const QString &tags,
                                           const QString &desc)
    : QDialog(parent),
    ui(new Ui::ResourceDetailDialog),
    m_resourceName(resourceName),
    m_sessionId(0),
    m_fileClient(fileClient),
    m_userId(userId),
    m_isFavorited(false)
{
    ui->setupUi(this);

    //解析 "sessionId|标题" 获取批次ID和展示名
    const qint64 pipePos = resourceName.indexOf('|');
    if (pipePos > 0) {
        m_sessionId   = resourceName.left(pipePos).toLongLong();
        m_resourceName = resourceName.mid(pipePos + 1);
    }

    //加载 QSS 样式表
    QFile file(":/qss/resourcedetaildialog_style.qss");
    if (file.open(QFile::ReadOnly)) {
        setStyleSheet(QLatin1String(file.readAll()));
        file.close();
    }

    //初始化基础显示
    ui->labelBatchTitle->setText(m_resourceName);
    ui->buttonFavorite->setText("收藏");

    // 使用外部聊天气泡 delegate（定义在 commentbubble.h 中）
    ui->listWidgetComments->setItemDelegate(new CommentBubbleDelegate(ui->listWidgetComments));

    if (!desc.isEmpty())
        ui->textEditDesc->setPlainText(desc);
    else
        ui->textEditDesc->setPlaceholderText("（暂无描述）");

    // tags 用 "|" 分隔，逐个添加到流式标签列表
    if (!tags.isEmpty()) {
        for (const QString &t : tags.split('|', Qt::SkipEmptyParts)) {
            const QString trimmed = t.trimmed();
            if (!trimmed.isEmpty())
                ui->listWidgetTags->addItem(trimmed);
        }
    } else {
        ui->listWidgetTags->addItem(QString("暂无标签"));
    }

    //按钮显式绑定
    connect(ui->buttonDownloadAll, &QPushButton::clicked,
            this, &ResourceDetailDialog::onDownloadAll);
    connect(ui->buttonComment, &QPushButton::clicked,
            this, &ResourceDetailDialog::on_buttonComment_clicked);

    if (!m_fileClient) return;

    //  批次文件加载 — 按 sessionId 请求服务端文件列表
    if (m_sessionId > 0) {
        // 上传者名：由服务端通过 UPLOADER## 单独推送
        connect(m_fileClient, &FileClient::uploaderReceived, this,
            [=](const QString &name) {
                if (!name.isEmpty())
                    ui->labelUploader->setText(QString("上传者：%1").arg(name));
            });

        // 文件列表：服务端 SESSION_FILES_END 后统一返回
        connect(m_fileClient, &FileClient::sessionFilesUpdated, this,
            [=](qint64 sid, const QVector<ResourceDto> &files)
            {
                if (sid != m_sessionId) return;
                ui->treeWidgetFiles->clear();

                for (const auto &f : files) {
                    auto *item = new QTreeWidgetItem();
                    item->setText(0, f.filename);
                    item->setText(1, QString::number(f.size));
                    item->setData(0, Qt::UserRole, f.filename);
                    ui->treeWidgetFiles->addTopLevelItem(item);
                }

                if (!files.isEmpty() && !files.first().uploadedAt.isEmpty())
                    ui->labelUploadTime->setText(
                        QString("上传时间：%1").arg(files.first().uploadedAt));
            });

        m_fileClient->requestSessionFiles(m_sessionId);
    }

    //双击文件列表行 → 下载该文件
    connect(ui->treeWidgetFiles, &QTreeWidget::itemDoubleClicked, this,
        [=](QTreeWidgetItem *item, int) {
            const QString fn = item->data(0, Qt::UserRole).toString();
            if (fn.isEmpty()) return;
            m_downloadPending = 1;
            m_downloadCompleted = 0;
            m_fileClient->downloadFile(fn);
        });

    //多文件下载只弹一次完成提示
    connect(m_fileClient, &FileClient::downloadFinished, this, [=]() {
        ++m_downloadCompleted;
        if (m_downloadPending > 0 && m_downloadCompleted >= m_downloadPending) {
            QMessageBox::information(this, "下载完成",
                QString("全部 %1 个文件下载完成").arg(m_downloadPending));
            m_downloadPending = 0;
            m_downloadCompleted = 0;
        }
    });

    //  评论功能 — 发送 / 接收 / 删除
    // 收到 COMMENT_END 时全量刷新评论列表
    connect(m_fileClient, &FileClient::commentsUpdated, this,
        [=](const QString &rn, const QVector<CommentDto> &comments)
        {
            if (rn != m_resourceName) return;
            ui->listWidgetComments->clear();
            for (const auto &c : comments) {
                const QString display = QString("%1 [%2]\n%3")
                    .arg(c.username, c.createdAt, c.content);
                auto *item = new QListWidgetItem(display);
                item->setData(ROLE_COMMENT_ID, c.id);
                item->setData(ROLE_OWNER_UID, c.userId);
                // 标记评论归属：本人评论(靠右) / 他人评论(靠左)
                // 仅打标记，左右布局完全交给 CommentBubbleDelegate（低耦合）
                item->setData(CommentBubbleDelegate::IsMineRole,
                              (m_userId > 0 && c.userId == m_userId));
                ui->listWidgetComments->addItem(item);
            }
        });

    // 发送评论成功 → 清空输入框并重新拉取
    connect(m_fileClient, &FileClient::commentAddOk, this, [=](qint64) {
        ui->textEditComment->clear();
        m_fileClient->requestComments(m_resourceName);
    });

    // 发送评论失败
    connect(m_fileClient, &FileClient::commentAddFail, this,
        [=](const QString &reason) { QMessageBox::warning(this, "发送失败", reason); });

    // 删除评论失败
    connect(m_fileClient, &FileClient::commentDelFail, this,
        [=](const QString &reason) { QMessageBox::warning(this, "删除失败", reason); });

    // 删除评论成功 → 重新拉取评论列表（与发送成功后的刷新逻辑一致）
    connect(m_fileClient, &FileClient::commentDelOk, this,
        [=](qint64) { m_fileClient->requestComments(m_resourceName); });

    // 评论列表启用自定义右键菜单：右键自己的评论可删除
    ui->listWidgetComments->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(ui->listWidgetComments, &QListWidget::customContextMenuRequested,
            this, &ResourceDetailDialog::onCommentContextMenu);

    // 收藏功能 — 添加 / 取消 / 状态检查
    connect(m_fileClient, &FileClient::addFavoriteOk, this,
        [=](const QString &rn) {
            if (rn == m_resourceName) { m_isFavorited = true; ui->buttonFavorite->setText("已收藏"); }
        });
    connect(m_fileClient, &FileClient::addFavoriteFail, this,
        [=](const QString &reason) {
            if (reason != "ALREADY_FAVORITED") QMessageBox::warning(this, "收藏失败", reason);
        });
    connect(m_fileClient, &FileClient::removeFavoriteOk, this,
        [=](const QString &rn) {
            if (rn == m_resourceName) { m_isFavorited = false; ui->buttonFavorite->setText("收藏"); }
        });
    connect(m_fileClient, &FileClient::removeFavoriteFail, this,
        [=](const QString &reason) { QMessageBox::warning(this, "取消收藏失败", reason); });
    connect(m_fileClient, &FileClient::checkFavoriteOk, this,
        [=](const QString &rn, bool fav) {
            if (rn == m_resourceName) {
                m_isFavorited = fav;
                ui->buttonFavorite->setText(fav ? "已收藏" : "收藏");
            }
        });

    //打开时自动拉取历史评论和收藏状态
    m_fileClient->requestComments(m_resourceName);
    if (m_userId > 0) m_fileClient->checkFavorite(m_resourceName);
}


// 析构函数
ResourceDetailDialog::~ResourceDetailDialog()
{
    delete ui;
}


// ============================================================
// 槽函数实现
// 监听 commentAddFail 信号，异常时弹窗
// ============================================================
void ResourceDetailDialog::on_buttonComment_clicked()
{
    if (!m_fileClient) { QMessageBox::warning(this, "错误", "评论模块未初始化"); return; }
    if (m_userId <= 0)  { QMessageBox::warning(this, "错误", "未登录，无法发送评论"); return; }

    const QString content = ui->textEditComment->toPlainText();
    if (content.trimmed().isEmpty()) { QMessageBox::warning(this, "提示", "评论内容不能为空"); return; }

    qDebug() << "[Comment] 发送评论: resourceName=" << m_resourceName
             << "userId=" << m_userId << "content=" << content.left(50);
    m_fileClient->addComment(m_userId, m_resourceName, content);
}

void ResourceDetailDialog::on_buttonFavorite_clicked()
{
    if (!m_fileClient) { QMessageBox::warning(this, "错误", "收藏模块未初始化"); return; }
    if (m_userId <= 0)  { QMessageBox::warning(this, "错误", "未登录，无法收藏"); return; }

    if (m_isFavorited)
        m_fileClient->removeFavorite(m_resourceName);
    else
        m_fileClient->addFavorite(m_resourceName);
}

//  下载全部
//  通过 m_downloadPending / m_downloadCompleted 实现聚合提示
void ResourceDetailDialog::onDownloadAll()
{
    const int count = ui->treeWidgetFiles->topLevelItemCount();
    if (count == 0 || !m_fileClient) return;

    m_downloadPending   = count;
    m_downloadCompleted = 0;
    for (int i = 0; i < count; ++i) {
        const QString fn = ui->treeWidgetFiles->topLevelItem(i)->data(0, Qt::UserRole).toString();
        if (!fn.isEmpty()) m_fileClient->downloadFile(fn);
    }
}

//  评论列表右键菜单 — 删除自己发布的评论
//  低耦合：仅复用 FileClient::deleteComment 接口，
//          删除的所有权由服务端做强校验（返回 NOT_OWNER），
//          这里用 item 中缓存的 ROLE_OWNER_UID 做前置过滤——
//          非本人评论直接不弹出菜单，避免无谓的请求
void ResourceDetailDialog::onCommentContextMenu(const QPoint &pos)
{
    // 取右键位置所在的评论项；空白处右键则忽略
    QListWidgetItem *item = ui->listWidgetComments->itemAt(pos);
    if (!item) return;

    // 未登录不允许删除
    if (!m_fileClient || m_userId <= 0) return;

    // 只能删除自己发布的评论：评论者ID与当前用户不一致则不弹菜单
    const qint64 ownerUid  = item->data(ROLE_OWNER_UID).toLongLong();
    const qint64 commentId = item->data(ROLE_COMMENT_ID).toLongLong();
    if (ownerUid != m_userId || commentId <= 0) return;

    // 构建右键菜单，仅含"删除评论"一项
    QMenu menu(this);
    QAction *delAction = menu.addAction("删除评论");

    // 弹出菜单（列表内坐标需转换为屏幕全局坐标）
    QAction *chosen = menu.exec(ui->listWidgetComments->viewport()->mapToGlobal(pos));
    if (chosen != delAction) return;

    // 二次确认，防止误删
    if (QMessageBox::question(this, "删除评论", "确定要删除这条评论吗？")
            != QMessageBox::Yes) return;

    // 发送删除请求；删除结果由 commentDelOk / commentDelFail 信号驱动 UI 刷新
    m_fileClient->deleteComment(m_userId, commentId);
}
