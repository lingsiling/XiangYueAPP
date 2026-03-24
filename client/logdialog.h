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

private:
    Ui::LogDialog *ui;

    bool isShow;//状态：是否显示密码

    QTcpSocket *tcpSocket;//提供给mainwindow使用的通信套接字
};
#endif // LOGDIALOG_H
