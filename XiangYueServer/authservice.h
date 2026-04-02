#ifndef AUTHSERVICE_H
#define AUTHSERVICE_H

#include "userrepository.h"
#include <QString>

/*
 * AuthService：认证业务层（注册/登录）
 * - 负责：校验输入、生成 salt、计算 hash、决定错误码
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

private:
    // 生成 salt（简化实现：uuid 截取）
    QString genSalt() const;

    // sha256(salt + password) -> 64 hex chars
    //QString hashPassword(const QString &salt, const QString &password) const;

private:
    UserRepository m_repo;
};

#endif // AUTHSERVICE_H