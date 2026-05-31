#include "favoritesdialog.h"
#include "ui_favoritesdialog.h"
#include <QFile>

FavoritesDialog::FavoritesDialog(QWidget *parent, 
                                FileClient *fileClient,
                                qint64 userId)
    : QDialog(parent)
    , ui(new Ui::FavoritesDialog)
    , m_fileClient(fileClient)
    , m_userId(userId)
{
    ui->setupUi(this);

    //加载样式表
    QFile file(":/qss/favoritesdialog_style.qss");
    if (file.open(QFile::ReadOnly)) {
        setStyleSheet(QLatin1String(file.readAll()));
        file.close();
    }

    //关闭按钮直接关闭当前对话框，和主窗口保持低耦合
    connect(ui->btnClose, &QPushButton::clicked, this, &QDialog::close);

    //刷新按钮只发出信号，由外层决定如何刷新收藏数据
    connect(ui->btnRefreshFavorites, &QPushButton::clicked,
            this, &FavoritesDialog::onBtnRefreshClicked);

    // 如果有 FileClient，连接收藏列表更新信号，并自动加载
    if (m_fileClient) {
        connect(m_fileClient, &FileClient::favoritesUpdated, this,
                [this](const QStringList &favorites) {
                    showFavorites(favorites);
                });

        // 自动加载收藏列表
        if (m_userId > 0) {
            m_fileClient->getFavorites(m_userId);
        }
    } else {
        ui->listWidgetFavorites->addItem("暂无收藏");
    }
}

FavoritesDialog::~FavoritesDialog()
{
    delete ui;
}

void FavoritesDialog::onBtnRefreshClicked()
{
    // 手动刷新：重新加载收藏列表
    if (m_fileClient && m_userId > 0) {
        m_fileClient->getFavorites(m_userId);
    }
    
    // 这里也发出信号，方便后续扩展
    emit refreshRequested();
}

void FavoritesDialog::showFavorites(const QStringList &favorites)
{
    ui->listWidgetFavorites->clear();

    if (favorites.isEmpty()) {
        ui->listWidgetFavorites->addItem("暂无收藏");
        return;
    }

    // 显示所有收藏的资源
    for (const auto &fav : favorites) {
        ui->listWidgetFavorites->addItem(fav);
    }
}