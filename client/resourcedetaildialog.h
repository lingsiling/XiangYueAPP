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
                                  qint64 userId = 0);

    ~ResourceDetailDialog();

private slots:
    void on_buttonDownload_clicked();    // 已废弃（UI 中改名 buttonDownloadAll），保留避免 MOC 报错
    void on_buttonComment_clicked();
    void on_buttonFavorite_clicked();

private:
    Ui::ResourceDetailDialog *ui;

    QString m_resourceName;
    qint64 m_sessionId = 0;   // 批次ID（新协议）；0 表示旧单文件模式
    FileClient *m_fileClient = nullptr;
    qint64 m_userId = 0;
    bool m_isFavorited = false; // 是否已收藏（用于切换按钮状态）
};

#endif // RESOURCEDETAILDIALOG_H
