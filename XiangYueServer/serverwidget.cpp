#include "dbmanager.h"
#include "serverwidget.h"
#include "ui_serverwidget.h"
#include "fileserver.h"
#include <QSqlDatabase>
#include <QDir>
#include <QFileInfo>
#include <QDebug>

ServerWidget::ServerWidget(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::ServerWidget)
{
    ui->setupUi(this);

    //数据库保存目录
    QString dbDir = "D:/Qt/Projects/XiangYueAPP/database";
    const QString dbPath = QDir(dbDir).filePath("xiangyue.db");
    ui->textEdit->append("正在初始化数据库...");
    ui->textEdit->append("DB path: " + dbPath);
    //确保目录存在
    if (!QDir().mkpath(dbDir)) {
        //目录创建失败：通常是权限/路径错误
        ui->textEdit->append("数据库目录创建失败: " + dbDir);
    } else {
        //打开数据库文件（不存在会自动创建）
        if (!DBManager::instance().open(dbPath)) {
            ui->textEdit->append("数据库打开失败: " + DBManager::instance().lastErrorText());
        }
        //建表/建索引（可重复执行，不会重复创建）
        else if (!DBManager::instance().initSchema()) {
            ui->textEdit->append("数据库初始化失败: " + DBManager::instance().lastErrorText());
        } else {
            ui->textEdit->append("数据库就绪（SQLite）");
        }
    }

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

            //打印接收数据
            if (data.startsWith("LIST") || data.startsWith("DOWNLOAD##") || data.startsWith("UPLOAD##"))
            {
                ui->textEdit->append("recv head=" + QString::fromUtf8(data.left(80)));
            }
            else
            {
                ui->textEdit->append("recv head=(binary)");
                // 或者：ui->textEdit->append("recv hex=" + data.left(32).toHex(' '));
            }

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

