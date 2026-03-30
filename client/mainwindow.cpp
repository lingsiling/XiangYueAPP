#include "mainwindow.h"
#include "ui_mainwindow.h"
#include "resourcedetaildialog.h"
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

    //接收服务端列表更新
    connect(fileClient, &FileClient::resourcesUpdated, this, [=](const QStringList &list){
        m_allResources = list;
        m_search.setAllResources(list);

        //如果搜索框为空，显示全量；否则保持当前搜索结果（更友好）
        QString key = ui->searchline->text();
        refreshList(m_search.filter(key));
    });

    //搜索按钮
    connect(ui->buttonSearch, &QPushButton::clicked, this, [=](){
        QString key = ui->searchline->text();
        refreshList(m_search.filter(key));
    });

    //上传按钮
    connect(ui->buttonUpload, &QPushButton::clicked, this, [=]() {

        QString filePath = QFileDialog::getOpenFileName(this,"选择文件");

        if(filePath.isEmpty()) return;

        fileClient->uploadFile(filePath);
    });

    // 双击资源列表项：跳转到资源详情页
    connect(ui->listWidget, &QListWidget::itemDoubleClicked, this, [=](QListWidgetItem *item){

        QString resourceName = item->text();
        // 打开资源详情对话框（模态）
        // 这里把资源名、fileClient 传给详情页，详情页里点“下载”再触发下载
        ResourceDetailDialog dlg(this, resourceName, fileClient);
        dlg.exec();
    });
}

void MainWindow::refreshList(const QStringList &list)
{
    ui->listWidget->clear();
    ui->listWidget->addItems(list);
}

MainWindow::~MainWindow()
{
    delete ui;
}