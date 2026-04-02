#ifndef LOGDIALOG_H
#define LOGDIALOG_H

#include <QDialog>
#include <QTcpSocket>

namespace Ui {
class LogDialog;
}


class LogDialog : public QDialog
{
    Q_OBJECT

public:
    explicit LogDialog(QWidget *parent = nullptr);
    ~LogDialog() override;

private slots:
    void on_buttonShowOrHide_clicked();

    void on_buttonlogin_clicked();

    void on_buttonregister_clicked();
private:
    Ui::LogDialog *ui;

    bool isShow = false;//状态：是否显示密码

    QTcpSocket *tcpSocket = nullptr;//提供给mainwindow使用的通信套接字
    // 解决 TCP 粘包：服务端所有回复都是以 '\n' 结尾的一行
    QByteArray m_buf;

private:
    void processLines(); //按行解析服务端返回（REGISTER_OK / LOGIN_OK 等）
};
#endif // LOGDIALOG_H
