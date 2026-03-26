#include "serverwidget.h"
#include "ui_serverwidget.h"
#include "fileserver.h"

ServerWidget::ServerWidget(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::ServerWidget)
{
    ui->setupUi(this);

    //创建文件逻辑对象
    fileServer = new FileServer(this);
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

        //客户端发送请求,接收客户端数据
        connect(tcpSocket, &QTcpSocket::readyRead, this, [=](){

            QByteArray data = tcpSocket->readAll();

            //交给文件逻辑处理
            fileServer->process(tcpSocket, data);
        });

        //客户端断开
        connect(tcpSocket, &QTcpSocket::disconnected, this, [=](){
            ui->textEdit->append("客户端断开连接");
            tcpSocket->deleteLater();
        });
    });
}

ServerWidget::~ServerWidget()
{
    delete ui;
}

