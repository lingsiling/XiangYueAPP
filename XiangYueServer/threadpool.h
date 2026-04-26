#ifndef THREADPOOL_H
#define THREADPOOL_H

#include <QObject>
#include <QThreadPool>
#include <functional>
#include <memory>

/*
 * ThreadPool：全局线程池管理器
 * 作用：
 *   - 统一管理应用级别的线程资源
 *   - 为异步任务执行和IO操作提供线程支持
 *   - 单例模式，应用全局仅一个实例
 *
 * 设计要点：
 *   - 低耦合：只提供任务提交接口，不关心业务逻辑
 *   - 高效率：通过线程重用避免频繁创建销毁线程
 *   - 可扩展：为后续IO多路复用预留扩展空间
 */
class ThreadPool : public QObject
{
    Q_OBJECT

public:
    //获取全局线程池单例
    static ThreadPool& instance();

    //初始化线程池
    //threadCount: 线程数量（-1表示按CPU核心数自动调整）
    void initialize(int threadCount = -1);

    //提交异步任务（支持lambda表达式）
    void submit(std::function<void()> task);

    //查询线程池状态
    int maxThreadCount() const;
    int activeThreadCount() const;
    bool isRunning() const { return m_isRunning; }

    //获取底层QThreadPool（兼容Qt现有API）
    QThreadPool* pool() const { return m_pool; }

private:
    ThreadPool();
    ~ThreadPool();
    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;

private:
    QThreadPool *m_pool;
    bool m_isRunning;
};

#endif // THREADPOOL_H