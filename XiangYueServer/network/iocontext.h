// iocontext.h — 单次重叠 I/O 的上下文（仅网络层 .cpp 使用，含 Winsock 头）
#ifndef IOCONTEXT_H
#define IOCONTEXT_H

//      Winsock2 头必须在任何可能引入 <windows.h> 的头之前包含
//      否则 <windows.h> 会拉入旧的 winsock.h(v1) 与 winsock2.h 冲突。
//      本头只被 connection.cpp / iocpserver.cpp 包含，且它们都在最顶部先包含本头。
#include <winsock2.h>
#include <windows.h>   // OVERLAPPED / ZeroMemory / CONTAINING_RECORD / IOCP 相关 API

#include <memory>

class Connection;

/*
 * IoOp：完成事件的类型。IOCP 工作线程取到完成包后，靠它区分该做什么。
 */
enum class IoOp {
    Recv,      // 一个 WSARecv 完成（收到数据 / 对端关闭）
    Send,      // 一个 WSASend 完成
    SendKick   // 业务线程通过 PostQueuedCompletionStatus 请求"开始发送"
               // （业务线程只入队 + 投递本事件，真正的 WSASend 由 IOCP 线程发起，
               //   从而保证"线程池不碰 socket"）
};

/*
 * IoContext：一次重叠 I/O 操作的上下文。
 *
 * 关键设计：
 *   - OVERLAPPED 必须是【第一个成员】，这样在完成事件里可用
 *     CONTAINING_RECORD(lpOverlapped, IoContext, overlapped) 还原出整个结构。
 *   - conn 持有 Connection 的【强引用】：只要这次 I/O 还在途，连接对象就不会被销毁，
 *     从根本上杜绝"完成事件回来时连接已被释放"的 use-after-free。
 *   - IoContext 一律在堆上 new、用完 delete，绝不作为 Connection 的成员存放，
 *     以避免 "Connection 持有 IoContext、IoContext 又强引用 Connection" 的循环引用。
 */
struct IoContext {
    OVERLAPPED overlapped;            // 必须置零后投递；必须是首成员
    WSABUF wsabuf;                // 指向待收/待发缓冲区
    IoOp op;                    // 操作类型
    std::shared_ptr<Connection> conn;  // 在途期间持有连接，防止其被销毁

    IoContext() {
        ZeroMemory(&overlapped, sizeof(overlapped));
        wsabuf.buf = nullptr;
        wsabuf.len = 0;
        op = IoOp::Recv;
    }
};

#endif // IOCONTEXT_H
