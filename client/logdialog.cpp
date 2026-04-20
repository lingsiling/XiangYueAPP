#include "logdialog.h"
#include "ui_logdialog.h"
#include "mainwindow.h"
#include "fileclient.h"
#include <QMessageBox>
#include <QDebug>

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

    // 接收服务端返回（REGISTER_OK / LOGIN_OK 等），按行解析
    m_readyReadConn = connect(tcpSocket, &QTcpSocket::readyRead, this, [=]() {
        m_buf += tcpSocket->readAll();
        processLines();
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

void LogDialog::on_buttonregister_clicked()
{
    if (tcpSocket->state() != QAbstractSocket::ConnectedState) {
        QMessageBox::warning(this, "错误", "未连接服务器");
        return;
    }

    const QString username = ui->nameline->text().trimmed();
    const QString password = ui->passline->text();

    if (username.isEmpty() || password.isEmpty()) {
        QMessageBox::warning(this, "错误", "用户名或密码不能为空");
        return;
    }

    // 发送注册命令（行协议，必须带 '\n'）
    const QString cmd = QString("REGISTER##%1##%2\n").arg(username, password);
    tcpSocket->write(cmd.toUtf8());
}

void LogDialog::on_buttonlogin_clicked()
{
    // 判断是否已连接
    if(tcpSocket->state() != QAbstractSocket::ConnectedState)
    {
        qDebug() << "未连接服务器，无法登录";
        return;
    }

    const QString username = ui->nameline->text().trimmed();
    const QString password = ui->passline->text();

    if (username.isEmpty() || password.isEmpty()) {
        QMessageBox::warning(this, "错误", "用户名或密码不能为空");
        return;
    }

    //等待服务端 LOGIN_OK
    const QString cmd = QString("LOGIN##%1##%2\n").arg(username, password);
    tcpSocket->write(cmd.toUtf8());
}

void LogDialog::processLines()
{
    //服务器返回消息都以 '\n' 结尾，所以可以按行拆
    while (true) {
        int pos = m_buf.indexOf('\n');
        if (pos < 0) break;

        QByteArray line = m_buf.left(pos);
        m_buf.remove(0, pos + 1);

        QString msg = QString::fromUtf8(line).trimmed();

        if (msg == "REGISTER_OK") {
            QMessageBox::information(this, "提示", "注册成功，请登录");
        }
        else if (msg.startsWith("REGISTER_FAIL##")) {
            QString reason = msg.section("##", 1, 1);
            QMessageBox::warning(this, "注册失败", reason);
        }
        else if (msg.startsWith("LOGIN_OK##")) {
            // LOGIN_OK##userId##username##avatar
            QStringList p = msg.split("##");

            UserSession s;
            s.userId = p.value(1).toLongLong();
            s.username = p.value(2);
            s.avatar = p.value(3);

            // 登录成功后：不允许 LogDialog 再读 socket（否则会抢包）
            disconnect(m_readyReadConn);

            // 登录成功才进入主界面
            MainWindow *w = new MainWindow(nullptr, tcpSocket);
            w->setSession(s);   // 注入用户会话信息
            w->show();

            this->close();
        }
        else if (msg.startsWith("LOGIN_FAIL##")) {
            QString reason = msg.section("##", 1, 1);
            QMessageBox::warning(this, "登录失败", reason);
        }
        else {
            //其他消息（LIST/FILE 等）不会在登录阶段出现；忽略
        }
    }
}
