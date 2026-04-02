#include "authservice.h"

#include <QCryptographicHash>
#include <QUuid>

QString AuthService::genSalt() const
{
    return QUuid::createUuid().toString(QUuid::WithoutBraces).left(16);
}

// 计算 hash,暂时不用 直接存明文密码（后续做安全加固时再改）
// QString AuthService::hashPassword(const QString &salt, const QString &password) const
// {
//     QByteArray h = QCryptographicHash::hash((salt + password).toUtf8(),
//                                             QCryptographicHash::Sha256).toHex();
//     return QString::fromUtf8(h);
// }

AuthService::RegisterResult AuthService::registerUser(const QString &username, const QString &password)
{
    RegisterResult r;

    const QString u = username.trimmed();
    if (u.isEmpty() || password.isEmpty()) {
        r.ok = false;
        r.reason = "INVALID_FORMAT";
        return r;
    }

    // 如果已存在，直接返回
    if (m_repo.findByUsername(u).has_value()) {
        r.ok = false;
        r.reason = "USER_EXISTS";
        return r;
    }

    const QString salt = "";//genSalt();
    const QString hash = password;//hashPassword(salt, password);

    // 默认头像（先只存路径字符串；文件下载显示后面再做）
    const QString avatar = "avatars/default.png";

    if (!m_repo.insertUser(u, hash, salt, avatar)) {
        r.ok = false;
        r.reason = "SERVER_ERROR";
        return r;
    }

    r.ok = true;
    return r;
}

AuthService::LoginResult AuthService::login(const QString &username, const QString &password)
{
    LoginResult r;

    const QString u = username.trimmed();
    if (u.isEmpty() || password.isEmpty()) {
        r.ok = false;
        r.reason = "INVALID_FORMAT";
        return r;
    }

    auto recOpt = m_repo.findByUsername(u);
    if (!recOpt.has_value()) {
        r.ok = false;
        r.reason = "USER_NOT_FOUND";
        return r;
    }

    const UserRecord &rec = *recOpt;
    //const QString inputHash = hashPassword(rec.salt, password);

    if (password != rec.passwordHash) {
        r.ok = false;
        r.reason = "WRONG_PASSWORD";
        return r;
    }

    r.ok = true;
    r.userId = rec.id;
    r.username = rec.username;
    r.avatar = rec.avatar;
    return r;
}