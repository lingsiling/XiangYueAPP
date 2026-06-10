#include "uploadresourcedialog.h"
#include "ui_uploadresourcedialog.h"
#include "fileclient.h"

#include <QFileDialog>
#include <QFileInfo>
#include <QFile>
#include <QMessageBox>
#include <QInputDialog>

#include <locale>

// 格式化文件大小为可读字符串
static QString formatFileSize(qint64 bytes)
{
    if (bytes < 1024)
        return QString::number(bytes) + " B";
    if (bytes < 1024 * 1024)
        return QString::number(bytes / 1024.0, 'f', 1) + " KB";
    if (bytes < 1024LL * 1024 * 1024)
        return QString::number(bytes / (1024.0 * 1024.0), 'f', 1) + " MB";
    return QString::number(bytes / (1024.0 * 1024.0 * 1024.0), 'f', 2) + " GB";
}

UploadResourceDialog::UploadResourceDialog(FileClient *fileClient, qint64 userId, QWidget *parent)
    : QDialog(parent)
    , ui(new Ui::UploadResourceDialog)
    , m_fileClient(fileClient)
    , m_userId(userId)
{
    ui->setupUi(this);

    // 加载 QSS 样式表
    QFile qssFile(":/qss/uploadresourcedialog_style.qss");
    if (qssFile.open(QFile::ReadOnly)) {
        setStyleSheet(QLatin1String(qssFile.readAll()));
        qssFile.close();
        qDebug() << "UploadResourceDialog 样式表加载成功";
    }
    // ---------- 信号连接 ----------
    connect(ui->btnSelectFiles,  &QPushButton::clicked, this, &UploadResourceDialog::onSelectFiles);
    connect(ui->btnClearFiles,   &QPushButton::clicked, this, &UploadResourceDialog::onClearFiles);
    connect(ui->btnAddTag,       &QPushButton::clicked, this, &UploadResourceDialog::onAddTag);
    connect(ui->btnSubmit,       &QPushButton::clicked, this, &UploadResourceDialog::onSubmit);
    connect(ui->btnCancel,       &QPushButton::clicked, this, &QDialog::reject);

    // 回车也可添加标签
    connect(ui->lineEditTag, &QLineEdit::returnPressed, this, &UploadResourceDialog::onAddTag);

    // 树形列表选择变化时更新统计
    connect(ui->treeWidgetFiles, &QTreeWidget::itemSelectionChanged, this, [this]() {
        int total = ui->treeWidgetFiles->topLevelItemCount();
        int selected = ui->treeWidgetFiles->selectedItems().size();
        ui->labelFileStats->setText(QString("已选 %1 个文件（共 %2 个）").arg(selected).arg(total));
    });

    // 设置表头拉伸
    ui->treeWidgetFiles->header()->setStretchLastSection(true);
}

UploadResourceDialog::~UploadResourceDialog()
{
    delete ui;
}

// ---------- 选择文件（多选）----------
void UploadResourceDialog::onSelectFiles()
{
    const QStringList paths = QFileDialog::getOpenFileNames(this, "选择要上传的文件");
    if (paths.isEmpty()) return;

    for (const QString &path : paths) {
        addFileRow(path);
    }

    int count = ui->treeWidgetFiles->topLevelItemCount();
    ui->labelFileStats->setText(QString("已选 0 个文件（共 %1 个）").arg(count));
}

// ---------- 清除选中 ----------
void UploadResourceDialog::onClearFiles()
{
    QList<QTreeWidgetItem *> sel = ui->treeWidgetFiles->selectedItems();
    for (QTreeWidgetItem *it : sel) {
        delete it;
    }

    int count = ui->treeWidgetFiles->topLevelItemCount();
    ui->labelFileStats->setText(QString("已选 0 个文件（共 %1 个）").arg(count));
}

// ---------- 添加标签 ----------
void UploadResourceDialog::onAddTag()
{
    const QString tag = ui->lineEditTag->text().trimmed();
    if (tag.isEmpty()) return;

    // 去重
    for (int i = 0; i < ui->listWidgetTags->count(); ++i) {
        if (ui->listWidgetTags->item(i)->text() == tag)
            return;
    }

    ui->listWidgetTags->addItem(tag);
    ui->lineEditTag->clear();
}

// ---------- 添加上传 ----------
void UploadResourceDialog::onSubmit()
{
    // 收集文件路径
    QStringList filePaths;
    for (int i = 0; i < ui->treeWidgetFiles->topLevelItemCount(); ++i) {
        QTreeWidgetItem *it = ui->treeWidgetFiles->topLevelItem(i);
        filePaths << it->data(0, Qt::UserRole).toString();
    }

    if (filePaths.isEmpty()) {
        QMessageBox::warning(this, "提示", "请先选择至少一个文件");
        return;
    }

    // 收集标签
    QStringList tags;
    for (int i = 0; i < ui->listWidgetTags->count(); ++i) {
        tags << ui->listWidgetTags->item(i)->text();
    }

    const QString bname = ui->lineEditBatchName->text().trimmed();
    const QString desc = ui->textEditDescription->toPlainText().trimmed();

    // 调用 FileClient 的批次上传接口：一次上传多个文件 + 标签 + 介绍
    if (m_fileClient) {
        m_fileClient->uploadBatch(filePaths, m_userId, tags, bname, desc);
    }

    accept();
}

// ---------- 向 treeWidget 添加一行 ----------
void UploadResourceDialog::addFileRow(const QString &path)
{
    QFileInfo fi(path);

    // 去重
    const QString absPath = fi.absoluteFilePath();
    for (int i = 0; i < ui->treeWidgetFiles->topLevelItemCount(); ++i) {
        if (ui->treeWidgetFiles->topLevelItem(i)->data(0, Qt::UserRole).toString() == absPath)
            return;
    }

    auto *item = new QTreeWidgetItem();
    item->setText(0, fi.fileName());
    item->setText(1, formatFileSize(fi.size()));
    item->setText(2, fi.suffix().toUpper());
    item->setData(0, Qt::UserRole, absPath);  // 存绝对路径

    ui->treeWidgetFiles->addTopLevelItem(item);
}
