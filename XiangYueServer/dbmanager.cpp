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
    //后续的“我的上传/收藏”会依赖这些表
    //建表顺序不强制，但按 users -> resources -> uploads -> favorites 更直观

    //users：账号（用户名/密码hash/salt/头像路径）
    if (!execOrLog(R"SQL(
        CREATE TABLE IF NOT EXISTS users (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            username TEXT NOT NULL UNIQUE,
            password_hash TEXT NOT NULL,
            salt TEXT NOT NULL,
            avatar TEXT,
            created_at TEXT DEFAULT CURRENT_TIMESTAMP
        );
    )SQL", "create users")) return false;

    //resources：资源/文件表（把服务器上的文件“入库”，便于查询某用户上传了哪些文件）
    if (!execOrLog(R"SQL(
        CREATE TABLE IF NOT EXISTS resources (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            filename TEXT NOT NULL UNIQUE,
            server_path TEXT,
            size INTEGER,
            uploader_user_id INTEGER,
            uploaded_at TEXT DEFAULT CURRENT_TIMESTAMP
        );
    )SQL", "create resources")) return false;

    //uploads：上传记录（如果只关心“每个文件当前上传者”，resources.uploader_user_id 也够用）
    //这里先建着，未来做“上传历史/统计”更方便
    if (!execOrLog(R"SQL(
        CREATE TABLE IF NOT EXISTS uploads (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            user_id INTEGER NOT NULL,
            resource_id INTEGER NOT NULL,
            created_at TEXT DEFAULT CURRENT_TIMESTAMP
        );
    )SQL", "create uploads")) return false;

    // favorites：收藏（用户-资源 多对多），UNIQUE 防止重复收藏
    if (!execOrLog(R"SQL(
        CREATE TABLE IF NOT EXISTS favorites (
            user_id INTEGER NOT NULL,
            resource_id INTEGER NOT NULL,
            created_at TEXT DEFAULT CURRENT_TIMESTAMP,
            UNIQUE(user_id, resource_id)
        );
    )SQL", "create favorites")) return false;

    //常用查询的索引：提升 “我的上传/我的收藏” 的查询速度
    if (!execOrLog(R"SQL(
        CREATE INDEX IF NOT EXISTS idx_resources_uploader
        ON resources(uploader_user_id);
    )SQL", "create idx_resources_uploader")) return false;

    if (!execOrLog(R"SQL(
        CREATE INDEX IF NOT EXISTS idx_uploads_user
        ON uploads(user_id);
    )SQL", "create idx_uploads_user")) return false;

    if (!execOrLog(R"SQL(
        CREATE INDEX IF NOT EXISTS idx_favorites_user
        ON favorites(user_id);
    )SQL", "create idx_favorites_user")) return false;

    //comments：资源评论/留言
    //- content 允许换行，直接存 TEXT
    //- resource_name 先用“文件名”作为资源标识，后续如果 resources 表稳定维护，可迁移为 resource_id
    if (!execOrLog(R"SQL(
        CREATE TABLE IF NOT EXISTS comments (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            resource_name TEXT NOT NULL,
            user_id INTEGER NOT NULL,
            content TEXT NOT NULL,
            created_at TEXT DEFAULT CURRENT_TIMESTAMP
        );
    )SQL", "create comments")) return false;

    // 常用查询索引：按资源名拉评论列表
    if (!execOrLog(R"SQL(
        CREATE INDEX IF NOT EXISTS idx_comments_resource
        ON comments(resource_name);
    )SQL", "create idx_comments_resource")) return false;

    return true;
}