#ifndef MYUPLOADDIALOG_H
#define MYUPLOADDIALOG_H

#include <QDialog>
#include "fileclient.h"

namespace Ui {
class MyUploadDialog;
}

// MyUploadDialog：只负责展示“我的上传”界面，不承载任何上传业务逻辑
class MyUploadDialog : public QDialog
{
    Q_OBJECT

public:
    explicit MyUploadDialog(FileClient *fileClient, qint64 userId, QWidget *parent = nullptr);
    ~MyUploadDialog();

private:
    void refreshUploads();
    void renderUploads(const QVector<SessionDto> &items);
    qint64 selectedSessionId() const;

private:
    Ui::MyUploadDialog *ui;
    FileClient *m_fileClient = nullptr;
    qint64 m_userId = 0;
};

#endif // MYUPLOADDIALOG_H