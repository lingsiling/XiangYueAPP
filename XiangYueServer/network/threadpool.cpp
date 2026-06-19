// threadpool.cpp — 业务线程池实现
#include "threadpool.h"
#include "dbconnectionpool.h"

#include <QThread>
#include <QDebug>
#include <exception>

ThreadPool& ThreadPool::instance()
{
    static ThreadPool inst;
    return inst;
}

ThreadPool::~ThreadPool()
{
    // 兜底：若调用方忘了 shutdown，这里确保线程被回收，避免进程退出时崩溃
    shutdown();
}

void ThreadPool::initialize(int threadCount)
{
    if (m_running.load()) {
        qWarning() << "[ThreadPool] 线程池已初始化，跳过重复初始化";
        return;
    }

    // 默认 worker 数 = CPU 核心数 + 1（个别任务阻塞于磁盘 I/O 时仍保留吞吐）
    if (threadCount <= 0) {
        threadCount = QThread::idealThreadCount() + 1;
        if (threadCount < 2)
            threadCount = 2;
    }
    m_threadCount = threadCount;

    m_running.store(true);

    // 启动 worker 线程
    m_workers.reserve(m_threadCount);
    for (int i = 0; i < m_threadCount; ++i) {
        m_workers.emplace_back([this]() { workerLoop(); });
    }

    qDebug() << "[ThreadPool] 业务线程池启动，worker 数:" << m_threadCount;
}

void ThreadPool::submit(std::function<void()> task,
                        TaskQueue::Priority priority,
                        const QString &desc)
{
    if (!m_running.load()) {
        qWarning() << "[ThreadPool] 线程池未运行，任务被丢弃:" << desc;
        return;
    }
    m_queue.enqueue(std::move(task), priority, desc);
}

void ThreadPool::workerLoop()
{
    // worker 主循环：阻塞取任务 → 执行 → 再取，直到队列停止且排空
    while (true) {
        TaskQueue::Task task;
        if (!m_queue.dequeue(task)) {
            // 队列已停止且无剩余任务 → 退出
            break;
        }

        if (task.handler) {
            // 业务里可能抛异常（极少见），必须吞掉以免整个 worker 线程崩溃
            try {
                task.handler();
            } catch (const std::exception &e) {
                qWarning() << "[ThreadPool] 任务异常:" << task.description << e.what();
            } catch (...) {
                qWarning() << "[ThreadPool] 任务未知异常:" << task.description;
            }
        }
    }

    // 退出前关闭本线程的 SQLite 连接（必须在持有连接的线程内关闭才安全）
    DBConnectionPool::instance().releaseConnection();
}

void ThreadPool::shutdown()
{
    // 仅在运行中执行一次真正的停机
    bool expected = true;
    if (!m_running.compare_exchange_strong(expected, false)) {
        return;
    }

    qDebug() << "[ThreadPool] 正在停止线程池...";

    // 停止队列：唤醒所有阻塞中的 worker（剩余任务会被排空）
    m_queue.stop();

    // 等待所有 worker 退出
    for (std::thread &t : m_workers) {
        if (t.joinable())
            t.join();
    }
    m_workers.clear();

    qDebug() << "[ThreadPool] 线程池已停止";
}
