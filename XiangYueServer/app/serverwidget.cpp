#include "dbmanager.h"
#include "dbconnectionpool.h"
#include "resourceservice.h"
#include "threadpool.h"
#include "serverwidget.h"
#include "iocpserver.h"          // 新 IOCP 架构入口
#include "serverconfig.h"        // 集中的路径/端口配置
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
    ui->textEdit->append("[1/5] 初始化全局线程池...");
    ThreadPool::instance().initialize(-1);  //自动调整线程数
    ui->textEdit->append(QString("线程池就绪 - 最大线程数:%1\n")
                             .arg(ThreadPool::instance().maxThreadCount()));

    //2. 初始化数据库连接池
    ui->textEdit->append("[2/5] 初始化数据库连接池...");
    const QString dbDir = ServerConfig::dbDir();
    const QString dbPath = ServerConfig::dbPath();

    if (!QDir().mkpath(dbDir)) {
        ui->textEdit->append("数据库目录创建失败: " + dbDir);
        return;
    }

    DBConnectionPool::instance().initialize(dbPath);
    ui->textEdit->append("✓ 数据库连接池已初始化\n");

    //3. 初始化主数据库（schema建立）
    ui->textEdit->append("[3/5] 初始化主数据库架构...");
    if (!DBManager::instance().open(dbPath)) {
        ui->textEdit->append("数据库打开失败: " + DBManager::instance().lastErrorText());
        return;
    }

    if (!DBManager::instance().initSchema()) {
        ui->textEdit->append("数据库初始化失败: " + DBManager::instance().lastErrorText());
        return;
    }
    ui->textEdit->append("数据库就绪（SQLite）\n");

    // 先把服务器保存目录中的现有文件同步到 resources 表，避免历史文件没有入库
    ui->textEdit->append("[4/5] 同步服务器资源目录...");
    const QString saveDir = ServerConfig::saveDir();
    QDir().mkpath(saveDir);

    ResourceService resourceService;
    // 启动时做一次目录对账，避免服务器已有文件但数据库里没有记录
    auto syncRes = resourceService.syncDirectory(saveDir);
    if (syncRes.ok) {
        ui->textEdit->append(QString("资源同步完成：写入/更新 %1 条，清理 %2 条\n")
                                 .arg(syncRes.touchedCount)
                                 .arg(syncRes.removedCount));
    } else {
        ui->textEdit->append("资源同步失败: " + syncRes.reason);
    }

    //5. 创建并启动 IOCP 服务器（监听线程 accept + IOCP 线程 I/O + 线程池处理业务）
    ui->textEdit->append("[5/5] 启动 IOCP 服务器...");
    tcpServer = new IocpServer(this);

    // 把服务器日志接到界面（跨线程信号会自动走队列连接，安全更新 UI）。
    // 以 this 为上下文对象：ServerWidget 销毁时连接自动断开。
    connect(tcpServer, &IocpServer::logMessage, this, [this](const QString &msg) {
        ui->textEdit->append(msg);
    });

    // startServer 内部：
    //   - WSAStartup + 创建完成端口
    //   - 启动 IOCP 工作线程（数 = CPU 核心数）
    //   - 启动监听线程：accept 新连接 → 关联完成端口 → 投递首个 WSARecv
    //   - 新连接的 I/O 在 IOCP 线程处理；耗时业务投递到线程池
    const quint16 port = ServerConfig::listenPort();
    bool isOK = tcpServer->startServer(port);

    if (isOK) {
        ui->textEdit->append(QString("IOCP 服务器成功启动，监听端口: %1").arg(port));
        ui->textEdit->append(QString("架构：监听线程Accept → %1个IOCP线程(I/O) → 线程池(业务)")
                                 .arg(tcpServer->ioThreadCount()));
        ui->textEdit->append("\n========== 服务就绪 ==========");
    } else {
        ui->textEdit->append("IOCP 服务器启动失败!");
    }
}

ServerWidget::~ServerWidget()
{
    // 先优雅停止服务器（停监听/IOCP线程/线程池），再销毁界面，
    // 避免停止过程中的日志回调访问已删除的 ui 控件。
    if (tcpServer)
        tcpServer->stopServer();
    delete ui;
}