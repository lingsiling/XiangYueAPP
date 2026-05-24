#include "myuploaddialog.h"
#include "ui_myuploaddialog.h"

#include <QListWidgetItem>

MyUploadDialog::MyUploadDialog(FileClient *fileClient, qint64 userId, QWidget *parent)
    : QDialog(parent)
    , ui(new Ui::MyUploadDialog)
    , m_fileClient(fileClient)
    , m_userId(userId)
{
    ui->setupUi(this);

    //只负责“展示+交互”，数据请求交给 FileClient
    connect(ui->btnClose, &QPushButton::clicked, this, &QDialog::close);
    connect(ui->btnRefreshUploads, &QPushButton::clicked, this, [this]() {
        refreshUploads();
    });

    //暂不改删除功能，保持其他模块行为不变
    ui->btnDeleteUpload->setEnabled(false);

    if (m_fileClient) {
        connect(m_fileClient, &FileClient::myUploadsUpdated,
                this, [this](qint64 uid, const QVector<MyUploadDto> &items) {
                    //仅处理当前用户对应的数据批次
                    if (uid != m_userId) return;
                    renderUploads(items);
                });
    }

    //对话框打开时自动拉一次数据
    refreshUploads();
}

MyUploadDialog::~MyUploadDialog()
{
    delete ui;
}

void MyUploadDialog::refreshUploads()
{
    if (!m_fileClient || m_userId <= 0)
        return;

    m_fileClient->requestMyUploads(m_userId);
}

void MyUploadDialog::renderUploads(const QVector<MyUploadDto> &items)
{
    ui->listWidgetUploads->clear();

    for (const auto &it : items) {
        //列表显示：文件名 + 大小 + 上传时间
        const QString text = QString("%1\t%2 bytes\t%3")
                                 .arg(it.fileName)
                                 .arg(it.size)
                                 .arg(it.uploadedAt);
        ui->listWidgetUploads->addItem(text);
    }
}