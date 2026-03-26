#ifndef MAINWINDOW_H
#define MAINWINDOW_H


#include <QMainWindow>
#include <QTcpSocket>
#include <QFile>
class FileClient;

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

protected:
    Ui::MainWindow *ui;
private:
    QTcpSocket *tcpSocket;//通信套接字
    FileClient *fileClient;//文件客户端对象
};

#endif // MAINWINDOW_H
