#include "uploadresourcedialog.h"
#include "ui_uploadresourcedialog.h"
#include "fileclient.h"

#include <QFileDialog>
#include <QFileInfo>
#include <QFile>
#include <QMessageBox>
#include <QInputDialog>

#include <locale>

// ============================================================
//  工具函数：将字节数格式化为可读字符串（如 "2.3 MB"）
// ============================================================
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
    , m_fileClient(fileClient)   // 网络通信模块（由 MainWindow 注入，不负责生命周期）
    , m_userId(userId)           // 当前登录用户ID
{
    ui->setupUi(this);

    // 让 QDialog 能渲染 QSS 背景渐变（关键：否则黑色）
    setAttribute(Qt::WA_StyledBackground, true);

    // 加载 QSS 样式表（从 Qt 资源文件）
    QFile qssFile(":/qss/uploadresourcedialog_style.qss");
    if (qssFile.open(QFile::ReadOnly)) {
        setStyleSheet(QLatin1String(qssFile.readAll()));
        qssFile.close();
    }
    // ====== 信号-槽绑定 ======
    // 按钮点击
    connect(ui->btnSelectFiles,  &QPushButton::clicked, this, &UploadResourceDialog::onSelectFiles);
    connect(ui->btnClearFiles,   &QPushButton::clicked, this, &UploadResourceDialog::onClearFiles);
    connect(ui->btnAddTag,       &QPushButton::clicked, this, &UploadResourceDialog::onAddTag);
    connect(ui->btnSubmit,       &QPushButton::clicked, this, &UploadResourceDialog::onSubmit);
    connect(ui->btnCancel,       &QPushButton::clicked, this, &QDialog::reject);

    // 文件列表行数变更 → 自动更新统计标签
    connect(ui->treeWidgetFiles, &QTreeWidget::itemSelectionChanged, this, [this]() {
        const int total = ui->treeWidgetFiles->topLevelItemCount();
        const int selCount = ui->treeWidgetFiles->selectedItems().size();
        ui->labelFileStats->setText(QString("已选择 %1 个文件（共 %2 个）").arg(selCount).arg(total));
    });

    // 统计标签初始显示
    ui->labelFileStats->setText("已选择 0 个文件（共 0 个）");

    // 光标在标签输入框时按回车 → 自动添加标签
    connect(ui->lineEditTag, &QLineEdit::returnPressed, this, &UploadResourceDialog::onAddTag);

    // 设置表头拉伸
    ui->treeWidgetFiles->header()->setStretchLastSection(true);
}

UploadResourceDialog::~UploadResourceDialog()
{
    delete ui;
}

// ============================================================
//  "选择文件"按钮：弹出系统文件对话框，支持多选
//  选中后逐个添加到 treeWidgetFiles（自动去重）
// ============================================================
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

// ============================================================
//  "清除选中"按钮：删除 treeWidgetFiles 中当前选中的行
// ============================================================
void UploadResourceDialog::onClearFiles()
{
    QList<QTreeWidgetItem *> sel = ui->treeWidgetFiles->selectedItems();
    for (QTreeWidgetItem *it : sel) {
        delete it;
    }

    int count = ui->treeWidgetFiles->topLevelItemCount();
    ui->labelFileStats->setText(QString("已选 0 个文件（共 %1 个）").arg(count));
}

// ============================================================
//  "添加标签"按钮 / 回车键：
//  将 lineEditTag 内容加入 listWidgetTags，自动去重
// ============================================================
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

// ============================================================
//  "上传"按钮：收集所有用户输入，调用 FileClient 发起批次上传
//
//  收集内容：
//    - filePaths：treeWidgetFiles 每行的 Qt::UserRole（绝对路径）
//    - tags：     listWidgetTags 每个 item 的文字
//    - desc：     textEditDescription 的文本
//
//  调用链路：
//    onSubmit() → FileClient::uploadBatch() → 发送 UPLOAD_BATCH 协议
// ============================================================
void UploadResourceDialog::onSubmit()
{
    // ====== 资源名称必填校验 ======
    const QString bname = ui->lineEditBatchName->text().trimmed();
    if (bname.isEmpty()) {
        QMessageBox::warning(this, "提示", "请输入资源名称");
        ui->lineEditBatchName->setFocus();
        return;
    }

    // ====== 标签必填校验 ======
    if (ui->listWidgetTags->count() == 0) {
        QMessageBox::warning(this, "提示", "请至少添加一个标签");
        ui->lineEditTag->setFocus();
        return;
    }

    // 收集文件路径
    QStringList filePaths;
    for (int i = 0; i < ui->treeWidgetFiles->topLevelItemCount(); ++i) {
        QTreeWidgetItem *it = ui->treeWidgetFiles->topLevelItem(i);
        filePaths << it->data(0, Qt::UserRole).toString();
    }

    // ====== 文件必须选择 ======
    if (filePaths.isEmpty()) {
        QMessageBox::warning(this, "提示", "请先选择至少一个文件");
        return;
    }

    // 收集标签
    QStringList tags;
    for (int i = 0; i < ui->listWidgetTags->count(); ++i) {
        tags << ui->listWidgetTags->item(i)->text();
    }

    const QString desc = ui->textEditDescription->toPlainText().trimmed();

    // 调用 FileClient 的批次上传接口
    if (m_fileClient) {
        m_fileClient->uploadBatch(filePaths, m_userId, tags, bname, desc);
    }

    accept();
}

// ============================================================
//  向 treeWidgetFiles 添加一行文件信息
//  列结构：文件名 | 大小 | 类型
//  将绝对路径存在 Qt::UserRole 中，供 onSubmit 读取
//  自动去重（相同绝对路径不重复添加）
// ============================================================
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
