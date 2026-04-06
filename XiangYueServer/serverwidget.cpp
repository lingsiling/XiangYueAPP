#include "dbmanager.h"
#include "serverwidget.h"
#include "threadedtcpserver.h"
#include "ui_serverwidget.h"
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

    //创建服务器对象
    tcpServer = new ThreadedTcpServer(this);

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
}

ServerWidget::~ServerWidget()
{
    delete ui;
}

