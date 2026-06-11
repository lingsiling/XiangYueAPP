#include "resourcedetaildialog.h"
#include "ui_resourcedetaildialog.h"
#include <QMessageBox>
#include <QListWidget>
#include <QMenu>
#include <QFile>

//把 commentId/userId 存在 item->data()，避免 UI 解析字符串
static constexpr int ROLE_COMMENT_ID = Qt::UserRole;
static constexpr int ROLE_OWNER_UID  = Qt::UserRole + 1;

ResourceDetailDialog::ResourceDetailDialog(QWidget *parent,
                                           const QString &resourceName,
                                           FileClient *fileClient,
                                           qint64 userId,
                                           const QString &tags)
    : QDialog(parent),
    ui(new Ui::ResourceDetailDialog),
    m_resourceName(resourceName),
    m_sessionId(0),
    m_fileClient(fileClient),
    m_userId(userId),
    m_isFavorited(false)
{
    ui->setupUi(this);

    // ====== 解析 resourceName 格式：sessionId|标题 ======
    // 新协议通过 "sessionId|标题" 传递；旧协议纯字符串为资源名
    const qint64 pipePos = resourceName.indexOf('|');
    if (pipePos > 0) {
        m_sessionId = resourceName.left(pipePos).toLongLong();
        m_resourceName = resourceName.mid(pipePos + 1);   // 标题作为显示名
    } else {
        m_sessionId = resourceName.toLongLong();            // 纯数字？（旧协议）
        if (m_sessionId > 0) m_resourceName = "资源详情";
    }

    //加载样式表
    QFile file(":/qss/resourcedetaildialog_style.qss");
    if (file.open(QFile::ReadOnly)) {
        setStyleSheet(QLatin1String(file.readAll()));
        file.close();
    }

    ui->labelBatchTitle->setText(m_resourceName);
    ui->buttonFavorite->setText("收藏");

    // ====== 标签展示：用 QListWidget 实现每个 tag 独立背景色块 ======
    // 与上传资源详情界面的 listWidgetTags 保持一致的视觉效果
    // tags 格式：以 "|" 分隔，如 "数学|PPT|cpp"
    if (!tags.isEmpty()) {
        const QStringList tagList = tags.split('|', Qt::SkipEmptyParts);
        for (const QString &tag : tagList) {
            const QString trimmed = tag.trimmed();
            if (!trimmed.isEmpty()) {
                ui->listWidgetTags->addItem(trimmed);       // 每个标签单独一个 item
            }
        }
    } else {
        ui->listWidgetTags->addItem(QString("暂无标签"));   // 无标签时占位提示
    }

    // ====== 下载计数逻辑：多文件下载只弹一次提示 ======
    connect(m_fileClient, &FileClient::downloadFinished, this, [=]() {
        ++m_downloadCompleted;
        if (m_downloadPending > 0 && m_downloadCompleted >= m_downloadPending) {
            QMessageBox::information(this, "下载完成",
                QString("全部 %1 个文件下载完成").arg(m_downloadPending));
            m_downloadPending = 0;
            m_downloadCompleted = 0;
        }
    });

    // ====== "下载全部" 按钮绑定 ======
    connect(ui->buttonDownloadAll, &QPushButton::clicked, this, &ResourceDetailDialog::onDownloadAll);

    if (!m_fileClient) return;

    // ====== 新协议：按 sessionId 加载批次文件 ======
    if (m_sessionId > 0) {
        // 监听上传者名称
        connect(m_fileClient, &FileClient::uploaderReceived, this,
            [=](const QString &name) {
                if (!name.isEmpty()) {
                    ui->labelUploader->setText(QString("上传者：%1").arg(name));
                }
            });
        // 监听批次文件列表返回
        connect(m_fileClient, &FileClient::sessionFilesUpdated, this,
            [=](qint64 sid, const QVector<ResourceDto> &files)
            {
                if (sid != m_sessionId) return;
                ui->treeWidgetFiles->clear();

                // ====== 填充文件列表（2 列：文件名 | 大小） ======
                for (const auto &f : files) {
                    auto *item = new QTreeWidgetItem();
                    item->setText(0, f.filename);                 // 列0：文件名
                    item->setText(1, QString::number(f.size));    // 列1：文件大小
                    item->setData(0, Qt::UserRole, f.filename);   // 存文件名给下载用
                    ui->treeWidgetFiles->addTopLevelItem(item);
                }

                // ====== 显示上传时间（取第一个文件的上传时间） ======
                if (!files.isEmpty() && !files.first().uploadedAt.isEmpty()) {
                    ui->labelUploadTime->setText(
                        QString("上传时间：%1").arg(files.first().uploadedAt));
                }
            });
        // 请求该批次的文件列表
        m_fileClient->requestSessionFiles(m_sessionId);
    }

    // ====== 双击列表行触发单文件下载 ======
    connect(ui->treeWidgetFiles, &QTreeWidget::itemDoubleClicked, this,
        [=](QTreeWidgetItem *item, int /*column*/)
        {
            const QString fileName = item->data(0, Qt::UserRole).toString();
            if (fileName.isEmpty()) return;
            // 设置计数为1（单文件下载）
            m_downloadPending = 1;
            m_downloadCompleted = 0;
            m_fileClient->downloadFile(fileName);
        });

    //删除结果：成功就刷新列表，失败就提示原因
    connect(m_fileClient, &FileClient::commentDelOk, this, [=](qint64){
        //删除成功后重新拉取一次，避免并发状态不一致
        m_fileClient->requestComments(m_resourceName);
    });

    connect(m_fileClient, &FileClient::commentDelFail, this, [=](const QString &reason){
        QMessageBox::warning(this, "删除失败", reason);
    });

    connect(m_fileClient, &FileClient::commentAddOk, this, [=](qint64){
        //发送成功：清空输入框，并刷新列表（最稳）
        ui->textEditComment->clear();
        m_fileClient->requestComments(m_resourceName);
    });

    connect(m_fileClient, &FileClient::commentAddFail, this, [=](const QString &reason){
        QMessageBox::warning(this, "发送失败", reason);
    });

    //收藏成功处理
    connect(m_fileClient, &FileClient::addFavoriteOk, this, [=](const QString &resourceName){
        if (resourceName == m_resourceName) {
            m_isFavorited = true;
            ui->buttonFavorite->setText("已收藏");
            qDebug() << "[Favorite] 收藏成功" << m_resourceName;
        }
    });

    connect(m_fileClient, &FileClient::addFavoriteFail, this, [=](const QString &reason){
        if (reason != "ALREADY_FAVORITED") {
            QMessageBox::warning(this, "收藏失败", reason);
        }
        qDebug() << "[Favorite] 收藏失败" << reason;
    });

    connect(m_fileClient, &FileClient::removeFavoriteOk, this, [=](const QString &resourceName){
        if (resourceName == m_resourceName) {
            m_isFavorited = false;
            ui->buttonFavorite->setText("收藏");
            qDebug() << "[Favorite] 取消收藏成功" << m_resourceName;
        }
    });

    connect(m_fileClient, &FileClient::removeFavoriteFail, this, [=](const QString &reason){
        QMessageBox::warning(this, "取消收藏失败", reason);
        qDebug() << "[Favorite] 取消收藏失败" << reason;
    });

    // 检查收藏状态结果处理
    connect(m_fileClient, &FileClient::checkFavoriteOk, this, [=](const QString &resourceName, bool isFavorited){
        if (resourceName == m_resourceName) {
            m_isFavorited = isFavorited;
            ui->buttonFavorite->setText(m_isFavorited ? "已收藏" : "收藏");
            qDebug() << "[Favorite] 收藏状态检查完成" << m_resourceName << ":" << (m_isFavorited ? "已收藏" : "未收藏");
        }
    });

    // 打开详情页时先拉取评论列表
    m_fileClient->requestComments(m_resourceName);

    // 打开详情页时检查收藏状态
    if (m_userId > 0) {
        m_fileClient->checkFavorite(m_resourceName);
    }
}

ResourceDetailDialog::~ResourceDetailDialog()
{
    delete ui;
}

void ResourceDetailDialog::on_buttonComment_clicked()
{
    if (!m_fileClient) {
        QMessageBox::warning(this, "错误", "评论模块未初始化");
        return;
    }
    if (m_userId <= 0) {
        QMessageBox::warning(this, "错误", "未登录，无法发送评论");
        return;
    }

    const QString content = ui->textEditComment->toPlainText();
    if (content.trimmed().isEmpty()) {
        QMessageBox::warning(this, "提示", "评论内容不能为空");
        return;
    }

    // content 支持换行；FileClient 会负责 base64 编码，协议层不耦合 UI
    m_fileClient->addComment(m_userId, m_resourceName, content);
}

void ResourceDetailDialog::on_buttonFavorite_clicked()
{
    if (!m_fileClient) {
        QMessageBox::warning(this, "错误", "收藏模块未初始化");
        return;
    }
    if (m_userId <= 0) {
        QMessageBox::warning(this, "错误", "未登录，无法收藏");
        return;
    }
    if (m_isFavorited) {
        m_fileClient->removeFavorite(m_resourceName);
    } else {
        m_fileClient->addFavorite(m_resourceName);
    }
}

// ====== "下载选中" 按钮：遍历选中的行，逐个下载 ======
void ResourceDetailDialog::onDownloadSelected()
{
    QList<QTreeWidgetItem *> selected = ui->treeWidgetFiles->selectedItems();
    if (selected.isEmpty()) {
        QMessageBox::information(this, "提示", "请先在列表中选择要下载的文件");
        return;
    }

    if (!m_fileClient) return;

    // ====== 设置下载计数：选中文件全部完成后才弹窗 ======
    m_downloadPending = selected.size();
    m_downloadCompleted = 0;

    for (QTreeWidgetItem *item : selected) {
        const QString fileName = item->data(0, Qt::UserRole).toString();
        if (!fileName.isEmpty()) {
            m_fileClient->downloadFile(fileName);
        }
    }
}

// ====== "下载全部" 按钮：遍历所有行，逐个下载 ======
void ResourceDetailDialog::onDownloadAll()
{
    const int count = ui->treeWidgetFiles->topLevelItemCount();
    if (count == 0) return;
    if (!m_fileClient) return;

    // ====== 设置下载计数：全部文件完成后才弹窗 ======
    m_downloadPending = count;
    m_downloadCompleted = 0;

    for (int i = 0; i < count; ++i) {
        QTreeWidgetItem *item = ui->treeWidgetFiles->topLevelItem(i);
        const QString fileName = item->data(0, Qt::UserRole).toString();
        if (!fileName.isEmpty()) {
            m_fileClient->downloadFile(fileName);
        }
    }
}
