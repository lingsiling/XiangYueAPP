#include "logdialog.h"

#include <QApplication>
#include <QFile>
#include <QTextStream>
#include <QDateTime>
#include <QStandardPaths>

//全局日志文件
QFile *g_logFile = nullptr;

//日志消息处理器：同时输出到文件和控制台
void customMessageHandler(QtMsgType type, const QMessageLogContext &context, const QString &msg)
{
    QString text;
    
    switch (type) {
        case QtDebugMsg:
            text = QString("[DEBUG] %1").arg(msg);
            break;
        case QtInfoMsg:
            text = QString("[INFO] %1").arg(msg);
            break;
        case QtWarningMsg:
            text = QString("[WARN] %1").arg(msg);
            break;
        case QtCriticalMsg:
            text = QString("[ERROR] %1").arg(msg);
            break;
        case QtFatalMsg:
            text = QString("[FATAL] %1").arg(msg);
            break;
    }
    
    //加上时间戳
    QString logMsg = QString("[%1] %2").arg(QDateTime::currentDateTime().toString("hh:mm:ss.zzz"), text);
    
    //输出到控制台（stderr）
    fprintf(stderr, "%s\n", logMsg.toLocal8Bit().constData());
    fflush(stderr);
    
    //输出到日志文件
    if (g_logFile && g_logFile->isOpen()) {
        QTextStream out(g_logFile);
        out << logMsg << "\n";
        out.flush();
    }
}

int main(int argc, char *argv[])
{
    // 创建日志文件
    QString logPath = QStandardPaths::writableLocation(QStandardPaths::TempLocation) + "/XiangYueAPP_Client.log";
    g_logFile = new QFile(logPath);
    if (g_logFile->open(QIODevice::Append | QIODevice::Text)) {
        QTextStream out(g_logFile);
        out << "\n========== 新的运行会话 ========== " << QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss") << "\n";
        out.flush();
    }
    
    //安装自定义消息处理器
    qInstallMessageHandler(customMessageHandler);
    
    QApplication a(argc, argv);

    qDebug() << "===== 客户端应用启动 =====";

    LogDialog w;
    w.show();
    
    int result = QCoreApplication::exec();
    
    //清理资源
    if (g_logFile && g_logFile->isOpen()) {
        g_logFile->close();
    }
    delete g_logFile;
    
    return result;
}
