#ifndef TASKQUEUE_H
#define TASKQUEUE_H

#include <QObject>
#include <QQueue>
#include <QMutex>
#include <QWaitCondition>
#include <functional>

/*
 * TaskQueue：异步任务队列管理器
 * 作用：
 *   - 提供线程安全的任务队列
 *   - 支持优先级任务处理
 *   - 解耦网络IO和业务逻辑
 *
 * 使用场景：
 *   - 将客户端连接的命令处理异步化
 *   - 避免单个耗时操作阻塞其他连接
 *   - 为IO多路复用做准备
 *
 * 设计特点：
 *   - 线程安全：使用互斥锁保护共享数据
 *   - 异步通知：任务完成后发出信号
 *   - 优先级支持：重要任务优先执行
 */
class TaskQueue : public QObject
{
    Q_OBJECT

public:
    //任务优先级枚举
    enum Priority {
        LOW = 0,        //低优先级（普通业务）
        NORMAL = 1,     //普通优先级
        HIGH = 2,       //高优先级（认证/支付等）
        CRITICAL = 3    //关键优先级（系统操作）
    };

    //任务结构体
    struct Task {
        Priority priority;
        std::function<void()> handler;
        QString description;  //任务描述（便于调试）

        bool operator<(const Task& other) const {
            //优先级高的排前面（用于优先级队列）
            return priority < other.priority;
        }
    };

    explicit TaskQueue(QObject *parent = nullptr);
    ~TaskQueue();

    //提交任务到队列
    void enqueue(std::function<void()> handler,
                 Priority priority = NORMAL,
                 const QString &desc = QString());

    //获取队列中待处理任务数
    int pendingCount() const;

    //启动处理任务（内部由线程池驱动）
    void start();
    void stop();

    bool isRunning() const { return m_isRunning; }

signals:
    //任务开始处理时发出
    void taskStarted(const QString &desc);

    //任务完成时发出
    void taskCompleted(const QString &desc);

    //任务出错时发出
    void taskError(const QString &desc, const QString &error);

private:
    void processQueue();  //处理队列中的任务

private:
    QQueue<Task> m_queue;
    mutable QMutex m_mutex;
    QWaitCondition m_waitCondition;
    bool m_isRunning;
};

#endif //TASKQUEUE_H