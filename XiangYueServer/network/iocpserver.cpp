// iocpserver.cpp — 原生 Windows IOCP 服务器实现
//
// 包含顺序：Winsock2 / ws2tcpip 必须在 windows.h 之前
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>   // CreateIoCompletionPort / GetQueuedCompletionStatus / CONTAINING_RECORD 等

#include "iocontext.h"
#include "connection.h"
#include "iocpserver.h"
#include "serverconfig.h"
#include "threadpool.h"

#include <QDebug>
#include <thread>

namespace {
// 把一个完成事件分发给对应连接处理。不依赖 IocpServer 成员，故作为文件级函数。
void dispatchCompletion(BOOL ok, DWORD bytes, LPOVERLAPPED ov)
{
    // 用 OVERLAPPED 指针还原出完整的 IoContext（OVERLAPPED 是其首成员）
    IoContext *io = CONTAINING_RECORD(ov, IoContext, overlapped);

    // 先把强引用复制到局部：保证整个处理期间连接存活，
    // 即使处理函数内部 delete io / 从注册表注销，本局部引用仍兜住对象。
    std::shared_ptr<Connection> conn = io->conn;
    const IoOp op = io->op;

    if (!conn) {     // 理论上不会发生
        delete io;
        return;
    }

    switch (op) {
    case IoOp::Recv:
        if (ok && bytes > 0)
            conn->onRecvCompleted(io, static_cast<quint32>(bytes));
        else
            conn->onRecvFailed(io);   // bytes==0：对端优雅关闭；或 recv 出错
        break;
    case IoOp::Send:
        if (ok)
            conn->onSendCompleted(io, static_cast<quint32>(bytes));
        else
            conn->onSendFailed(io);
        break;
    case IoOp::SendKick:
        conn->onSendKick(io);
        break;
    }
    // conn 局部引用在此析构；若它是最后一个引用，连接对象就在本 IOCP 线程内销毁。
}
} // namespace

IocpServer::IocpServer(QObject *parent)
    : QObject(parent)
    , m_listenSock(static_cast<std::uintptr_t>(INVALID_SOCKET))
{
}

IocpServer::~IocpServer()
{
    stopServer();
}

bool IocpServer::startServer(quint16 port, int ioThreadCount)
{
    if (m_running.load()) {
        emitLog(QStringLiteral("[IOCP] 服务器已在运行，忽略重复启动"));
        return false;
    }
    if (port == 0)
        port = ServerConfig::listenPort();

    // 1) 初始化 Winsock
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        emitLog(QStringLiteral("[IOCP] WSAStartup 失败"));
        return false;
    }
    m_wsaStarted = true;

    // 2) 创建完成端口（此时不关联任何 socket；并发线程数传 0 = 由系统取 CPU 核心数）
    m_iocp = CreateIoCompletionPort(INVALID_HANDLE_VALUE, nullptr, 0, 0);
    if (m_iocp == nullptr) {
        emitLog(QString("[IOCP] CreateIoCompletionPort 失败, err=%1").arg(GetLastError()));
        closeWinsock();
        return false;
    }

    // 3) 创建监听 socket（重叠模式）
    SOCKET listenSock = WSASocketW(AF_INET, SOCK_STREAM, IPPROTO_TCP,
                                   nullptr, 0, WSA_FLAG_OVERLAPPED);
    if (listenSock == INVALID_SOCKET) {
        emitLog(QString("[IOCP] 创建监听 socket 失败, err=%1").arg(WSAGetLastError()));
        closeWinsock();
        return false;
    }
    m_listenSock = static_cast<std::uintptr_t>(listenSock);

    // 允许地址快速重用（重启服务器时不必等 TIME_WAIT）
    BOOL opt = TRUE;
    setsockopt(listenSock, SOL_SOCKET, SO_REUSEADDR,
               reinterpret_cast<const char *>(&opt), sizeof(opt));

    sockaddr_in addr;
    ZeroMemory(&addr, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(port);

    if (bind(listenSock, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) == SOCKET_ERROR) {
        emitLog(QString("[IOCP] bind 端口 %1 失败, err=%2").arg(port).arg(WSAGetLastError()));
        closeWinsock();
        return false;
    }
    if (listen(listenSock, SOMAXCONN) == SOCKET_ERROR) {
        emitLog(QString("[IOCP] listen 失败, err=%1").arg(WSAGetLastError()));
        closeWinsock();
        return false;
    }

    // 4) 确定 IOCP 工作线程数（默认 = CPU 核心数）
    if (ioThreadCount <= 0) {
        unsigned hc = std::thread::hardware_concurrency();
        ioThreadCount = (hc >= 2) ? static_cast<int>(hc) : 2;
    }
    m_ioThreadCount = ioThreadCount;

    m_running.store(true);

    // 5) 启动 IOCP 工作线程
    m_ioThreads.reserve(m_ioThreadCount);
    for (int i = 0; i < m_ioThreadCount; ++i)
        m_ioThreads.emplace_back([this]() { ioWorkerLoop(); });

    // 6) 启动监听线程
    m_acceptor = std::thread([this]() { acceptorLoop(); });

    emitLog(QString("[IOCP] 服务器启动成功：端口 %1，IOCP 工作线程 %2 个")
                .arg(port).arg(m_ioThreadCount));
    emitLog(QStringLiteral("[IOCP] 架构：监听线程Accept → IOCP线程(I/O) → 线程池(业务)"));
    return true;
}

void IocpServer::acceptorLoop()
{
    SOCKET listenSock = static_cast<SOCKET>(m_listenSock);
    emitLog(QStringLiteral("[IOCP] 监听线程已启动"));

    while (m_running.load()) {
        sockaddr_in addr;
        int addrLen = sizeof(addr);
        SOCKET client = accept(listenSock, reinterpret_cast<sockaddr *>(&addr), &addrLen);

        if (client == INVALID_SOCKET) {
            if (!m_running.load())
                break;   // 正常关停：监听 socket 已被 stopServer 关闭
            int err = WSAGetLastError();
            // 监听 socket 被关闭会导致 accept 失败，这通常意味着关停
            if (err == WSAENOTSOCK || err == WSAEINVAL || err == WSAEINTR)
                break;
            emitLog(QString("[IOCP] accept 失败, err=%1").arg(err));
            continue;
        }

        // 新连接：创建 Connection → 登记 → start（关联完成端口 + 投递首个 WSARecv）
        auto conn = Connection::create(static_cast<std::uintptr_t>(client), m_iocp, this);
        addConnection(conn);
        if (!conn->start()) {
            removeConnection(conn);   // 初始化失败：注销（conn 析构时 closesocket）
            emitLog(QStringLiteral("[IOCP] 新连接初始化失败，已丢弃"));
            continue;
        }

        char ipbuf[64] = {0};
        inet_ntop(AF_INET, &addr.sin_addr, ipbuf, sizeof(ipbuf));
        emitLog(QString("[IOCP] 新连接 %1:%2，当前连接数 %3")
                    .arg(ipbuf).arg(ntohs(addr.sin_port)).arg(totalConnections()));
    }

    emitLog(QStringLiteral("[IOCP] 监听线程已退出"));
}

void IocpServer::ioWorkerLoop()
{
    HANDLE iocp = static_cast<HANDLE>(m_iocp);

    for (;;) {
        DWORD bytes = 0;
        ULONG_PTR key = 0;
        LPOVERLAPPED ov = nullptr;
        BOOL ok = GetQueuedCompletionStatus(iocp, &bytes, &key, &ov, INFINITE);

        if (ov == nullptr) {
            // lpOverlapped 为空：不是某个 I/O 的完成。
            //   - 关停时：这是我们投递的关停哨兵 → 排空剩余完成、链式唤醒其它线程后退出。
            //   - 运行中：极少见的异常完成，记录后继续。
            if (!m_running.load()) {
                drainRemainingCompletions();
                PostQueuedCompletionStatus(iocp, 0, 0, nullptr);  // 链式：保证每个线程都能收到哨兵
                break;
            }
            continue;
        }

        dispatchCompletion(ok, bytes, ov);
    }
}

void IocpServer::drainRemainingCompletions()
{
    // 关停阶段：用 0 超时把完成队列里剩余的真实完成事件全部取出处理，
    // 确保所有在途 I/O 的 IoContext 都被回收（连接对象随之释放），避免泄漏。
    HANDLE iocp = static_cast<HANDLE>(m_iocp);
    for (;;) {
        DWORD bytes = 0;
        ULONG_PTR key = 0;
        LPOVERLAPPED ov = nullptr;
        BOOL ok = GetQueuedCompletionStatus(iocp, &bytes, &key, &ov, 0);
        if (ov == nullptr)
            break;   // WAIT_TIMEOUT（无更多完成）或又一个哨兵 → 结束排空
        dispatchCompletion(ok, bytes, ov);
    }
}

void IocpServer::addConnection(const std::shared_ptr<Connection> &conn)
{
    std::lock_guard<std::mutex> lk(m_connMutex);
    m_connections.insert(conn);
}

void IocpServer::removeConnection(const std::shared_ptr<Connection> &conn)
{
    std::lock_guard<std::mutex> lk(m_connMutex);
    m_connections.erase(conn);
}

int IocpServer::totalConnections() const
{
    std::lock_guard<std::mutex> lk(m_connMutex);
    return static_cast<int>(m_connections.size());
}

void IocpServer::closeAllConnections()
{
    // 先快照再遍历：close() 内部会回调 removeConnection() 修改集合，遍历原集合会迭代器失效
    std::vector<std::shared_ptr<Connection>> snapshot;
    {
        std::lock_guard<std::mutex> lk(m_connMutex);
        snapshot.assign(m_connections.begin(), m_connections.end());
    }
    for (auto &c : snapshot)
        c->close();
}

void IocpServer::stopServer()
{
    if (!m_running.exchange(false)) {
        // 未在运行：可能是 startServer 中途失败遗留的资源，仍做一次清理
        closeWinsock();
        return;
    }

    emitLog(QStringLiteral("[IOCP] 正在停止服务器..."));

    // 1) 关闭监听 socket → 唤醒阻塞中的 accept()，监听线程退出
    SOCKET listenSock = static_cast<SOCKET>(m_listenSock);
    if (listenSock != INVALID_SOCKET) {
        ::closesocket(listenSock);
        m_listenSock = static_cast<std::uintptr_t>(INVALID_SOCKET);
    }
    if (m_acceptor.joinable())
        m_acceptor.join();

    // 2) 关闭所有连接（置 closed + closesocket；在途 I/O 会以错误完成，交由 IOCP 线程回收）
    closeAllConnections();

    // 3) 给每个 IOCP 线程投递关停哨兵，然后等待它们退出（退出前会排空剩余完成）
    HANDLE iocp = static_cast<HANDLE>(m_iocp);
    if (iocp) {
        for (int i = 0; i < m_ioThreadCount; ++i)
            PostQueuedCompletionStatus(iocp, 0, 0, nullptr);
    }
    for (auto &t : m_ioThreads) {
        if (t.joinable())
            t.join();
    }
    m_ioThreads.clear();

    // 4) 停止业务线程池（此后不会再有业务任务调用 postSend，从而不会再有 Winsock 调用）
    ThreadPool::instance().shutdown();

    // 5) 清空注册表（释放最后一批连接强引用）
    {
        std::lock_guard<std::mutex> lk(m_connMutex);
        m_connections.clear();
    }

    // 6) 关闭完成端口、释放 Winsock
    closeWinsock();

    emitLog(QStringLiteral("[IOCP] 服务器已完全停止"));
}

void IocpServer::closeWinsock()
{
    SOCKET listenSock = static_cast<SOCKET>(m_listenSock);
    if (listenSock != INVALID_SOCKET) {
        ::closesocket(listenSock);
        m_listenSock = static_cast<std::uintptr_t>(INVALID_SOCKET);
    }
    if (m_iocp) {
        ::CloseHandle(static_cast<HANDLE>(m_iocp));
        m_iocp = nullptr;
    }
    if (m_wsaStarted) {
        ::WSACleanup();
        m_wsaStarted = false;
    }
}

void IocpServer::emitLog(const QString &msg)
{
    qDebug().noquote() << msg;
    emit logMessage(msg);   // 跨线程 → 队列连接 → GUI 线程显示
}
