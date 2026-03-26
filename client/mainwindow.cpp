#include "mainwindow.h"
#include "ui_mainwindow.h"
#include "fileclient.h"
#include <QFileDialog>
#include <QFileInfo>
MainWindow::MainWindow(QWidget *parent,QTcpSocket *socket)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
{
    ui->setupUi(this);

    tcpSocket = socket;

    ui->textEdit->append("主界面已接管连接");

    fileClient = new FileClient(tcpSocket, this);

    //进入自动请求列表
    fileClient->requestList();

    //上传按钮
    connect(ui->buttonUpload, &QPushButton::clicked, this, [=]() {

        QString filePath = QFileDialog::getOpenFileName(this,"选择文件");

        if(filePath.isEmpty()) return;

        fileClient->uploadFile(filePath);
    });

    //点击下载
    connect(ui->listWidget, &QListWidget::itemClicked, this, [=](QListWidgetItem *item){

        fileClient->downloadFile(item->text());
    });
}

MainWindow::~MainWindow()
{
    delete ui;
}