#include "imagepreviewprovider.h"
#include "ui_imagepreviewprovider.h"

#include <QPixmap>
#include <QResizeEvent>
#include <QShowEvent>

ImagePreviewProvider::ImagePreviewProvider(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::ImagePreviewProvider)
{
    ui->setupUi(this);
}

ImagePreviewProvider::~ImagePreviewProvider()
{
    delete ui;
}

// 从内存字节解码图片（QPixmap 自动按内容识别格式，无需扩展名）
bool ImagePreviewProvider::loadData(const QByteArray &data)
{
    QPixmap pm;
    if (!pm.loadFromData(data) || pm.isNull())
        return false;

    m_pixmap = pm;
    // 注意：此时控件可能尚未被布局，视口尺寸不准；
    // 真正的首帧缩放由 showEvent() 触发，保证布局已就绪。
    applyScaledPixmap();
    return true;
}

// 控件首次显示时（布局已就绪），强制按正确视口尺寸刷新缩放
void ImagePreviewProvider::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    if (!m_pixmap.isNull())
        applyScaledPixmap();
}

// 窗口尺寸变化时，按新的视口大小重新适配图片显示尺寸
void ImagePreviewProvider::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    if (!m_pixmap.isNull())
        applyScaledPixmap();
}

// 始终按视口大小、保持宽高比进行平滑缩放：
//   - 小图自动放大填充视口（水平/垂直至少一个方向填满）；
//   - 大图自动缩小到适配视口；
//   - 极端比例图片（如超长图）会在另一方向超出，靠 QScrollArea 滚动查看。
void ImagePreviewProvider::applyScaledPixmap()
{
    // 留 4px 边距，避免缩放后恰好触发滚动条来回抖动
    const QSize viewport = ui->scrollArea->viewport()->size() - QSize(4, 4);

    // 始终缩放：Qt::KeepAspectRatio 保证宽高比不变，自动适配（该放大就放大，该缩小就缩小）
    ui->labelImage->setPixmap(
        m_pixmap.scaled(viewport, Qt::KeepAspectRatio, Qt::SmoothTransformation));
}
