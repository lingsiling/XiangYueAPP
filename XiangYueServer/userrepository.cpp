#include "userrepository.h"
#include "dbmanager.h"

#include <QSqlQuery>
#include <QSqlError>
#include <QVariant>
#include <QDebug>

std::optional<UserRecord> UserRepository::findByUsername(const QString &username)
{
    QSqlQuery q(DBManager::instance().db());
    q.prepare("SELECT id, username, password_hash, salt, avatar FROM users WHERE username=?");
    q.addBindValue(username);

    if (!q.exec()) {
        qDebug() << "[UserRepo] findByUsername exec failed:" << q.lastError().text();
        return std::nullopt;
    }

    if (!q.next())
        return std::nullopt;

    UserRecord u;
    u.id = q.value(0).toLongLong();
    u.username = q.value(1).toString();
    u.passwordHash = q.value(2).toString();
    u.salt = q.value(3).toString();
    u.avatar = q.value(4).toString();
    return u;
}

bool UserRepository::insertUser(const QString &username,
                                const QString &passwordHash,
                                const QString &salt,
                                const QString &avatar)
{
    QSqlQuery q(DBManager::instance().db());
    q.prepare("INSERT INTO users(username, password_hash, salt, avatar) VALUES(?,?,?,?)");
    q.addBindValue(username);
    q.addBindValue(passwordHash);
    q.addBindValue(salt);
    q.addBindValue(avatar);

    if (!q.exec()) {
        // 常见原因：username UNIQUE 冲突
        qDebug() << "[UserRepo] insertUser failed:" << q.lastError().text();
        return false;
    }
    return true;
}