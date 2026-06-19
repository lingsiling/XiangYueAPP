// serverconfig.h — 服务端运行时配置（集中管理路径与端口）
#ifndef SERVERCONFIG_H
#define SERVERCONFIG_H

#include <QString>

/*
 * ServerConfig：服务端全局配置（目录、数据库、端口）
 *
 * 设计目的：
 *   把原先散落在 ClientWorker / ServerWidget 里的硬编码绝对路径
 *   （"D:/Qt/Projects/XiangYueAPP/..."）集中到这一处。
 *   这样：
 *     - 路径只需改一处，不会出现多处不一致；
 *     - 业务任务（运行在线程池）可以直接取常量，无需依赖 ClientWorker 成员；
 *     - 后续要做成"可配置/相对路径"也只动这一个文件。
 *
 * 用法：
 *   const QString dir = ServerConfig::saveDir();
 *
 * 说明：
 *   这里仍保留原工程使用的绝对路径，保证行为与重构前完全一致，
 *   不引入"路径变更"这类与本次 IOCP 重构无关的风险。
 */
namespace ServerConfig {

// 资源文件根目录（上传的文件按 user_<id>/session_<id>/ 存放于此）
inline QString saveDir()    { return QStringLiteral("D:/Qt/Projects/XiangYueAPP/ServerSave/"); }

// 资源目录（与 saveDir 相同，仅语义上区分"资源根"）
inline QString resourceDir() { return saveDir(); }

// SQLite 数据库文件路径
inline QString dbPath()     { return QStringLiteral("D:/Qt/Projects/XiangYueAPP/database/xiangyue.db"); }

// 数据库所在目录（启动时需 mkpath 确保存在）
inline QString dbDir()      { return QStringLiteral("D:/Qt/Projects/XiangYueAPP/database"); }

// 用户头像目录
inline QString avatarDir()  { return QStringLiteral("D:/Qt/Projects/XiangYueAPP/ServerAvatars/"); }

// 监听端口
inline quint16 listenPort() { return 7777; }

} // namespace ServerConfig

#endif // SERVERCONFIG_H
