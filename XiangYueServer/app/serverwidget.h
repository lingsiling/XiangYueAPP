#ifndef SERVERWIDGET_H
#define SERVERWIDGET_H

#include <QWidget>

// 前向声明 —— 不暴露 IOCP 内部细节给 UI 层
class IocpServer;

QT_BEGIN_NAMESPACE
namespace Ui {
class ServerWidget;
}
QT_END_NAMESPACE

/*
 * ServerWidget：服务器管理界面
 *
 * 职责：
 *   - 初始化基础设施（线程池、数据库、资源同步）
 *   - 创建并管理 IocpServer（原生 Windows IOCP 服务器）
 *   - 显示服务器运行状态日志（通过 IocpServer::logMessage 信号）
 *
 * 注意：ServerWidget 不直接操作 socket 或处理连接，
 * 所有网络 I/O 由 IocpServer → Connection → ClientWorker 链处理。
 */
class ServerWidget : public QWidget
{
    Q_OBJECT

public:
    explicit ServerWidget(QWidget *parent = nullptr);
    ~ServerWidget() override;

private:
    Ui::ServerWidget *ui;
    IocpServer *tcpServer;  // IOCP 风格服务器（替代旧 ThreadedTcpServer）
};

#endif // SERVERWIDGET_H
