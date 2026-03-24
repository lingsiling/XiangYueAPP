#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QTcpSocket>
#include <QFile>
namespace Ui {
class MainWindow;
}

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr,QTcpSocket *socket = nullptr);
    ~MainWindow();

    void sendData();//发送文件函数
private slots:
    void on_buttonUpload_clicked();

private:
    Ui::MainWindow *ui;

    QTcpSocket *tcpSocket;//通信套接字
    QFile file;//要发送的文件
    QString fileName;//文件名
    qint64 fileSize;//文件大小
    qint64 sendSize;//已发送大小
};

#endif // MAINWINDOW_H
