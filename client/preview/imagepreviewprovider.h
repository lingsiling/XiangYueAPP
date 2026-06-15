#ifndef IMAGEPREVIEWPROVIDER_H
#define IMAGEPREVIEWPROVIDER_H

#include <QWidget>
#include "previewprovider.h"

namespace Ui {
class ImagePreviewProvider;
}

// ============================================================
//  图片预览控件
//
//  界面在 imagepreviewprovider.ui 中：QScrollArea + 居中 QLabel。
//  渲染策略：
//    - 图片不超过可视区域时按原始尺寸居中显示；
//    - 过大的图片等比缩小到适应视图宽/高，避免撑爆窗口；
//    - 仍超出时由 QScrollArea 提供上下/左右滚动。
//  只依赖 QtGui/QtWidgets，所有 Kit 均可用（无需 QtPdf）。
// ============================================================
class ImagePreviewProvider : public QWidget, public PreviewProvider
{
    Q_OBJECT

public:
    explicit ImagePreviewProvider(QWidget *parent = nullptr);
    ~ImagePreviewProvider() override;

    // PreviewProvider 接口
    bool loadData(const QByteArray &data) override;   // 从内存字节解码图片
    QWidget *widget() override { return this; }

protected:
    // 窗口尺寸变化时，按新的视口大小重新适配图片显示尺寸
    void resizeEvent(QResizeEvent *event) override;

private:
    void applyScaledPixmap();   // 按当前视口大小刷新 label 上的显示图

    Ui::ImagePreviewProvider *ui;
    QPixmap m_pixmap;           // 原始解码图（缩放都基于它，避免反复缩放失真）
};

#endif // IMAGEPREVIEWPROVIDER_H
