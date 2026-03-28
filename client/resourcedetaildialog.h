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
    explicit ResourceDetailDialog(QWidget *parent = nullptr,const QString &resourceName = QString(),FileClient *fileClient = nullptr);

    ~ResourceDetailDialog();

private slots:
    void on_buttonDownload_clicked();
    void on_buttonComment_clicked();

private:
    Ui::ResourceDetailDialog *ui;

    QString m_resourceName;
    FileClient *m_fileClient = nullptr;
};

#endif // RESOURCEDETAILDIALOG_H
