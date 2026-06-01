#include "myuploaddialog.h"
#include "ui_myuploaddialog.h"

#include <QMessageBox>
#include <QListWidgetItem>
#include <QFile>

MyUploadDialog::MyUploadDialog(FileClient *fileClient, qint64 userId, QWidget *parent)
    : QDialog(parent)
    , ui(new Ui::MyUploadDialog)
    , m_fileClient(fileClient)
    , m_userId(userId)
{
    ui->setupUi(this);

    // 加载样式表
    QFile file(":/qss/myuploaddialog_style.qss");
    if (file.open(QFile::ReadOnly)) {
        setStyleSheet(QLatin1String(file.readAll()));
        file.close();
    }

    //只负责“展示+交互”，数据请求交给 FileClient
    connect(ui->btnClose, &QPushButton::clicked, this, &QDialog::close);
    connect(ui->btnRefreshUploads, &QPushButton::clicked, this, [this]() {
        refreshUploads();
    });

    //删除按钮只在选中条目时可用，避免误删
    ui->btnDeleteUpload->setEnabled(false);
    connect(ui->listWidgetUploads, &QListWidget::itemSelectionChanged, this, [this]() {
        ui->btnDeleteUpload->setEnabled(ui->listWidgetUploads->currentItem() != nullptr);
    });

    connect(ui->btnDeleteUpload, &QPushButton::clicked, this, [this]() {
        if (!m_fileClient || m_userId <= 0)
            return;

        const QString fileName = selectedUploadFileName();
        if (fileName.isEmpty())
            return;

        // 删除前二次确认，避免误操作
        const auto ret = QMessageBox::question(this,
                                              "确认删除",
                                              QString("确定删除选中的上传资源：%1 吗？\n该操作会同时删除 resources , uploads 和 favorites 记录。").arg(fileName),
                                              QMessageBox::Yes | QMessageBox::No,
                                              QMessageBox::No);
        if (ret != QMessageBox::Yes)
            return;

        m_fileClient->deleteMyUpload(fileName);
    });

    if (m_fileClient) {
        connect(m_fileClient, &FileClient::myUploadsUpdated,
                this, [this](qint64 uid, const QVector<MyUploadDto> &items) {
                    //仅处理当前用户对应的数据批次
                    if (uid != m_userId) return;
                    renderUploads(items);
                });

        connect(m_fileClient, &FileClient::deleteMyUploadOk, this, [this](const QString &fileName) {
            QMessageBox::information(this, "删除成功", QString("已删除：%1").arg(fileName));
            //删除后同步刷新“我的上传”和主界面资源列表
            refreshUploads();
            m_fileClient->requestList();
        });

        connect(m_fileClient, &FileClient::deleteMyUploadFail, this, [this](const QString &reason) {
            QMessageBox::warning(this, "删除失败", reason);
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
        auto *item = new QListWidgetItem(text, ui->listWidgetUploads);
        item->setData(Qt::UserRole, it.fileName);
    }

    ui->btnDeleteUpload->setEnabled(ui->listWidgetUploads->currentItem() != nullptr);
}

QString MyUploadDialog::selectedUploadFileName() const
{
    auto *item = ui->listWidgetUploads->currentItem();
    if (!item) return {};

    const QString storedName = item->data(Qt::UserRole).toString().trimmed();
    if (!storedName.isEmpty())
        return storedName;

    //兼容旧列表项格式：没有 UserRole 时，从文本中取第一段
    return item->text().section('\t', 0, 0).trimmed();
}