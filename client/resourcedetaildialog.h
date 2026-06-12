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
};

#endif // RESOURCEDETAILDIALOG_H
