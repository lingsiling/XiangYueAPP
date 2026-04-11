#include "resourcedetaildialog.h"
#include "ui_resourcedetaildialog.h"
#include <QMessageBox>
#include <QListWidget>

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

                    ui->listWidgetComments->addItem(text);
                }
            });

    connect(m_fileClient, &FileClient::commentAddOk, this, [=](qint64){
        // 发送成功：清空输入框，并刷新列表（最稳）
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