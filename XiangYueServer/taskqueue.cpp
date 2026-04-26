// taskqueue.cpp
#include "taskqueue.h"
#include "threadpool.h"
#include <QDebug>

TaskQueue::TaskQueue(QObject *parent)
    : QObject(parent), m_isRunning(false)
{
}

TaskQueue::~TaskQueue()
{
    stop();
}

void TaskQueue::enqueue(std::function<void()> handler,
                        Priority priority,
                        const QString &desc)
{
    if (!handler) {
        qWarning() << "[TaskQueue] 任务处理函数为空，拒绝入队";
        return;
    }

    QMutexLocker locker(&m_mutex);

    Task task;
    task.priority = priority;
    task.handler = handler;
    task.description = desc.isEmpty() ? "Unknown Task" : desc;

    m_queue.enqueue(task);

    qDebug() << "[TaskQueue] 任务入队:" << task.description
             << "优先级:" << task.priority
             << "队列长度:" << m_queue.size();

    //通知处理线程有新任务
    m_waitCondition.wakeAll();
}

int TaskQueue::pendingCount() const
{
    QMutexLocker locker(&m_mutex);
    return m_queue.size();
}

void TaskQueue::start()
{
    if (m_isRunning) {
        qDebug() << "[TaskQueue] 任务队列已启动，跳过重复启动";
        return;
    }

    m_isRunning = true;
    qDebug() << "[TaskQueue] 任务队列已启动";

    // 提交一个长期运行的任务处理循环
    ThreadPool::instance().submit([this]() {
        processQueue();
    });
}

void TaskQueue::processQueue()
{
    //注意：这个函数运行在线程池的工作线程中
    while (m_isRunning) {
        Task task;

        {
            QMutexLocker locker(&m_mutex);

            //队列为空，等待新任务或超时
            if (m_queue.isEmpty()) {
                // 等待超时（1秒）或被唤醒
                if (!m_waitCondition.wait(&m_mutex, 1000)) {
                    //超时，继续检查 m_isRunning
                    continue;
                }
            }

            //再次检查队列（可能在等待中被停止）
            if (m_queue.isEmpty()) {
                continue;
            }

            task = m_queue.dequeue();
        } //释放锁

        //执行任务（不持有锁）
        if (task.handler) {
            try {
                emit taskStarted(task.description);
                task.handler();
                emit taskCompleted(task.description);
                qDebug() << "[TaskQueue] 任务完成:" << task.description;
            }
            catch (const std::exception &e) {
                qWarning() << "[TaskQueue] 任务异常:" << task.description << e.what();
                emit taskError(task.description, QString::fromStdString(e.what()));
            }
            catch (...) {
                qWarning() << "[TaskQueue] 任务未知异常:" << task.description;
                emit taskError(task.description, "Unknown error");
            }
        }
    }

    qDebug() << "[TaskQueue] 处理循环已退出";
}

void TaskQueue::stop()
{
    m_isRunning = false;
    m_waitCondition.wakeAll();
    qDebug() << "[TaskQueue] 任务队列已停止";
}