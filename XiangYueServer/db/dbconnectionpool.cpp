#include "dbconnectionpool.h"
#include <QSqlError>
#include <QSqlQuery>
#include <QThread>
#include <QDebug>

//线程本地存储：每个线程有自己的连接
thread_local QSqlDatabase DBConnectionPool::m_threadConnection;

DBConnectionPool& DBConnectionPool::instance()
{
    static DBConnectionPool inst;
    return inst;
}

DBConnectionPool::DBConnectionPool()
{
}

void DBConnectionPool::initialize(const QString &dbPath)
{
    m_dbPath = dbPath;
    qDebug() << "[DBConnectionPool] 初始化成功，DB路径:" << dbPath;
}

QSqlDatabase DBConnectionPool::connection()
{
    //检查当前线程是否已有连接
    if (m_threadConnection.isOpen()) {
        return m_threadConnection;
    }

    //创建新连接
    QString connName = QString("sqlite_conn_%1")
                           .arg(reinterpret_cast<quintptr>(QThread::currentThreadId()));

    //检查是否已存在（Qt中可能存在旧连接）
    if (QSqlDatabase::contains(connName)) {
        m_threadConnection = QSqlDatabase::database(connName);
    } else {
        m_threadConnection = QSqlDatabase::addDatabase("QSQLITE", connName);
        m_threadConnection.setDatabaseName(m_dbPath);
    }

    //打开连接
    if (!m_threadConnection.open()) {
        qWarning() << "[DBConnectionPool] 无法打开数据库连接:"
                   << m_threadConnection.lastError().text();
        return QSqlDatabase();
    }

    // ================================================================
    //  并发写优化（IOCP 架构下，多个业务线程会并发访问同一个 SQLite 库）
    //
    //  - journal_mode=WAL：写前日志模式。读写可并发（读不阻塞写、写不阻塞读），
    //    显著减少多线程下的锁等待，是 SQLite 高并发的关键开关。
    //  - busy_timeout=5000：遇到锁时最多自旋等待 5 秒再报错，
    //    避免瞬时争用直接抛 "database is locked"。
    //  - synchronous=NORMAL：WAL 模式下兼顾安全与吞吐的推荐档位。
    // ================================================================
    {
        QSqlQuery pragma(m_threadConnection);
        pragma.exec("PRAGMA journal_mode=WAL;");
        pragma.exec("PRAGMA busy_timeout=5000;");
        pragma.exec("PRAGMA synchronous=NORMAL;");
    }

    qDebug() << "[DBConnectionPool] 为线程创建新连接:" << connName;
    return m_threadConnection;
}

void DBConnectionPool::releaseConnection()
{
    if (m_threadConnection.isOpen()) {
        QString connName = m_threadConnection.connectionName();
        m_threadConnection.close();
        qDebug() << "[DBConnectionPool] 已释放线程连接:" << connName;
    }
}