#include "dbmanager.h"
#include <QSqlQuery>
#include <QSqlError>
#include <QDebug>
#include <QThread>

DBManager::DBManager()
{
    m_db = QSqlDatabase::addDatabase("QSQLITE");
}

DBManager& DBManager::instance()
{
    static DBManager inst;
    return inst;
}

bool DBManager::open(const QString &dbFilePath)
{
    //防止重复 open（重复 open 没意义）
    if (m_db.isOpen())
        return true;

    //SQLite：数据库名就是文件路径
    m_db.setDatabaseName(dbFilePath);

    //open() 失败通常是：路径目录不存在 / 文件被占用 / 权限不足
    if (!m_db.open()) {
        m_lastError = m_db.lastError().text();
        qDebug() << "[DB] open failed:" << m_lastError;
        return false;
    }

    qDebug() << "[DB] opened:" << dbFilePath;
    return true;
}

QSqlDatabase DBManager::db() const
{
    const QString connName = QString("sqlite_conn_%1")
    .arg(reinterpret_cast<quintptr>(QThread::currentThreadId()));

    if (QSqlDatabase::contains(connName))
        return QSqlDatabase::database(connName);

    //fallback：主线程初始化连接
    return m_db;
}

QSqlDatabase DBManager::openForCurrentThread(const QString &dbFilePath)
{
    //每线程一个连接名，避免跨线程共享同一个 QSqlDatabase
    const QString connName = QString("sqlite_conn_%1")
                                 .arg(reinterpret_cast<quintptr>(QThread::currentThreadId()));

    if (QSqlDatabase::contains(connName)) {
        QSqlDatabase db = QSqlDatabase::database(connName);
        if (!db.isOpen()) {
            db.setDatabaseName(dbFilePath);
            db.open();
        }
        return db;
    }

    QSqlDatabase db = QSqlDatabase::addDatabase("QSQLITE", connName);
    db.setDatabaseName(dbFilePath);
    if (!db.open()) {
        qDebug() << "[DB] openForCurrentThread failed:" << db.lastError().text();
    }
    return db;
}

QString DBManager::lastErrorText() const
{
    return m_lastError;
}

bool DBManager::execOrLog(const QString &sql, const char *tag)
{
    // 所有建表/建索引都走这里，保证错误信息统一输出
    QSqlQuery q(m_db);

    if (!q.exec(sql)) {
        m_lastError = q.lastError().text();
        qDebug() << "[DB]" << tag << "failed:" << m_lastError;
        qDebug() << "[DB] sql =" << sql;
        return false;
    }
    return true;
}

bool DBManager::initSchema()
{
    // ================================================================
    //  享阅 数据库表结构
    //  设计原则：最小够用，只保留必要字段
    //  表之间通过 id 关联，SQLite 不强制外键约束
    // ================================================================

    // ---------- 1. 用户表 ----------
    if (!execOrLog(R"SQL(
        CREATE TABLE IF NOT EXISTS users (
            id         INTEGER PRIMARY KEY AUTOINCREMENT,
            username   TEXT    NOT NULL UNIQUE,
            password   TEXT    NOT NULL,
            avatar     TEXT,
            created_at TEXT    DEFAULT CURRENT_TIMESTAMP
        );
    )SQL", "create users")) return false;

    // ---------- 2. 资源文件表 ----------
    if (!execOrLog(R"SQL(
        CREATE TABLE IF NOT EXISTS resources (
            id               INTEGER PRIMARY KEY AUTOINCREMENT,
            filename         TEXT    NOT NULL UNIQUE,
            server_path      TEXT,
            size             INTEGER,
            uploader_user_id INTEGER,
            uploaded_at      TEXT    DEFAULT CURRENT_TIMESTAMP
        );
    )SQL", "create resources")) return false;

    // ---------- 3. 上传批次表 ----------
    // 一次上传 = 一个批次，可包含多个文件
    // description = "批次名|资源介绍" 格式
    if (!execOrLog(R"SQL(
        CREATE TABLE IF NOT EXISTS upload_sessions (
            id          INTEGER PRIMARY KEY AUTOINCREMENT,
            user_id     INTEGER NOT NULL,
            tags        TEXT    DEFAULT '',
            description TEXT    DEFAULT '',
            file_count  INTEGER DEFAULT 0,
            created_at  TEXT    DEFAULT CURRENT_TIMESTAMP
        );
    )SQL", "create upload_sessions")) return false;

    // ---------- 4. 上传记录表 ----------
    // 记录每个文件属于哪个批次
    // session_id 为 NULL 表示旧数据（单文件上传，未建批次）
    if (!execOrLog(R"SQL(
        CREATE TABLE IF NOT EXISTS uploads (
            id          INTEGER PRIMARY KEY AUTOINCREMENT,
            user_id     INTEGER NOT NULL,
            resource_id INTEGER NOT NULL,
            session_id  INTEGER,
            created_at  TEXT    DEFAULT CURRENT_TIMESTAMP
        );
    )SQL", "create uploads")) return false;

    // ---------- 5. 收藏表 ----------
    // is_active: 1=已收藏 0=已取消(软删除)
    if (!execOrLog(R"SQL(
        CREATE TABLE IF NOT EXISTS favorites (
            id          INTEGER PRIMARY KEY AUTOINCREMENT,
            user_id     INTEGER NOT NULL,
            resource_id INTEGER NOT NULL,
            is_active   INTEGER DEFAULT 1,
            created_at  TEXT    DEFAULT CURRENT_TIMESTAMP,
            updated_at  TEXT    DEFAULT CURRENT_TIMESTAMP,
            UNIQUE(user_id, resource_id)
        );
    )SQL", "create favorites")) return false;

    // ---------- 6. 评论表 ----------
    if (!execOrLog(R"SQL(
        CREATE TABLE IF NOT EXISTS comments (
            id            INTEGER PRIMARY KEY AUTOINCREMENT,
            resource_name TEXT    NOT NULL,
            user_id       INTEGER NOT NULL,
            content       TEXT    NOT NULL,
            created_at    TEXT    DEFAULT CURRENT_TIMESTAMP
        );
    )SQL", "create comments")) return false;

    return true;
}