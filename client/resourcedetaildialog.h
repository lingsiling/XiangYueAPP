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
    void on_buttonDownload_clicked();
    void on_buttonComment_clicked();

private:
    Ui::ResourceDetailDialog *ui;

    QString m_resourceName;
    FileClient *m_fileClient = nullptr;
    qint64 m_userId = 0; // 当前登录用户 id（发送评论必须带上）
};

#endif // RESOURCEDETAILDIALOG_H
