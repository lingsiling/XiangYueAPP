#ifndef AUTHSERVICE_H
#define AUTHSERVICE_H

#include "userrepository.h"
#include <QString>

/*
 * AuthService：认证业务层（注册/登录）
 * - 负责：校验输入、决定错误码
 * - 密码直接明文存储
 * - 不负责：TCP 收发/拆包（那是 Controller 的职责）
 */
class AuthService
{
public:
    struct RegisterResult {
        bool ok = false;
        QString reason; // ok==false 时填：USER_EXISTS / INVALID_FORMAT / SERVER_ERROR
    };

    struct LoginResult {
        bool ok = false;
        QString reason; // ok==false 时填：USER_NOT_FOUND / WRONG_PASSWORD / INVALID_FORMAT / SERVER_ERROR
        qint64 userId = 0;
        QString username;
        QString avatar;
    };

    RegisterResult registerUser(const QString &username, const QString &password);
    LoginResult login(const QString &username, const QString &password);

    // 按用户ID查询用户信息（代替 ClientWorker 直接调 UserRepository）
    std::optional<UserRecord> findUserById(qint64 userId);

private:
    UserRepository m_repo;
};

#endif // AUTHSERVICE_H