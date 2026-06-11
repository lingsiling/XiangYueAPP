#include "mainwindow.h"
#include "ui_mainwindow.h"
#include "favoritesdialog.h"
#include "myuploaddialog.h"
#include "uploadresourcedialog.h"
#include "resourcedetaildialog.h"
#include "fileclient.h"
#include <QFileDialog>
#include <QFileInfo>
#include <QFile>
#include <QDebug>
#include <QPainter>
#include <QPainterPath>

MainWindow::MainWindow(QWidget *parent,QTcpSocket *socket)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
{
    ui->setupUi(this);

    // ===== 加载 QSS 样式表 =====
    QFile styleFile(":/qss/mainwindow_style.qss");
    if (styleFile.open(QFile::ReadOnly)) {
        QString styleSheet = styleFile.readAll();
        this->setStyleSheet(styleSheet);
        styleFile.close();
        qDebug() << "MainWindow 样式表加载成功";
    } else {
        qDebug() << "MainWindow 样式表加载失败，请检查文件路径";
    }
    // ===== 样式表加载完毕 =====

    tcpSocket = socket;

    ui->textEdit->append("主界面已接管连接");

    fileClient = new FileClient(tcpSocket, this);

    //请求批次列表
    fileClient->requestAllSessions();

    //连接 fileReceived 更新 QLabel（只处理头像文件名）
    connect(fileClient, &FileClient::fileReceived, this, [=](const QString &fn, const QString &localPath){
        // 简单判定：头像文件一般是 png/jpg；也可以只允许 user_*.png
        if (fn.endsWith(".png", Qt::CaseInsensitive) ||
            fn.endsWith(".jpg", Qt::CaseInsensitive) ||
            fn.endsWith(".jpeg", Qt::CaseInsensitive))
        {
            QPixmap pix(localPath);
            if (!pix.isNull()) {
                QPixmap scaled = pix.scaled(ui->avatar->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation);
                setCircularAvatar(scaled);
            }
        }
    });

    // 接收服务端批次列表更新（新协议）
    connect(fileClient, &FileClient::sessionsUpdated, this, [=](const QVector<SessionDto> &sessions){
        ui->treeWidgetResources->clear();
        for (const auto &s : sessions) {
            auto *item = new QTreeWidgetItem();
            item->setText(0, s.title.isEmpty() ? "(无名称)" : s.title);

            item->setText(1, QString::number(s.fileCount));
            item->setText(2, s.createdAt);

            item->setData(0, Qt::UserRole, s.id);
            // 用 "||" 作为字段分隔符（避免与 tags 内部 "|" 冲突）
            item->setData(0, Qt::UserRole + 1,
                QString("%1||%2||%3||%4").arg(s.id).arg(s.title).arg(s.tags).arg(s.description));

            ui->treeWidgetResources->addTopLevelItem(item);
        }
    });

    //搜索按钮
    connect(ui->buttonSearch, &QPushButton::clicked, this, [=](){
        QString key = ui->searchline->text();
        refreshList(m_search.filter(key));
    });

    // 我的收藏按钮：只负责弹出收藏 UI，不直接耦合收藏数据源
    connect(ui->buttonFavorite, &QPushButton::clicked, this, [this]() {
        FavoritesDialog dlg(this, fileClient, m_session.userId);

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

    //上传按钮 → 打开上传资源详情UI
    connect(ui->buttonUpload, &QPushButton::clicked, this, [=]() {
        UploadResourceDialog dlg(fileClient, m_session.userId, this);
        dlg.exec();
    });

    // 双击列表项：打开资源详情页
    connect(ui->treeWidgetResources, &QTreeWidget::itemDoubleClicked, this, [=](QTreeWidgetItem *item, int /*column*/){
        const QString data = item->data(0, Qt::UserRole + 1).toString();
        if (data.isEmpty()) return;

        // 用 "||" 分隔（sessionId||title||tags||desc），避免与 tags 内部的 "|" 冲突
        const QStringList parts = data.split("||");
        const qint64 sessionId = parts.value(0).toLongLong();
        const QString title = parts.value(1);
        const QString tags  = parts.value(2);
        const QString desc  = parts.value(3);

        if (sessionId > 0) {
            // resourceName 传 title（批次名），详情页标题显示批次名
            // tags 和 desc 直接通过构造函数参数传入
            const QString combine = QString("%1|%2").arg(sessionId).arg(title);
            ResourceDetailDialog dlg(this, combine, fileClient, m_session.userId, tags, desc);
            dlg.exec();
        }
    });

    //UI追加日志输出
    connect(fileClient, &FileClient::logLine, this, [=](const QString &line){
        ui->textEdit->append(line);
    });
}

void MainWindow::refreshList(const QStringList &list)
{
    ui->treeWidgetResources->clear();
    //每行显示文件名
    for (const QString &fileName : list) {
        if (!fileName.isEmpty()) {
            auto *item = new QTreeWidgetItem();
            item->setText(0, fileName);
            ui->treeWidgetResources->addTopLevelItem(item);
        }
    }
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

void MainWindow::showMyUploadDialog()
{
    // 如果对话框已存在，就直接激活，避免重复创建多个窗口
    if (m_myUploadDialog != nullptr) {
        m_myUploadDialog->activateWindow();
        m_myUploadDialog->raise();
        return;
    }

    m_myUploadDialog = new MyUploadDialog(fileClient, m_session.userId, this);
    m_myUploadDialog->setAttribute(Qt::WA_DeleteOnClose, true);

    // 对话框销毁后清空指针，避免悬空引用
    connect(m_myUploadDialog, &QObject::destroyed, this, [this]() {
        m_myUploadDialog = nullptr;
    });

    m_myUploadDialog->show();
}

void MainWindow::setCircularAvatar(const QPixmap &pixmap)
{
    if (pixmap.isNull()) {
        qDebug() << "错误：传入的pixmap为空！";
        return;
    }
    
    // 创建圆形头像，使用标签的大小
    QSize size = ui->avatar->size();
    if (size.isEmpty()) {
        size = QSize(120, 120);
    }
    
    QPixmap circular(size);
    circular.fill(Qt::transparent);
    
    QPainter painter(&circular);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
    
    // 绘制圆形路径
    QPainterPath path;
    path.addEllipse(0, 0, size.width(), size.height());
    painter.setClipPath(path);
    
    // 缩放并绘制图片
    QPixmap scaledPix = pixmap.scaledToWidth(size.width(), Qt::SmoothTransformation);
    int y = (size.height() - scaledPix.height()) / 2;
    painter.drawPixmap(0, y, scaledPix);
    painter.end();
    
    qDebug() << "圆形头像创建成功，大小:" << size;
    ui->avatar->setPixmap(circular);
}

MainWindow::~MainWindow()
{
    delete ui;
}