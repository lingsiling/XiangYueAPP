#include "dbmanager.h"
#include "dbconnectionpool.h"
#include "threadpool.h"
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
    , tcpServer(nullptr)
{
    ui->setupUi(this);

    ui->textEdit->append("========== 服务器初始化 ==========\n");

    //1. 初始化线程池（必须首先初始化）
    ui->textEdit->append("[1/4] 初始化全局线程池...");
    ThreadPool::instance().initialize(-1);  //自动调整线程数
    ui->textEdit->append(QString("线程池就绪 - 最大线程数:%1\n")
                             .arg(ThreadPool::instance().maxThreadCount()));

    //2. 初始化数据库连接池
    ui->textEdit->append("[2/4] 初始化数据库连接池...");
    QString dbDir = "D:/Qt/Projects/XiangYueAPP/database";
    const QString dbPath = QDir(dbDir).filePath("xiangyue.db");

    if (!QDir().mkpath(dbDir)) {
        ui->textEdit->append("数据库目录创建失败: " + dbDir);
        return;
    }

    DBConnectionPool::instance().initialize(dbPath);
    ui->textEdit->append("✓ 数据库连接池已初始化\n");

    //3. 初始化主数据库（schema建立）
    ui->textEdit->append("[3/4] 初始化主数据库架构...");
    if (!DBManager::instance().open(dbPath)) {
        ui->textEdit->append("数据库打开失败: " + DBManager::instance().lastErrorText());
        return;
    }

    if (!DBManager::instance().initSchema()) {
        ui->textEdit->append("数据库初始化失败: " + DBManager::instance().lastErrorText());
        return;
    }
    ui->textEdit->append("数据库就绪（SQLite）\n");

    //4. 创建并启动TCP服务器
    ui->textEdit->append("[4/4] 启动TCP服务器...");
    tcpServer = new ThreadedTcpServer(this);

    bool isOK = tcpServer->listen(QHostAddress::Any, 7777);
    if (isOK) {
        ui->textEdit->append("服务器成功启动，监听端口: 7777");
        ui->textEdit->append("\n========== 服务就绪 ==========");
    } else {
        ui->textEdit->append("服务器启动失败!");
    }
}

ServerWidget::~ServerWidget()
{
    delete ui;
}