#include "threadedtcpserver.h"
#include "clientworker.h"

#include <QThread>
#include <QDebug>

void ThreadedTcpServer::incomingConnection(qintptr socketDescriptor)
{
    //每连接一个线程，线程里创建一个 ClientWorker 来处理这个连接
    QThread *t = new QThread;

    //Worker 只拿 descriptor，在子线程里创建 QTcpSocket 并接管 descriptor
    ClientWorker *worker = new ClientWorker(socketDescriptor);

    worker->moveToThread(t);

    //线程启动后初始化 worker
    QObject::connect(t, &QThread::started, worker, &ClientWorker::start);

    //worker 请求退出线程
    QObject::connect(worker, &ClientWorker::finished, t, &QThread::quit);

    //清理：线程结束后再释放 worker / thread，避免 “Destroyed while running”
    QObject::connect(t, &QThread::finished, worker, &QObject::deleteLater);
    QObject::connect(t, &QThread::finished, t, &QObject::deleteLater);

    t->start();
}