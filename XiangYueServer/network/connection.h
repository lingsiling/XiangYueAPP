// connection.h — IOCP 单连接上下文（对外接口保持 Winsock-free，便于 ClientWorker 复用）
#ifndef CONNECTION_H
#define CONNECTION_H

#include <QByteArray>
#include <memory>
#include <deque>
#include <vector>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <functional>
#include <cstdint>

struct IoContext;     // 完整定义在 iocontext.h（含 Winsock），只对网络层 .cpp 可见
class IocpServer;
class ClientWorker;

/*
 * Connection：一条客户端连接在服务端的全部状态与行为。
 *
 * 在 IOCP 架构里的角色：
 *   acceptor 线程 accept 到 socket → 创建 Connection → 关联完成端口 → 投递首个 WSARecv。
 *   之后这条连接的所有 I/O 完成事件都由 IOCP 工作线程处理；业务则交给线程池。
 *
 * 三类线程会触碰 Connection，各自的边界如下：
 *   ┌── IOCP 工作线程：处理 recv/send 完成；调用协议解析器；发起 WSARecv/WSASend。
 *   │       同一连接同时只有一个在途 WSARecv，故 recv 完成被串行处理，
 *   │       协议解析器(ClientWorker)无需加锁。
 *   ├── 业务线程池：执行 post() 进来的业务闭包（查库/读文件），只通过 postSend() 交回字节，
 *   │       绝不直接调用任何 socket API。
 *   └── （acceptor 线程：仅在 start() 时投递首个 WSARecv。）
 *
 * 生命周期（引用计数）：
 *   Connection 全程由 std::shared_ptr 持有。持有者有三类：
 *     1) IocpServer 的连接注册表；2) 每个在途 I/O 的 IoContext；3) drain() 执行期间的局部强引用。
 *   关闭时置 closed + closesocket + 从注册表注销；当最后一个强引用析构，连接被销毁。
 *
 * 保序与背压：
 *   - 业务串行执行器(post/drain)：保证【同一连接】的业务任务严格按提交顺序、逐个执行，
 *     从而响应字节流与"顺序服务端"完全一致（多文件下载也不会交错损坏）。
 *   - 发送队列(postSend)：WSASend 串行化（同时只一个在途），支持部分发送续传；
 *     单连接排队字节超阈值时对生产者背压，避免慢客户端把大文件堆爆内存。
 */
class Connection : public std::enable_shared_from_this<Connection>
{
public:
    /*
     * 工厂方法：创建一条连接（尚未关联完成端口）。
     * socketHandle  已 accept 的 socket（实为 SOCKET，这里用不透明整型避免在头里引入 Winsock）
     * iocpHandle    完成端口句柄（实为 HANDLE）
     * server        所属服务器（用于断开时注销，不持有其所有权）
     */
    static std::shared_ptr<Connection> create(std::uintptr_t socketHandle,
                                              void *iocpHandle,
                                              IocpServer *server);
    ~Connection();

    Connection(const Connection&) = delete;
    Connection& operator=(const Connection&) = delete;

    /*
     * 关联到完成端口并投递第一个 WSARecv。成功返回 true。
     * 由 acceptor 线程在把连接登记进注册表后调用。
     */
    bool start();

    /*
     * 提交一个业务闭包到本连接的【串行执行器】。线程安全。
     * 闭包会在线程池中执行，且【同一连接】的闭包严格按提交顺序、逐个执行。
     * 注意：闭包内若要访问本连接，请捕获裸指针(Connection*)，不要捕获 shared_ptr，
     *       否则会与 m_taskQueue 形成循环引用（drain() 执行期间已持有强引用保活）。
     */
    void post(std::function<void()> task);

    /*
     * 把响应字节排入发送队列。线程安全，可被任意线程（含业务线程）调用。
     * 本函数只负责"入队 + 唤起发送"，真正的 WSASend 由 IOCP 线程发起。
     * 连接已关闭时静默丢弃。超过背压阈值时阻塞调用者，直到队列腾出空间或连接关闭。
     */
    void postSend(const QByteArray &data);

    // 关闭连接（幂等）。
    void close();

    // 会话用户ID：登录成功后由业务线程写入；解析线程读取（原子，避免撕裂）。
    void setUserId(qint64 uid) { m_userId.store(uid); }
    qint64 userId() const        { return m_userId.load(); }

    bool isClosed() const { return m_closed.load(); }

    // ===== 下列方法仅由 IocpServer 的 IOCP 工作线程在分发完成事件时调用 =====
    void onRecvCompleted(IoContext *io, quint32 bytes); // 收到 bytes>0 字节
    void onRecvFailed(IoContext *io);                   // recv 失败 / 对端关闭(bytes==0)
    void onSendCompleted(IoContext *io, quint32 bytes); // 一个 WSASend 完成
    void onSendFailed(IoContext *io);                   // send 失败
    void onSendKick(IoContext *io);                     // 处理"开始发送"请求

private:
    Connection(std::uintptr_t socketHandle, void *iocpHandle, IocpServer *server);

    bool postRecv(IoContext *io);          // 用 io 投递一个 WSARecv（复用 m_recvBuf）
    bool issueSend_locked(IoContext *io);  // 持 m_sendMutex：用 io 发送当前队首未发送部分
    void postSendKick_locked();            // 持 m_sendMutex：投递 SendKick 唤起 IOCP 线程发送
    void drain();                          // 串行执行器主体：按序执行业务任务直到队空

    std::uintptr_t m_sock;   // 实为 SOCKET
    void *m_iocp;   // 实为 HANDLE（完成端口，不持有）
    IocpServer *m_server; // 不持有

    std::unique_ptr<ClientWorker> m_session;  // 协议解析器（仅 IOCP recv 线程访问，无需锁）

    // —— 接收 —— （单一在途 recv ⇒ 无需加锁）
    std::vector<char> m_recvBuf;

    // —— 发送 —— （WSASend 串行化 + 部分发送续传 + 背压）
    std::mutex m_sendMutex;
    std::condition_variable m_sendCv;
    std::deque<QByteArray> m_sendQueue;
    std::size_t m_sendOffset = 0;       // 队首已发送字节数
    bool m_sendInFlight = false; // 是否有 WSASend 在途
    std::size_t m_sendQueuedBytes = 0;  // 已排队未发送的总字节（背压计量）

    // —— 业务串行执行器 ——
    std::mutex m_taskMutex;
    std::deque<std::function<void()>> m_taskQueue;
    bool m_draining = false;

    // —— 状态 ——
    std::atomic<qint64> m_userId{0};
    std::atomic<bool> m_closed{false};
};

#endif // CONNECTION_H
