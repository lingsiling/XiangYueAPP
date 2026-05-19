#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include  "resourcesearch.h"
#include "usersession.h"
#include <QMainWindow>
#include <QTcpSocket>
#include <QFile>
class FileClient;
class TransferDialog;

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

protected:
    Ui::MainWindow *ui;

private:
    enum class ResourceViewMode {
        AllResources,
        Favorites
    };

    ResourceSearch m_search;        //搜索逻辑
    QStringList m_allResources;     //全量资源（服务端 LIST）
    QStringList m_favoriteResources;
    UserSession m_session;
    ResourceViewMode m_viewMode = ResourceViewMode::AllResources;
    void requestAvatarIfNeeded();
    void refreshList(const QStringList &list); //刷新UI
    void showUploadProgressDialog(); //显示上传进度条
    void applyCurrentFilter();
    QStringList currentResources() const;
    QStringList filterResources(const QStringList &source, const QString &keyword) const;
    void setViewMode(ResourceViewMode mode);
private:
    QTcpSocket *tcpSocket;//通信套接字
    FileClient *fileClient;//文件客户端对象
    TransferDialog *m_uploadDialog = nullptr; //上传进度条对话框
};

#endif // MAINWINDOW_H
