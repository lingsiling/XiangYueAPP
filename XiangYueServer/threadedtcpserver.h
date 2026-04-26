#ifndef THREADEDTCPServer_H
#define THREADEDTCPServer_H

#include <QTcpServer>

//ThreadedTcpServer：主线程监听端口，accept 到连接后直接拿 socketDescriptor。
//不在主线程创建 QTcpSocket，从根上避免跨线程 socket 引擎问题。
class ThreadedTcpServer : public QTcpServer
{
    Q_OBJECT
public:
    using QTcpServer::QTcpServer;

protected:
    // Qt 在 new connection 时会回调这里，socketDescriptor 是 OS 层连接句柄
    void incomingConnection(qintptr socketDescriptor) override;
};

#endif // THREADEDTCPServer_H