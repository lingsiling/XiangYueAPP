#ifndef FAVORITESDIALOG_H
#define FAVORITESDIALOG_H

#include <QDialog>
#include "fileclient.h"

namespace Ui {
class FavoritesDialog;
}

class QTreeWidgetItem;

// FavoritesDialog：只负责展示“我的收藏”界面（已收藏的批次列表）
// 取消收藏统一在资源详情页(ResourceDetailDialog)完成——双击某行即可进入，保持低耦合
class FavoritesDialog : public QDialog
{
    Q_OBJECT

public:
    explicit FavoritesDialog(QWidget *parent = nullptr,
                             FileClient *fileClient = nullptr,
                             qint64 userId = 0);
    ~FavoritesDialog();

private:
    void refreshFavorites();                                   // 向服务端请求收藏列表
    void renderFavorites(const QVector<SessionDto> &items);    // 把批次渲染到树
    void openDetail(QTreeWidgetItem *item);                    // 双击某行 → 打开资源详情页

private:
    Ui::FavoritesDialog *ui;
    FileClient *m_fileClient = nullptr;
    qint64 m_userId = 0;
};

#endif // FAVORITESDIALOG_H
