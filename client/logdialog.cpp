#include "logdialog.h"
#include "ui_logdialog.h"
#include "mainwindow.h"
#include "fileclient.h"

LogDialog::LogDialog(QWidget *parent)
    : QDialog(parent)
    , ui(new Ui::LogDialog)
{
    ui->setupUi(this);

    tcpSocket = new QTcpSocket(this);

    //一打开登陆界面就连接服务器
    tcpSocket->connectToHost("127.0.0.1",7777);

    // 连接成功
    connect(tcpSocket, &QTcpSocket::connected, this, [=]() {
        qDebug() << "登录界面：已连接服务器";
    });

    // 连接失败
    connect(tcpSocket, &QTcpSocket::errorOccurred, this, [=]() {
        qDebug() << "连接失败：" << tcpSocket->errorString();
    });

    // 默认隐藏密码
    isShow = false;
    ui->passline->setEchoMode(QLineEdit::Password);
    ui->buttonShowOrHide->setText("显示"); // 初始显示图标
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
        ui->buttonShowOrHide->setText("显示");
        isShow = false;
    }
    else
    {
        // 当前是隐藏 → 改为显示
        ui->passline->setEchoMode(QLineEdit::Normal);
        ui->buttonShowOrHide->setText("隐藏");
        isShow = true;
    }
}


void LogDialog::on_buttonlogin_clicked()
{
    // 判断是否已连接
    if(tcpSocket->state() != QAbstractSocket::ConnectedState)
    {
        qDebug() << "未连接服务器，无法登录";
        return;
    }

    // 创建主窗口 并把socket传给mainwindow
    MainWindow *w = new MainWindow(nullptr,tcpSocket);
    w->show();
    // 关闭当前登录窗口
    this->close();
}

