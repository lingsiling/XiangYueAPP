// connection.cpp — IOCP 单连接上下文实现
//
// 包含顺序很重要：先 Winsock2，再其它
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>   // ZeroMemory / GetLastError / CreateIoCompletionPort / PostQueuedCompletionStatus / CloseHandle

#include "iocontext.h"
#include "connection.h"
#include "clientworker.h"
#include "threadpool.h"
#include "iocpserver.h"

#include <QDebug>

namespace {
// recv 落地缓冲大小：64KB，兼顾吞吐与内存
constexpr int kRecvBufSize = 64 * 1024;
// 单连接发送队列背压阈值：4MB。超过则阻塞生产者（业务线程），防止慢客户端撑爆内存。
constexpr std::size_t kMaxSendQueueBytes = 4 * 1024 * 1024;
} // namespace

// —— 小工具：在 .cpp 内把不透明句柄还原成 Winsock 类型 ——
static inline SOCKET sockOf(std::uintptr_t s) { return static_cast<SOCKET>(s); }
static inline HANDLE iocpOf(void *h)          { return static_cast<HANDLE>(h); }

std::shared_ptr<Connection> Connection::create(std::uintptr_t socketHandle,
                                               void *iocpHandle,
                                               IocpServer *server)
{
    // 构造函数私有，这里用 new 包装成 shared_ptr（不能用 make_shared，因为构造函数私有）
    return std::shared_ptr<Connection>(new Connection(socketHandle, iocpHandle, server));
}

Connection::Connection(std::uintptr_t socketHandle, void *iocpHandle, IocpServer *server)
    : m_sock(socketHandle)
    , m_iocp(iocpHandle)
    , m_server(server)
    , m_recvBuf(kRecvBufSize)
{
    // 协议解析器：仅持有本连接裸指针。注意此刻 shared_ptr 尚未建立，
    // ClientWorker 构造函数中不可调用 shared_from_this()（它只保存指针，无此调用）。
    m_session.reset(new ClientWorker(this));
}

Connection::~Connection()
{
    // 兜底：若从未走过 close()（例如 start() 前就被丢弃），在此关闭 socket
    if (!m_closed.exchange(true)) {
        SOCKET s = sockOf(m_sock);
        if (s != INVALID_SOCKET)
            ::closesocket(s);
    }
    qDebug() << "[Connection] 连接对象已销毁";
}

bool Connection::start()
{
    SOCKET s = sockOf(m_sock);
    HANDLE iocp = iocpOf(m_iocp);

    // 把 socket 关联到完成端口；completionKey 用 Connection*（仅便于调试，
    // 真正的"保活"靠每个 IoContext 持有的 shared_ptr）。
    if (CreateIoCompletionPort(reinterpret_cast<HANDLE>(s), iocp,
                               reinterpret_cast<ULONG_PTR>(this), 0) == nullptr) {
        qWarning() << "[Connection] 关联完成端口失败, err=" << GetLastError();
        return false;
    }

    // 投递首个 WSARecv。recv 用的 IoContext 在堆上、持有强引用，
    // 之后每次 recv 完成都复用它（避免反复分配，也避免循环引用）。
    IoContext *io = new IoContext();
    io->conn = shared_from_this();
    if (!postRecv(io)) {
        delete io;
        return false;
    }
    return true;
}

bool Connection::postRecv(IoContext *io)
{
    ZeroMemory(&io->overlapped, sizeof(OVERLAPPED));
    io->op = IoOp::Recv;
    io->wsabuf.buf = m_recvBuf.data();
    io->wsabuf.len = static_cast<ULONG>(m_recvBuf.size());

    DWORD flags = 0;
    DWORD bytes = 0;
    int rc = WSARecv(sockOf(m_sock), &io->wsabuf, 1, &bytes, &flags, &io->overlapped, nullptr);
    if (rc == SOCKET_ERROR) {
        int err = WSAGetLastError();
        if (err != WSA_IO_PENDING) {
            // WSAECONNRESET 等：连接已不可用
            return false;
        }
    }
    return true;  // 正常进入"挂起"，完成时投递到 IOCP
}

void Connection::onRecvCompleted(IoContext *io, quint32 bytes)
{
    // 单一在途 recv ⇒ 本函数对同一连接是串行的 ⇒ 访问 m_session / m_recvBuf 无需加锁。
    if (m_closed.load()) {
        delete io;
        return;
    }

    // 把收到的字节交给协议解析器（解析出的业务会被 post 到串行执行器）
    if (m_session)
        m_session->onDataReceived(m_recvBuf.data(), static_cast<int>(bytes));

    // 复用同一个 io 继续投递下一个 WSARecv
    if (m_closed.load() || !postRecv(io)) {
        delete io;
        close();
    }
}

void Connection::onRecvFailed(IoContext *io)
{
    // bytes==0（对端优雅关闭）或 recv 出错都走这里
    delete io;
    close();
}

void Connection::postSend(const QByteArray &data)
{
    if (data.isEmpty())
        return;

    std::unique_lock<std::mutex> lk(m_sendMutex);
    if (m_closed.load())
        return;

    // 背压：发送队列积压过多时阻塞调用者（业务线程），等 IOCP 线程把数据发出去腾出空间。
    // 连接关闭也会唤醒，从而安全返回（避免永久阻塞）。
    m_sendCv.wait(lk, [this]() {
        return m_sendQueuedBytes < kMaxSendQueueBytes || m_closed.load();
    });
    if (m_closed.load())
        return;

    m_sendQueue.push_back(data);
    m_sendQueuedBytes += static_cast<std::size_t>(data.size());

    // 若当前没有 WSASend 在途，唤起一次发送（交给 IOCP 线程执行真正的 WSASend）
    if (!m_sendInFlight) {
        m_sendInFlight = true;
        postSendKick_locked();
    }
}

void Connection::postSendKick_locked()
{
    // 业务线程不直接调用 WSASend（恪守"线程池不碰 socket"）。
    // 改为投递一个 SendKick 完成包，由某个 IOCP 线程取到后发起 WSASend。
    IoContext *kick = new IoContext();
    kick->op = IoOp::SendKick;
    kick->conn = shared_from_this();

    if (!PostQueuedCompletionStatus(iocpOf(m_iocp), 0,
                                    reinterpret_cast<ULONG_PTR>(this),
                                    &kick->overlapped)) {
        qWarning() << "[Connection] PostQueuedCompletionStatus 失败, err=" << GetLastError();
        delete kick;
        m_sendInFlight = false;
    }
}

void Connection::onSendKick(IoContext *io)
{
    delete io;  // kick 上下文用完即弃（其强引用已由 IOCP 循环的局部变量兜住）

    bool doClose = false;
    IoContext *sendIo = nullptr;
    {
        std::lock_guard<std::mutex> lk(m_sendMutex);
        if (m_closed.load() || m_sendQueue.empty()) {
            m_sendInFlight = false;
            return;
        }
        sendIo = new IoContext();
        sendIo->conn = shared_from_this();
        if (!issueSend_locked(sendIo))
            doClose = true;
    }
    if (doClose) {
        delete sendIo;
        close();
    }
}

bool Connection::issueSend_locked(IoContext *io)
{
    // 调用者已持 m_sendMutex 且确认队列非空。发送队首"未发送部分"。
    QByteArray &front = m_sendQueue.front();
    ZeroMemory(&io->overlapped, sizeof(OVERLAPPED));
    io->op = IoOp::Send;
    io->wsabuf.buf = const_cast<char *>(front.constData()) + m_sendOffset;
    io->wsabuf.len = static_cast<ULONG>(front.size() - m_sendOffset);

    DWORD sent = 0;
    int rc = WSASend(sockOf(m_sock), &io->wsabuf, 1, &sent, 0, &io->overlapped, nullptr);
    if (rc == SOCKET_ERROR) {
        int err = WSAGetLastError();
        if (err != WSA_IO_PENDING)
            return false;
    }
    return true;
}

void Connection::onSendCompleted(IoContext *io, quint32 bytes)
{
    bool doClose = false;
    bool deleteIo = false;
    {
        std::lock_guard<std::mutex> lk(m_sendMutex);
        if (m_sendQueue.empty()) {
            // 理论上不会发生，保险处理
            m_sendInFlight = false;
            deleteIo = true;
        } else {
            m_sendOffset += bytes;
            QByteArray &front = m_sendQueue.front();
            if (m_sendOffset >= static_cast<std::size_t>(front.size())) {
                // 队首整块发送完毕：出队、复位偏移、释放背压
                m_sendQueuedBytes -= static_cast<std::size_t>(front.size());
                m_sendQueue.pop_front();
                m_sendOffset = 0;
                m_sendCv.notify_all();
            }
            // 还有数据（下一块，或当前块的剩余部分）就继续发，复用 io
            if (!m_closed.load() && !m_sendQueue.empty()) {
                if (!issueSend_locked(io)) {
                    doClose = true;
                    deleteIo = true;
                }
            } else {
                m_sendInFlight = false;
                deleteIo = true;
            }
        }
    }
    if (deleteIo)
        delete io;
    if (doClose)
        close();
}

void Connection::onSendFailed(IoContext *io)
{
    {
        std::lock_guard<std::mutex> lk(m_sendMutex);
        m_sendInFlight = false;
        m_sendCv.notify_all();
    }
    delete io;
    close();
}

void Connection::post(std::function<void()> task)
{
    if (!task)
        return;

    bool needKick = false;
    {
        std::lock_guard<std::mutex> lk(m_taskMutex);
        if (m_closed.load())
            return;  // 已关闭，丢弃业务
        m_taskQueue.push_back(std::move(task));
        // 若当前没有 drain 作业在跑，则提交一个（保证同连接只有一个 drain 在线程池里）
        if (!m_draining) {
            m_draining = true;
            needKick = true;
        }
    }

    if (needKick) {
        // drain 作业持有自身强引用，执行期间连接不会被销毁
        auto self = shared_from_this();
        ThreadPool::instance().submit([self]() { self->drain(); },
                                      TaskQueue::NORMAL, QStringLiteral("conn-drain"));
    }
}

void Connection::drain()
{
    // 在线程池 worker 中执行：按提交顺序、逐个执行本连接的业务任务，直到队空。
    // 同一时刻同一连接只有一个 drain 在跑（由 m_draining 保证）⇒ 响应严格保序。
    for (;;) {
        std::function<void()> task;
        {
            std::lock_guard<std::mutex> lk(m_taskMutex);
            if (m_closed.load()) {
                // 连接已关闭：丢弃剩余业务，避免做无用功
                m_taskQueue.clear();
                m_draining = false;
                return;
            }
            if (m_taskQueue.empty()) {
                m_draining = false;  // 队空：让出线程（空闲连接不占用任何线程）
                return;
            }
            task = std::move(m_taskQueue.front());
            m_taskQueue.pop_front();
        }

        try {
            task();
        } catch (const std::exception &e) {
            qWarning() << "[Connection] 业务任务异常:" << e.what();
        } catch (...) {
            qWarning() << "[Connection] 业务任务未知异常";
        }
    }
}

void Connection::close()
{
    // 幂等：只有第一次调用真正执行关闭
    if (m_closed.exchange(true))
        return;

    SOCKET s = sockOf(m_sock);
    if (s != INVALID_SOCKET) {
        // 关闭 socket 会令所有在途的 WSARecv/WSASend 以错误完成，
        // 其完成事件回到 IOCP 线程后各自清理 IoContext（释放强引用）。
        ::closesocket(s);
    }

    // 唤醒可能因背压而阻塞在 postSend() 里的业务线程，让其看到 closed 后返回
    {
        std::lock_guard<std::mutex> lk(m_sendMutex);
    }
    m_sendCv.notify_all();

    // 从服务器注册表注销（释放注册表持有的那一份强引用）
    if (m_server)
        m_server->removeConnection(shared_from_this());

    qDebug() << "[Connection] 连接已关闭";
}
