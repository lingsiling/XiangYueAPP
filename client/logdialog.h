#ifndef LOGDIALOG_H
#define LOGDIALOG_H

#include <QDialog>

QT_BEGIN_NAMESPACE
namespace Ui {
class LogDialog;
}
QT_END_NAMESPACE

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
};
#endif // LOGDIALOG_H
