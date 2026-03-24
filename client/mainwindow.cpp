#include "mainwindow.h"
#include "ui_mainwindow.h"
#include <QFileDialog>
#include <QFileInfo>
MainWindow::MainWindow(QWidget *parent,QTcpSocket *socket)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
{
    ui->setupUi(this);

    tcpSocket = socket;

    ui->textEdit->append("主界面已接管连接");
    sendSize = 0;

    // 监听服务器消息（以后用）
    // connect(tcpSocket, &QTcpSocket::readyRead, this, [=]() {
    //     QByteArray data = tcpSocket->readAll();
    //     ui->textEdit->append("收到：" + data);
    // });

}

MainWindow::~MainWindow()
{
    delete ui;
}

void MainWindow::on_buttonUpload_clicked()
{
    QString filePath = QFileDialog::getOpenFileName(this,"选择文件","../");
    //如果没有选择文件，直接返回
    if(filePath.isEmpty())
    {
        ui->textEdit->append("未选择文件");
        return;
    }
    //获取文件信息
    QFileInfo info(filePath);

    fileName = info.fileName(); //文件名
    fileSize = info.size(); //文件大小
    sendSize = 0; //已发送大小（初始化为零）

    //打开文件为只读
    file.setFileName(filePath);

    if(!file.open(QIODevice::ReadOnly))
    {
        qDebug()<<"文件打开失败";
    }
    ui->textEdit->append(fileName);

}

