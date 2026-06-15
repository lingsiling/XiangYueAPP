#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include "tagsearchengine.h"
#include "usersession.h"
#include "fileclient.h"
#include <QMainWindow>
#include <QTcpSocket>
#include <QFile>
#include <QTreeWidget>
#include <QVector>
class FileClient;
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
    TagSearchEngine m_tagSearch;        //标签搜索引擎（HNSW）
    QVector<SessionDto> m_allSessions;  //全量资源批次（服务端推送）
    UserSession m_session;
    void requestAvatarIfNeeded();
    void renderSessions(const QVector<SessionDto> &sessions); //把批次列表渲染到主界面树
    void showUploadProgressDialog(); //显示上传进度条
    void showMyUploadDialog(); //显示“我的上传”UI
    void setCircularAvatar(const QPixmap &pixmap); //设置圆形头像private:
    QTcpSocket *tcpSocket;//通信套接字
    FileClient *fileClient;//文件客户端对象

    MyUploadDialog *m_myUploadDialog = nullptr; //我的上传对话框
};

#endif // MAINWINDOW_H
