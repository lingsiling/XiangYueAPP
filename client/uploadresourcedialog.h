#ifndef UPLOADRESOURCEDIALOG_H
#define UPLOADRESOURCEDIALOG_H

#include <QDialog>
#include <QStringList>

class FileClient;

namespace Ui {
class UploadResourceDialog;
}

/*
 * UploadResourceDialog：上传资源详情弹窗
 *
 * 职责（单一职责原则）：
 *   - 收集用户输入：多文件选择、标签添加、资源介绍
 *   - 调用 FileClient 的 uploadBatch 接口发起上传
 *   - 不涉及网络协议、不涉及数据库操作
 *
 * 使用方式：
 *   UploadResourceDialog dlg(fileClient, userId, this);
 *   dlg.exec();  // 模态弹出
 */
class UploadResourceDialog : public QDialog
{
    Q_OBJECT

public:
    explicit UploadResourceDialog(FileClient *fileClient, qint64 userId, QWidget *parent = nullptr);
    ~UploadResourceDialog();

private slots:
    void onSelectFiles();       // 弹出文件选择框（支持多选），选中的文件加入 treeWidgetFiles
    void onClearFiles();        // 删除 treeWidgetFiles 中当前选中的行
    void onAddTag();            // 将 lineEditTag 内容加入 listWidgetTags（自动去重）
    void onSubmit();            // 收集文件路径+标签+介绍 → 调用 FileClient::uploadBatch

private:
    void addFileRow(const QString &path); // 向 treeWidgetFiles 添加一行（文件名|大小|类型），去重

    Ui::UploadResourceDialog *ui;
    FileClient *m_fileClient = nullptr;   // 网络通信模块（由 MainWindow 注入，不负责生命周期）
    qint64 m_userId = 0;                  // 当前登录用户ID
};

#endif // UPLOADRESOURCEDIALOG_H
