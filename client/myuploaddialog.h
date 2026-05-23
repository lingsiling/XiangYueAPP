#ifndef MYUPLOADDIALOG_H
#define MYUPLOADDIALOG_H

#include <QDialog>

namespace Ui {
class MyUploadDialog;
}

// MyUploadDialog：只负责展示“我的上传”界面，不承载任何上传业务逻辑
class MyUploadDialog : public QDialog
{
    Q_OBJECT

public:
    explicit MyUploadDialog(QWidget *parent = nullptr);
    ~MyUploadDialog();

private:
    Ui::MyUploadDialog *ui;
};

#endif // MYUPLOADDIALOG_H