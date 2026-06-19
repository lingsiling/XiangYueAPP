// taskqueue.cpp — 全局任务队列实现
#include "taskqueue.h"
#include <QDebug>

void TaskQueue::enqueue(std::function<void()> handler,
                        Priority priority,
                        const QString &desc)
{
    if (!handler) {
        qWarning() << "[TaskQueue] 任务处理函数为空，拒绝入队";
        return;
    }

    {
        std::lock_guard<std::mutex> lock(m_mutex);

        // 已停止则不再接收新任务（避免停机过程中还往里塞）
        if (m_stopped) {
            qWarning() << "[TaskQueue] 队列已停止，拒绝入队:" << desc;
            return;
        }

        Task task;
        task.priority = priority;
        task.handler = std::move(handler);
        task.description = desc;
        task.seq = m_seqCounter++;   // 记录入队顺序，保证同优先级 FIFO

        m_queue.push(std::move(task));
    }

    // 唤醒一个等待的 worker 来处理（在锁外通知，减少惊群后的锁竞争）
    m_cv.notify_one();
}

bool TaskQueue::dequeue(Task &out)
{
    std::unique_lock<std::mutex> lock(m_mutex);

    // 阻塞等待：直到"有任务"或"已停止"
    m_cv.wait(lock, [this]() {
        return !m_queue.empty() || m_stopped;
    });

    // 被 stop() 唤醒且确实没有任务了 → 返回 false，worker 据此退出循环
    if (m_queue.empty()) {
        return false;
    }

    // priority_queue 的 top() 即"最该先执行"的任务；取出后必须 pop
    out = m_queue.top();
    m_queue.pop();
    return true;
}

void TaskQueue::stop()
{
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_stopped = true;
    }
    // 唤醒所有 worker：有剩余任务的继续取走执行，没任务的 dequeue 返回 false 后退出
    m_cv.notify_all();
}

int TaskQueue::size() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return static_cast<int>(m_queue.size());
}

bool TaskQueue::isStopped() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_stopped;
}
