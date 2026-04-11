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

    //连接 fileReceived 更新 QLabel（只处理头像文件名）
    connect(fileClient, &FileClient::fileReceived, this, [=](const QString &fn, const QString &localPath){
        // 简单判定：头像文件一般是 png/jpg；也可以只允许 user_*.png
        if (fn.endsWith(".png", Qt::CaseInsensitive) ||
            fn.endsWith(".jpg", Qt::CaseInsensitive) ||
            fn.endsWith(".jpeg", Qt::CaseInsensitive))
        {
            QPixmap pix(localPath);
            if (!pix.isNull())
                ui->avatar->setPixmap(pix.scaled(ui->avatar->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
        }
    });

    //接收服务端列表更新
    connect(fileClient, &FileClient::resourcesUpdated, this, [=](const QStringList &list){
        m_allResources = list;
        m_search.setAllResources(list);

        //如果搜索框为空，显示全量；否则保持当前搜索结果
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

void MainWindow::setSession(const UserSession &s)
{
    //只做“显示”，不在这里做登录逻辑
    m_session = s;

    ui->username->setText(s.username);

    requestAvatarIfNeeded();
}

void MainWindow::requestAvatarIfNeeded()
{
    if (!tcpSocket || tcpSocket->state() != QAbstractSocket::ConnectedState)
        return;

    //头像下载复用 FILE 机制，所以直接让服务端发 FILE##... 回来
    const QString cmd = QString("GET_AVATAR##%1\n").arg(m_session.userId);
    tcpSocket->write(cmd.toUtf8());
}

MainWindow::~MainWindow()
{
    delete ui;
}