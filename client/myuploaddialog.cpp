#include "myuploaddialog.h"
#include "ui_myuploaddialog.h"

MyUploadDialog::MyUploadDialog(QWidget *parent)
    : QDialog(parent)
    , ui(new Ui::MyUploadDialog)
{
    ui->setupUi(this);

    // 这里只负责把 UI 打开，内部按钮不接业务逻辑，保持低耦合
}

MyUploadDialog::~MyUploadDialog()
{
    delete ui;
}