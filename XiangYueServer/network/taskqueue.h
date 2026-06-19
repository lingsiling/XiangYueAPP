#ifndef TASKQUEUE_H
#define TASKQUEUE_H

#include <QString>
#include <functional>
#include <queue>
#include <vector>
#include <mutex>
#include <condition_variable>
#include <cstdint>

/*
 * TaskQueue：全局任务队列（线程安全、阻塞、支持优先级）
 *
 * ┌─────────────────────────────────────────────────────────────┐
 * │ 这是"任务队列 + 线程池"模型里的【队列】部分。                  │
 * │   - 生产者：IOCP 工作线程解析出业务后，通过 ThreadPool::submit  │
 * │     间接调用 enqueue() 投递任务。                              │
 * │   - 消费者：ThreadPool 的若干 worker 线程循环调用 dequeue()，   │
 * │     取出任务并执行。                                           │
 * └─────────────────────────────────────────────────────────────┘
 *
 * 与旧实现的本质区别（旧实现是反模式，已废弃）：
 *   旧：每个连接 new 一个 TaskQueue，并把一个 while 死循环 submit 到
 *       线程池里长期占用一个线程 → 连接数一多线程池就被占满而饿死。
 *   新：全进程只有一个 TaskQueue（由 ThreadPool 持有）。worker 线程
 *       阻塞在 dequeue() 上等任务，空闲时不占 CPU，有任务才被唤醒。
 *       连接数与线程数彻底解耦，这才是高并发该有的样子。
 *
 * 线程安全：std::mutex 保护队列，std::condition_variable 做阻塞唤醒。
 */
class TaskQueue
{
public:
    // 任务优先级：数值越大越优先出队
    enum Priority {
        LOW = 0,        // 低优先级
        NORMAL = 1,     // 普通业务（列表、评论、上传入库等）
        HIGH = 2,       // 高优先级（登录/注册等用户强感知操作）
        CRITICAL = 3    // 关键操作
    };

    // 一个待执行任务
    struct Task {
        Priority priority = NORMAL;
        std::function<void()> handler;  // 真正要执行的业务闭包
        QString description;            // 任务描述（仅用于调试日志）
        std::uint64_t seq = 0;          // 入队序号：保证"同优先级 FIFO"
    };

    TaskQueue() = default;
    ~TaskQueue() = default;

    // 禁止拷贝（持有锁/条件变量）
    TaskQueue(const TaskQueue&) = delete;
    TaskQueue& operator=(const TaskQueue&) = delete;

    /*
     * 投递一个任务（生产者调用，线程安全）。
     * @param handler   业务闭包
     * @param priority  优先级
     * @param desc      调试描述
     */
    void enqueue(std::function<void()> handler,
                 Priority priority = NORMAL,
                 const QString &desc = QString());

    /*
     * 取出一个任务（消费者调用，阻塞）。
     *   - 队列非空：立刻取出优先级最高（同优先级最早入队）的任务，返回 true。
     *   - 队列为空：阻塞等待，直到有新任务（返回 true）或被 stop() 唤醒且队列已空（返回 false）。
     * @param out  取出的任务
     * @return     取到任务返回 true；队列已停止且无任务返回 false（worker 据此退出）。
     */
    bool dequeue(Task &out);

    /*
     * 停止队列：唤醒所有阻塞在 dequeue() 上的 worker。
     * 已入队的任务仍可被取走执行完（优雅排空）；之后 dequeue 在队空时返回 false。
     */
    void stop();

    // 当前待处理任务数（调试/监控用）
    int size() const;

    // 是否已停止
    bool isStopped() const;

private:
    // 优先级比较器：构造 std::priority_queue 使其"堆顶 = 最该先执行的任务"
    struct TaskCompare {
        bool operator()(const Task &a, const Task &b) const {
            if (a.priority != b.priority)
                return a.priority < b.priority;   // 优先级低的"更小"，沉底
            return a.seq > b.seq;                 // 同优先级：seq 大的（更晚入队）"更小"，沉底 → FIFO
        }
    };

    mutable std::mutex m_mutex;
    std::condition_variable m_cv;
    std::priority_queue<Task, std::vector<Task>, TaskCompare> m_queue;
    std::uint64_t m_seqCounter = 0;   // 单调递增的入队序号
    bool m_stopped = false;
};

#endif // TASKQUEUE_H
