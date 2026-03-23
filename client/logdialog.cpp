#include "logdialog.h"
#include "ui_logdialog.h"
#include "mainwindow.h"

LogDialog::LogDialog(QWidget *parent)
    : QDialog(parent)
    , ui(new Ui::LogDialog)
{
    ui->setupUi(this);

    // 默认隐藏密码
    isShow = false;
    ui->passline->setEchoMode(QLineEdit::Password);
    ui->buttonShowOrHide->setText("👁"); // 初始显示图标
}

LogDialog::~LogDialog()
{
    delete ui;
}

void LogDialog::on_buttonShowOrHide_clicked()
{
    if(isShow)
    {
        // 当前是显示 → 改为隐藏
        ui->passline->setEchoMode(QLineEdit::Password);
        ui->buttonShowOrHide->setText("👁");
        isShow = false;
    }
    else
    {
        // 当前是隐藏 → 改为显示
        ui->passline->setEchoMode(QLineEdit::Normal);
        ui->buttonShowOrHide->setText("🙈");
        isShow = true;
    }
}


void LogDialog::on_buttonlogin_clicked()
{
    // 创建主窗口
    MainWindow *w = new MainWindow;
    w->show();
    // 关闭当前登录窗口
    this->close();
}

