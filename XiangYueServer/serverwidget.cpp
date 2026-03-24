#include "serverwidget.h"
#include "ui_serverwidget.h"

ServerWidget::ServerWidget(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::ServerWidget)
{
    ui->setupUi(this);

    //创建服务器对象
    tcpServer = new QTcpServer(this);

    //监听（设置端口7777）
    bool isOK = tcpServer->listen(QHostAddress::Any,7777);

    if(isOK)
    {
        ui->textEdit->append("服务器启动成功，监听端口：7777");
    }
    else
    {
        ui->textEdit->append("服务器启动失败!");
        return;
    }

    //有客户端连接
    connect(tcpServer,&QTcpServer::newConnection,this,[=](){
        //获取客户端socket
        tcpSocket = tcpServer->nextPendingConnection();
        //获取客户端的IP和socket
        QString ip = tcpSocket->peerAddress().toString();
        quint16 port = tcpSocket->peerPort();
        //打印信息
        QString msg = QString("客户端连接：[%1:%2]").arg(ip).arg(port);
        ui->textEdit->append(msg);
    });

    //接收客户端数据
    // connect(tcpSocket,&QTcpSocket::readyRead,this,[=](){
    //    //读取数据
    //     QByteArray data = tcpSocket->readAll();
    //     QString str = QString::fromUtf8(data);
    //     ui->textEdit->append("收到数据：" + str);
    // });

    //客户端断开连接
    // connect(tcpSocket, &QTcpSocket::disconnected, this, [=]() {
    //     ui->textEdit->append("客户端断开连接");
    //     tcpSocket->deleteLater(); // 释放资源
    // });
}

ServerWidget::~ServerWidget()
{
    delete ui;
}
