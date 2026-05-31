#ifndef FAVORITESDIALOG_H
#define FAVORITESDIALOG_H

#include <QDialog>
#include "fileclient.h"

namespace Ui {
class FavoritesDialog;
}

class FavoritesDialog : public QDialog
{
    Q_OBJECT

public:
    explicit FavoritesDialog(QWidget *parent = nullptr, 
                            FileClient *fileClient = nullptr,
                            qint64 userId = 0);
    ~FavoritesDialog();

    // 更新收藏列表显示
    void showFavorites(const QStringList &favorites);

signals:
    // 刷新按钮点击时发出，方便后续低耦合接入收藏列表数据源
    void refreshRequested();

private slots:
    // 刷新按钮点击处理
    void onBtnRefreshClicked();

private:
    Ui::FavoritesDialog *ui;
    FileClient *m_fileClient = nullptr;
    qint64 m_userId = 0;
};

#endif // FAVORITESDIALOG_H