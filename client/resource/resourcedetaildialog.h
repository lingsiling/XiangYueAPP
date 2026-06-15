#ifndef RESOURCEDETAILDIALOG_H
#define RESOURCEDETAILDIALOG_H

#include <QDialog>
#include "fileclient.h"

namespace Ui {
class ResourceDetailDialog;
}

class ResourceDetailDialog : public QDialog
{
    Q_OBJECT

public:
    explicit ResourceDetailDialog(QWidget *parent = nullptr,
                                  const QString &resourceName = QString(),
                                  FileClient *fileClient = nullptr,
                                  qint64 userId = 0,
                                  const QString &tags = QString(),
                                  const QString &desc = QString());

    ~ResourceDetailDialog();

    // 保留空实现避免 MOC 自动连接报错"no matching signal"
    // 实际下载由构造函数中 connect(buttonDownloadAll, ...) 处理
    void on_buttonDownload_clicked() {}
    void on_buttonComment_clicked();
    void on_buttonFavorite_clicked();
    void onDownloadAll();                 // "下载全部" 按钮：下载列表所有文件
    void onPreviewSelected();             // "预览" 按钮：请求服务端把选中文件流到内存预览（不下载落盘）

    // 评论列表右键菜单：删除选中的评论（仅限删除自己发布的）
    void onCommentContextMenu(const QPoint &pos);

private:
    Ui::ResourceDetailDialog *ui;

    QString m_resourceName;
    qint64 m_sessionId = 0;
    FileClient *m_fileClient = nullptr;
    qint64 m_userId = 0;
    bool m_isFavorited = false;

    // 下载计数（用于聚合多文件下载的提示）
    int m_downloadPending = 0;     // 待下载文件数（发起下载时+1）
    int m_downloadCompleted = 0;   // 已下载完成数（收到 downloadFinished 时+1） // 是否已收藏（用于切换按钮状态）

    // 预览专用：记录"正在预览请求中"的文件名（防止重复点击 + 回调时匹配是不是本次请求）。
    // 预览不落盘：requestPreview() 后服务端把文件流入内存，收到 previewDataReady/previewFailed
    // 且文件名匹配时弹预览/提示。与下载计数（m_downloadPending）完全无关。
    QString m_previewPendingFile;
};

#endif // RESOURCEDETAILDIALOG_H
