#include "resourcedetaildialog.h"
#include "ui_resourcedetaildialog.h"
#include <QMessageBox>
#include <QListWidget>
#include <QMenu>

//把 commentId/userId 存在 item->data()，避免 UI 解析字符串
static constexpr int ROLE_COMMENT_ID = Qt::UserRole;
static constexpr int ROLE_OWNER_UID  = Qt::UserRole + 1;

ResourceDetailDialog::ResourceDetailDialog(QWidget *parent,
                                           const QString &resourceName,
                                           FileClient *fileClient,
                                           qint64 userId)
    : QDialog(parent),
    ui(new Ui::ResourceDetailDialog),
    m_resourceName(resourceName),
    m_fileClient(fileClient),
    m_userId(userId)
{
    ui->setupUi(this);

    //显示资源名
    ui->labelResourceName->setText(m_resourceName);

    if (!m_fileClient) return;

    //UI 只监听 FileClient 的信号，不关心协议/数据库
    connect(m_fileClient, &FileClient::commentsUpdated, this,
            [=](const QString &rn, const QVector<CommentDto> &comments)
            {
                if (rn != m_resourceName) return;

                ui->listWidgetComments->clear();

                //显示格式：用户名 + 时间 + 内容（content 允许换行）
                for (const auto &c : comments)
                {
                    const QString text = QString("%1  %2\n%3")
                        .arg(c.username)
                        .arg(c.createdAt)
                        .arg(c.content);

                    //用 item data 保存主键信息，删除时不需要从文本解析
                    auto *item = new QListWidgetItem(text);
                    item->setData(ROLE_COMMENT_ID, c.id);
                    item->setData(ROLE_OWNER_UID,  c.userId);

                    ui->listWidgetComments->addItem(item);
                }
            });

    //---------------------------
    //右键菜单：每一行弹“删除”（只对 item 生效）
    //---------------------------
    ui->listWidgetComments->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(ui->listWidgetComments, &QListWidget::customContextMenuRequested,
            this, [=](const QPoint &pos)
            {
                QListWidgetItem *item = ui->listWidgetComments->itemAt(pos);
                if (!item) return; //点到空白处，不弹

                const qint64 commentId = item->data(ROLE_COMMENT_ID).toLongLong();
                const qint64 ownerUid  = item->data(ROLE_OWNER_UID).toLongLong();

                QMenu menu(this);
                QAction *actDelete = menu.addAction("删除");

                //客户端仅做体验层限制：不是本人评论就禁用“删除”
                //服务端仍必须做强校验（防止伪造请求）
                if (ownerUid != m_userId)
                    actDelete->setEnabled(false);

                QAction *chosen = menu.exec(ui->listWidgetComments->viewport()->mapToGlobal(pos));
                if (!chosen) return;

                if (chosen == actDelete)
                {
                    if (commentId <= 0) return;

                    if (QMessageBox::question(this, "确认", "确定要删除这条留言吗？")!= QMessageBox::Yes)
                        return;

                    //UI 不拼协议，交给 FileClient
                    m_fileClient->deleteComment(m_userId, commentId);
                }
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

    // 打开详情页时先拉取评论列表
    m_fileClient->requestComments(m_resourceName);
}

ResourceDetailDialog::~ResourceDetailDialog()
{
    delete ui;
}

void ResourceDetailDialog::on_buttonDownload_clicked()
{
    if(!m_fileClient)
    {
        QMessageBox::warning(this, "错误", "下载模块未初始化");
        return;
    }
    if(m_resourceName.isEmpty())
    {
        QMessageBox::warning(this, "错误", "资源名为空，无法下载");
        return;
    }

    m_fileClient->downloadFile(m_resourceName);
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