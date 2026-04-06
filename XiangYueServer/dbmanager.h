#ifndef DBMANAGER_H
#define DBMANAGER_H

#include <QSqlDatabase>
#include <QString>

//DBManager：负责“打开数据库 + 初始化表结构”
//SQLite 是单个文件数据库，setDatabaseName() 传文件路径
class DBManager
{
public:
    //使用单例模式，外部通过 DBManager::instance() 获取实例
    static DBManager& instance();

    //打开数据库文件
    bool open(const QString &dbFilePath); //"xiangyue.db"

    //初始化数据库结构：建表 + 建索引
    //IF NOT EXISTS 保证重复调用也安全
    bool initSchema();

    //给业务层（登录/收藏/上传记录）拿到连接对象使用
    QSqlDatabase db() const;

    // 发生错误时，外部可显示到 UI
    QString lastErrorText() const;

    QSqlDatabase openForCurrentThread(const QString &dbFilePath);
private:
    DBManager();                 //单例：外部不可直接 new

    // 内部工具：执行 SQL，失败时记录 lastError 并打印（方便你调试）
    bool execOrLog(const QString &sql, const char *tag);
private:
    QSqlDatabase m_db;           //Qt 数据库连接对象
    QString m_lastError;  // 保存最近一次错误文本
};

#endif // DBMANAGER_H