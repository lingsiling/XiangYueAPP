#include "mainwindow.h"
#include "ui_mainwindow.h"
#include "favoritesdialog.h"
#include "myuploaddialog.h"
#include "resourcedetaildialog.h"
#include "fileclient.h"
#include "transferdialog.h"
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

    // 我的收藏按钮：只负责弹出收藏 UI，不直接耦合收藏数据源
    connect(ui->buttonFavorite, &QPushButton::clicked, this, [this]() {
        FavoritesDialog dlg(this);

        // 刷新按钮点击后，先把信号抛出去，后续再按你的收藏模块接入数据刷新
        connect(&dlg, &FavoritesDialog::refreshRequested, this, [this]() {
            ui->textEdit->append("收藏列表刷新按钮已点击");
        });

        dlg.exec();
    });

    //“我的上传”按钮：只弹出上传 UI，不执行任何上传业务
    connect(ui->buttonMyUpload, &QPushButton::clicked, this, [this]() {
        showMyUploadDialog();
    });

    //上传按钮（可多选）
    connect(ui->buttonUpload, &QPushButton::clicked, this, [=]() {

        const QStringList paths = QFileDialog::getOpenFileNames(this, "选择文件（可多选）");
        if (paths.isEmpty()) return;
        //显示上传进度条
        showUploadProgressDialog();
        //UI 只调用接口，不关心“队列/协议/确认机制”
        fileClient->uploadFiles(paths);
    });

    // 双击资源列表项：跳转到资源详情页
    connect(ui->listWidget, &QListWidget::itemDoubleClicked, this, [=](QListWidgetItem *item){

        QString resourceName = item->text();
        // 打开资源详情对话框（模态）
        // 这里把资源名、fileClient 传给详情页，详情页里点“下载”再触发下载
        ResourceDetailDialog dlg(this, resourceName, fileClient, m_session.userId);
        dlg.exec();
    });

    //UI追加日志输出
    connect(fileClient, &FileClient::logLine, this, [=](const QString &line){
        ui->textEdit->append(line);
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

qint64 MainWindow::currentUserId() const
{
    return m_session.userId;
}

void MainWindow::requestAvatarIfNeeded()
{
    if (!tcpSocket || tcpSocket->state() != QAbstractSocket::ConnectedState)
        return;

    //头像下载复用 FILE 机制，所以直接让服务端发 FILE##... 回来
    const QString cmd = QString("GET_AVATAR##%1\n").arg(m_session.userId);
    tcpSocket->write(cmd.toUtf8());
}

void MainWindow::showUploadProgressDialog()
{
    //如果对话框已存在且仍在显示，则不重复创建
    if (m_uploadDialog != nullptr) {
        m_uploadDialog->activateWindow();
        m_uploadDialog->raise();
        return;
    }

    //创建新的上传进度条对话框
    m_uploadDialog = new TransferDialog(this);
    m_uploadDialog->setTransferType(TransferDialog::Upload);

    //连接上传进度信号
    connect(fileClient, &FileClient::uploadProgress,
            m_uploadDialog, &TransferDialog::updateProgress);

    //连接上传完成信号
    connect(fileClient, &FileClient::uploadFinished,
            m_uploadDialog, &TransferDialog::completeTransfer);

    //连接取消上传请求信号
    connect(m_uploadDialog, &TransferDialog::cancelRequested,
            fileClient, &FileClient::cancelUploadRequested);

    //对话框关闭后清理指针
    connect(m_uploadDialog, &TransferDialog::finished,
            this, [=]() {
        if (m_uploadDialog) {
            m_uploadDialog->deleteLater();
            m_uploadDialog = nullptr;
        }
    });

    //显示进度条对话框（非模态，允许事件循环处理）
    m_uploadDialog->show();
}

void MainWindow::showMyUploadDialog()
{
    // 如果对话框已存在，就直接激活，避免重复创建多个窗口
    if (m_myUploadDialog != nullptr) {
        m_myUploadDialog->activateWindow();
        m_myUploadDialog->raise();
        return;
    }

    m_myUploadDialog = new MyUploadDialog(this);
    m_myUploadDialog->setAttribute(Qt::WA_DeleteOnClose, true);

    // 对话框销毁后清空指针，避免悬空引用
    connect(m_myUploadDialog, &QObject::destroyed, this, [this]() {
        m_myUploadDialog = nullptr;
    });

    m_myUploadDialog->show();
}

MainWindow::~MainWindow()
{
    delete ui;
}