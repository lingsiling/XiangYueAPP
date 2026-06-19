// iocpserver.h — 原生 Windows IOCP 服务器（对外接口 Winsock-free）
#ifndef IOCPSERVER_H
#define IOCPSERVER_H

#include <QObject>
#include <memory>
#include <thread>
#include <vector>
#include <unordered_set>
#include <mutex>
#include <atomic>
#include <cstdint>

class Connection;

/*
 * IocpServer：基于 Windows I/O 完成端口（IOCP）的高并发 TCP 服务器。
 *
 * 线程模型（三级）：
 *   1) 监听线程 acceptor：阻塞 accept() 循环。每来一个连接就创建 Connection、
 *      关联到完成端口、投递首个 WSARecv。它扮演"主线程负责监听所有连接"的角色——
 *      在 GUI 程序里真正的主线程要跑 Qt 事件循环、不能阻塞，故用一个专职监听线程。
 *   2) IOCP 工作线程（数量 = CPU 核心数）：GetQueuedCompletionStatus 取完成事件，
 *      处理 recv/send 完成、发起后续 WSARecv/WSASend，并把业务投递到线程池。
 *   3) 业务线程池（ThreadPool）：只跑业务（查库/读文件），绝不碰 socket。
 *
 * 连接生命周期：所有 Connection 由 shared_ptr 持有，登记在 m_connections。
 *   断开/出错时 Connection::close() 会回调 removeConnection() 注销自己。
 *
 * 与 UI 的交互：本类是 QObject，仅通过 logMessage() 信号把日志送回 GUI 线程
 *   （跨线程自动走队列连接），自身不直接操作任何界面控件。
 */
class IocpServer : public QObject
{
    Q_OBJECT
public:
    explicit IocpServer(QObject *parent = nullptr);
    ~IocpServer() override;

    /*
     * 启动服务器。
     * port           监听端口；传 0 则用 ServerConfig::listenPort()
     * ioThreadCount  IOCP 工作线程数；<=0 则取 CPU 核心数
     * 成功 true
     */
    bool startServer(quint16 port = 0, int ioThreadCount = -1);

    // 停止服务器（优雅关闭：停监听 → 关所有连接 → 退 IOCP 线程 → 停线程池 → 释放 Winsock）
    void stopServer();

    // 当前连接数（线程安全）
    int totalConnections() const;

    // 当前 IOCP 工作线程数
    int ioThreadCount() const { return m_ioThreadCount; }

    // 由 Connection::close() 调用：把连接从注册表注销（线程安全）
    void removeConnection(const std::shared_ptr<Connection> &conn);

signals:
    // 运行日志（在工作线程发出，经队列连接送达 GUI 线程显示）
    void logMessage(const QString &msg);

private:
    void acceptorLoop();                 // 监听线程主体
    void ioWorkerLoop();                 // IOCP 工作线程主体
    void drainRemainingCompletions();    // 关停时排空剩余完成事件，回收在途 IoContext
    void addConnection(const std::shared_ptr<Connection> &conn);
    void closeAllConnections();
    void closeWinsock();                 // 关闭监听 socket / 完成端口 / WSACleanup（幂等）
    void emitLog(const QString &msg);    // 线程安全发日志

    std::uintptr_t m_listenSock;         // 实为 SOCKET
    void *m_iocp = nullptr;     // 实为 HANDLE（完成端口）
    int m_ioThreadCount = 0;
    bool m_wsaStarted = false;

    std::thread m_acceptor;
    std::vector<std::thread> m_ioThreads;
    std::atomic<bool> m_running{false};

    mutable std::mutex m_connMutex;
    std::unordered_set<std::shared_ptr<Connection>> m_connections;  // 连接注册表
};

#endif // IOCPSERVER_H
