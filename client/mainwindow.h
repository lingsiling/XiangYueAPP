#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include  "resourcesearch.h"
#include "usersession.h"
#include <QMainWindow>
#include <QTcpSocket>
#include <QFile>
#include <QTreeWidget>
class FileClient;
class TransferDialog;
class MyUploadDialog;

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
    void setSession(const UserSession &s);   //登录成功后注入会话信息
    qint64 currentUserId() const;

protected:
    Ui::MainWindow *ui;

private:
    ResourceSearch m_search;        //搜索逻辑
    QStringList m_allResources;     //全量资源（服务端 LIST）
    UserSession m_session;
    void requestAvatarIfNeeded();
    void refreshList(const QStringList &list); //刷新UI
    void showUploadProgressDialog(); //显示上传进度条
    void showMyUploadDialog(); //显示“我的上传”UI
    void setCircularAvatar(const QPixmap &pixmap); //设置圆形头像private:
    QTcpSocket *tcpSocket;//通信套接字
    FileClient *fileClient;//文件客户端对象
    TransferDialog *m_uploadDialog = nullptr; //上传进度条对话框
    MyUploadDialog *m_myUploadDialog = nullptr; //我的上传对话框
};

#endif // MAINWINDOW_H
