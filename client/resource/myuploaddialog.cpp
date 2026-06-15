#include "myuploaddialog.h"
#include "ui_myuploaddialog.h"

#include <QMessageBox>
#include <QTreeWidgetItem>
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
    connect(ui->treeWidgetUploads, &QTreeWidget::itemSelectionChanged, this, [this]() {
        ui->btnDeleteUpload->setEnabled(ui->treeWidgetUploads->currentItem() != nullptr);
    });

    connect(ui->btnDeleteUpload, &QPushButton::clicked, this, [this]() {
        if (!m_fileClient || m_userId <= 0)
            return;

        const qint64 sessionId = selectedSessionId();
        if (sessionId <= 0)
            return;

        //取批次名仅用于确认提示，真正的删除以 sessionId 为准
        auto *item = ui->treeWidgetUploads->currentItem();
        const QString title = item ? item->text(0) : QString::number(sessionId);

        // 删除前二次确认，避免误操作
        const auto ret = QMessageBox::question(this,
                                              "确认删除",
                                              QString("确定删除整个资源批次：%1 吗？\n该操作会删除批次内的全部文件，及其对应的 resources、uploads、favorites 和 upload_sessions 记录，且不可恢复。").arg(title),
                                              QMessageBox::Yes | QMessageBox::No,
                                              QMessageBox::No);
        if (ret != QMessageBox::Yes)
            return;

        m_fileClient->deleteMyUploadSession(sessionId);
    });

    if (m_fileClient) {
        connect(m_fileClient, &FileClient::myUploadsUpdated,
                this, [this](qint64 uid, const QVector<SessionDto> &items) {
                    //仅处理当前用户对应的数据批次
                    if (uid != m_userId) return;
                    renderUploads(items);
                });

        connect(m_fileClient, &FileClient::deleteMyUploadOk, this, [this](qint64 sessionId) {
            QMessageBox::information(this, "删除成功",
                                     QString("已删除资源批次（ID：%1）及其全部文件").arg(sessionId));
            //删除后同步刷新”我的上传”和主界面的批次列表
            refreshUploads();
            m_fileClient->requestAllSessions();
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

void MyUploadDialog::renderUploads(const QVector<SessionDto> &items)
{
    ui->treeWidgetUploads->clear();

    //没有任何上传记录时给出占位提示，并设为不可选中，避免误触发"删除"
    if (items.isEmpty()) {
        auto *placeholder = new QTreeWidgetItem(ui->treeWidgetUploads);
        placeholder->setText(0, "暂无上传记录");
        placeholder->setFlags(placeholder->flags() & ~Qt::ItemIsSelectable & ~Qt::ItemIsEnabled);
        ui->btnDeleteUpload->setEnabled(false);
        return;
    }

    for (const auto &it : items) {
        //三列展示：资源名称(批次名) / 文件数 / 上传时间，与主界面批次列表保持一致
        auto *item = new QTreeWidgetItem(ui->treeWidgetUploads);
        item->setText(0, it.title.isEmpty() ? "(无名称)" : it.title);
        item->setText(1, QString::number(it.fileCount));
        item->setText(2, it.createdAt);
        //把批次ID存进 UserRole，删除时直接取用（删除整批以 sessionId 为准）
        item->setData(0, Qt::UserRole, it.id);
    }

    ui->btnDeleteUpload->setEnabled(ui->treeWidgetUploads->currentItem() != nullptr);
}

qint64 MyUploadDialog::selectedSessionId() const
{
    auto *item = ui->treeWidgetUploads->currentItem();
    if (!item) return 0;

    return item->data(0, Qt::UserRole).toLongLong();
}