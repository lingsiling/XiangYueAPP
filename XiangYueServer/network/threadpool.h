#ifndef THREADPOOL_H
#define THREADPOOL_H

#include "taskqueue.h"
#include <thread>
#include <vector>
#include <atomic>
#include <functional>

/*
 * ThreadPool：业务线程池（"任务队列 + 线程池"模型里的【线程池】部分）
 *
 * 职责：
 *   - 启动固定数量的 worker 线程，每个 worker 阻塞地从全局 TaskQueue 取任务并执行。
 *   - 对外只暴露 submit()（投递业务闭包），调用方完全不需要关心线程细节。
 *
 * 在整体架构里的位置：
 *   IOCP 工作线程（只做 I/O）解析出业务请求后 → Connection::post() → 最终 submit()
 *   到本线程池 → worker 执行 Qt service（查库/读文件）→ 产出响应字节 →
 *   conn->postSend() 交回网络层发送。
 *   ★ worker 线程绝不调用任何 socket API ★（发送由 IOCP 线程的 WSASend 完成）
 *
 * 与旧实现的区别：
 *   旧实现是 QThreadPool 包装 + 每连接一个自旋的 processQueue → 连接一多就饿死。
 *   新实现是"固定 worker 数 + 共享阻塞队列"，连接数与线程数解耦，空闲不占 CPU。
 *
 * 数据库：
 *   每个 worker 第一次执行查库任务时，DBConnectionPool 会惰性地为该线程创建
 *   独立的 SQLite 连接（thread_local，天然线程隔离）。worker 退出前主动关闭，
 *   避免进程结束时 QtSql 跨线程析构告警。
 */
class ThreadPool
{
public:
    // 全局唯一实例
    static ThreadPool& instance();

    /*
     * 初始化并启动线程池。
     * threadCount  worker 数量；<=0 时按 CPU 核心数 + 1 自动取值
     *                     （+1 是为了在某个任务偶尔阻塞于磁盘 I/O 时仍有吞吐）
     * 重复调用会被忽略。
     */
    void initialize(int threadCount = -1);

    /*
     * 投递一个业务任务。线程安全，可从任意线程调用。
     * task      业务闭包
     * priority  优先级（登录/注册等可用 HIGH）
     * desc      调试描述
     */
    void submit(std::function<void()> task,
                TaskQueue::Priority priority = TaskQueue::NORMAL,
                const QString &desc = QString());

    /*
     * 停止线程池：停止队列 → 唤醒并 join 所有 worker。
     * 已入队任务会被排空执行完，之后 worker 退出。可安全多次调用。
     */
    void shutdown();

    int maxThreadCount() const { return m_threadCount; }
    int pendingTasks() const { return m_queue.size(); }
    bool isRunning() const { return m_running.load(); }

private:
    ThreadPool() = default;
    ~ThreadPool();
    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;

    // worker 线程主循环：从队列取任务并执行，直到队列停止
    void workerLoop();

    TaskQueue m_queue;            // 全局任务队列（worker 共享）
    std::vector<std::thread> m_workers;          // worker 线程
    int m_threadCount = 0;  // worker 数量
    std::atomic<bool> m_running{false};   // 是否在运行
};

#endif // THREADPOOL_H
