#ifndef USERSESSION_H
#define USERSESSION_H

#include <QString>

// UserSession：登录成功后“当前用户信息”
// - 纯数据对象（不含网络、不含UI）
// - 用于在 LogDialog -> MainWindow 之间传递信息，降低耦合
struct UserSession
{
    qint64 userId = 0;
    QString username;
    QString avatar; // 服务端返回的头像路径（先可不使用）
};

#endif // USERSESSION_H