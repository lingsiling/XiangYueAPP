#ifndef SERVERWIDGET_H
#define SERVERWIDGET_H

#include <QWidget>
#include <QTcpServer>
#include <QTcpSocket>

QT_BEGIN_NAMESPACE
namespace Ui {
class ServerWidget;
}
QT_END_NAMESPACE

class ServerWidget : public QWidget
{
    Q_OBJECT

public:
    explicit ServerWidget(QWidget *parent = nullptr);
    ~ServerWidget() override;

private:
    Ui::ServerWidget *ui;

    QTcpServer *tcpServer;//监听套接字（服务器）
    QTcpSocket *tcpSocket;//通信套接字（客户端）
};
#endif // SERVERWIDGET_H
