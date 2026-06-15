#include "favoritesdialog.h"
#include "ui_favoritesdialog.h"
#include "resourcedetaildialog.h"

#include <QFile>
#include <QTreeWidgetItem>

// 在树项里缓存打开详情页所需的批次信息（避免从显示文本反向解析）
static constexpr int ROLE_SESSION_ID = Qt::UserRole;       // 批次ID
static constexpr int ROLE_TAGS       = Qt::UserRole + 1;   // 标签（"|"分隔）
static constexpr int ROLE_DESC       = Qt::UserRole + 2;   // 资源介绍

FavoritesDialog::FavoritesDialog(QWidget *parent,
                                 FileClient *fileClient,
                                 qint64 userId)
    : QDialog(parent)
    , ui(new Ui::FavoritesDialog)
    , m_fileClient(fileClient)
    , m_userId(userId)
{
    ui->setupUi(this);

    // 加载样式表
    QFile file(":/qss/favoritesdialog_style.qss");
    if (file.open(QFile::ReadOnly)) {
        setStyleSheet(QLatin1String(file.readAll()));
        file.close();
    }

    // 只负责“展示 + 交互”，数据请求交给 FileClient
    connect(ui->btnClose, &QPushButton::clicked, this, &QDialog::close);
    connect(ui->btnRefreshFavorites, &QPushButton::clicked, this, [this]() {
        refreshFavorites();
    });

    // 双击某行 → 打开资源详情页（在详情页内取消收藏）
    connect(ui->treeWidgetFavorites, &QTreeWidget::itemDoubleClicked, this,
            [this](QTreeWidgetItem *item, int) { openDetail(item); });

    if (m_fileClient) {
        // 收藏列表刷新结果：只处理当前用户对应的数据
        connect(m_fileClient, &FileClient::favoritesUpdated, this,
                [this](qint64 uid, const QVector<SessionDto> &items) {
                    if (uid != m_userId) return;
                    renderFavorites(items);
                });
    }

    // 对话框打开时自动拉一次收藏列表
    refreshFavorites();
}

FavoritesDialog::~FavoritesDialog()
{
    delete ui;
}

void FavoritesDialog::refreshFavorites()
{
    if (!m_fileClient || m_userId <= 0)
        return;

    m_fileClient->getFavorites(m_userId);
}

void FavoritesDialog::renderFavorites(const QVector<SessionDto> &items)
{
    ui->treeWidgetFavorites->clear();

    // 没有任何收藏时给出占位提示，并设为不可选中
    if (items.isEmpty()) {
        auto *placeholder = new QTreeWidgetItem(ui->treeWidgetFavorites);
        placeholder->setText(0, "暂无收藏");
        placeholder->setFlags(placeholder->flags() & ~Qt::ItemIsSelectable & ~Qt::ItemIsEnabled);
        return;
    }

    for (const auto &it : items) {
        // 三列展示：资源名称(批次名) / 文件数 / 上传时间，与“我的上传”保持一致
        auto *item = new QTreeWidgetItem(ui->treeWidgetFavorites);
        item->setText(0, it.title.isEmpty() ? "(无名称)" : it.title);
        item->setText(1, QString::number(it.fileCount));
        item->setText(2, it.createdAt);
        // 把打开详情页需要的信息缓存进 item（批次ID/标签/介绍）
        item->setData(0, ROLE_SESSION_ID, it.id);
        item->setData(0, ROLE_TAGS, it.tags);
        item->setData(0, ROLE_DESC, it.description);
    }
}

void FavoritesDialog::openDetail(QTreeWidgetItem *item)
{
    if (!item || !m_fileClient) return;

    const qint64 sessionId = item->data(0, ROLE_SESSION_ID).toLongLong();
    if (sessionId <= 0) return;   // 占位项无有效批次ID

    const QString title = item->text(0);
    const QString tags  = item->data(0, ROLE_TAGS).toString();
    const QString desc  = item->data(0, ROLE_DESC).toString();

    // 详情页约定的 resourceName 形如 "sessionId|批次名"
    const QString combine = QString("%1|%2").arg(sessionId).arg(title);
    ResourceDetailDialog dlg(this, combine, m_fileClient, m_userId, tags, desc);
    dlg.exec();

    // 详情页可能在其中取消了收藏，关闭后刷新列表，保证显示与服务端一致
    refreshFavorites();
}
