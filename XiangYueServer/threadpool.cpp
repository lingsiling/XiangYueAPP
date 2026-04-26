#include "threadpool.h"
#include <QDebug>

ThreadPool& ThreadPool::instance()
{
    static ThreadPool inst;
    return inst;
}

ThreadPool::ThreadPool()
    : QObject(nullptr), m_pool(nullptr), m_isRunning(false)
{
}

ThreadPool::~ThreadPool()
{
    if (m_pool) {
        m_pool->waitForDone();
        delete m_pool;
        m_pool = nullptr;
    }
}

void ThreadPool::initialize(int threadCount)
{
    if (m_isRunning) {
        qWarning() << "[ThreadPool] 线程池已初始化，跳过重复初始化";
        return;
    }

    //创建线程池实例
    m_pool = new QThreadPool(this);

    //设置线程数量
    if (threadCount <= 0) {
        //默认：CPU核心数 + 1（提高IO等待时的吞吐量）
        threadCount = QThread::idealThreadCount() + 1;
    }
    m_pool->setMaxThreadCount(threadCount);

    m_isRunning = true;
    qDebug() << "[ThreadPool] 线程池初始化成功，最大线程数:" << threadCount;
}

void ThreadPool::submit(std::function<void()> task)
{
    if (!m_isRunning || !m_pool) {
        qWarning() << "[ThreadPool] 线程池未初始化，任务提交失败";
        return;
    }

    //包装lambda为QRunnable
    class FunctionRunnable : public QRunnable {
    public:
        explicit FunctionRunnable(std::function<void()> fn) : m_fn(fn) {}
        void run() override {
            if (m_fn) m_fn();
        }
    private:
        std::function<void()> m_fn;
    };

    m_pool->start(new FunctionRunnable(task));
}

int ThreadPool::maxThreadCount() const
{
    return m_pool ? m_pool->maxThreadCount() : 0;
}

int ThreadPool::activeThreadCount() const
{
    return m_pool ? m_pool->activeThreadCount() : 0;
}