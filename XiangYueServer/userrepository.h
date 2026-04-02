#ifndef USERREPOSITORY_H
#define USERREPOSITORY_H

#include <QString>
#include <optional>

/*
 * UserRecord：与 users 表字段对应的数据结构
 * Repository 层只负责“把表里的一行取出来/写进去”，不负责业务规则。
 */
struct UserRecord
{
    qint64 id = 0;
    QString username;
    QString passwordHash;
    QString salt;
    QString avatar;
};

class UserRepository
{
public:
    // 根据 username 查询用户；不存在返回 nullopt
    std::optional<UserRecord> findByUsername(const QString &username);

    // 插入新用户；成功返回 true；失败（如 username 冲突）返回 false
    bool insertUser(const QString &username,
                    const QString &passwordHash,
                    const QString &salt,
                    const QString &avatar);
};

#endif // USERREPOSITORY_H