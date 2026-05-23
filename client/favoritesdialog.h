#ifndef FAVORITESDIALOG_H
#define FAVORITESDIALOG_H

#include <QDialog>

namespace Ui {
class FavoritesDialog;
}

class FavoritesDialog : public QDialog
{
    Q_OBJECT

public:
    explicit FavoritesDialog(QWidget *parent = nullptr);
    ~FavoritesDialog();

signals:
    // 刷新按钮点击时发出，方便后续低耦合接入收藏列表数据源
    void refreshRequested();

private slots:
    // 刷新按钮点击处理
    void onBtnRefreshClicked();

private:
    Ui::FavoritesDialog *ui;
};

#endif // FAVORITESDIALOG_H