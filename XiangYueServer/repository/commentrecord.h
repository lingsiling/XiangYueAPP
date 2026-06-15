// commentrecord.h
#ifndef COMMENTRECORD_H
#define COMMENTRECORD_H

#include <QString>

//CommentRecord：数据库查询出来的一条评论记录（带用户名，便于直接返回给客户端）
struct CommentRecord
{
    qint64 id = 0;
    QString resourceName;
    qint64 userId = 0;
    QString username;
    QString createdAt;  //直接用 SQLite 的 created_at 文本
    QString content;    //原文（允许换行）
};

#endif // COMMENTRECORD_H