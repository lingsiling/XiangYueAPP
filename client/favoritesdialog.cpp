#include "favoritesdialog.h"
#include "ui_favoritesdialog.h"
#include <QFile>

FavoritesDialog::FavoritesDialog(QWidget *parent)
    : QDialog(parent)
    , ui(new Ui::FavoritesDialog)
{
    ui->setupUi(this);

    // 加载样式表
    QFile file(":/qss/favoritesdialog_style.qss");
    if (file.open(QFile::ReadOnly)) {
        setStyleSheet(QLatin1String(file.readAll()));
        file.close();
    }

    // 关闭按钮直接关闭当前对话框，和主窗口保持低耦合
    connect(ui->btnClose, &QPushButton::clicked, this, &QDialog::close);

    // 刷新按钮只发出信号，由外层决定如何刷新收藏数据
    connect(ui->btnRefreshFavorites, &QPushButton::clicked,
            this, &FavoritesDialog::onBtnRefreshClicked);
}

FavoritesDialog::~FavoritesDialog()
{
    delete ui;
}

void FavoritesDialog::onBtnRefreshClicked()
{
    // 这里只做 UI 级别事件转发，不直接依赖文件客户端或业务模块
    emit refreshRequested();
}