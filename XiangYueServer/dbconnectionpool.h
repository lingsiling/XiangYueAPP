// dbconnectionpool.h - 数据库连接池
#ifndef DBCONNECTIONPOOL_H
#define DBCONNECTIONPOOL_H

#include <QSqlDatabase>
#include <QQueue>
#include <QMutex>
#include <QString>
#include <memory>

/*
 * DBConnectionPool：数据库连接池
 * 作用：
 *   - 为每个线程维护一个独立的SQLite连接
 *   - 避免SQLite跨线程并发问题
 *   - 提高数据库访问效率
 *
 * 设计特点：
 *   - 线程本地存储：每线程一个连接
 *   - 自动管理：连接的创建/销毁自动化
 *   - 安全性高：避免锁竞争和死锁
 *
 * SQLite特性：
 *   - 单进程库，跨线程使用需特别注意
 *   - 本设计通过"线程本地存储"完全避免并发问题
 *   - 比连接池复用模式（需频繁lock）性能更好
 */
class DBConnectionPool
{
public:
    // 获取单例
    static DBConnectionPool& instance();

    // 初始化连接池
    void initialize(const QString &dbPath);

    // 获取当前线程的数据库连接
    // 如果本线程尚未有连接，自动创建一个
    QSqlDatabase connection();

    // 清理当前线程的连接（线程退出时调用）
    void releaseConnection();

private:
    DBConnectionPool();
    DBConnectionPool(const DBConnectionPool&) = delete;
    DBConnectionPool& operator=(const DBConnectionPool&) = delete;

private:
    QString m_dbPath;
    static thread_local QSqlDatabase m_threadConnection;
};

#endif // DBCONNECTIONPOOL_H