#include "imagepreviewprovider.h"
#include "ui_imagepreviewprovider.h"

#include <QPixmap>
#include <QResizeEvent>

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
    applyScaledPixmap();
    return true;
}

void ImagePreviewProvider::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    // 视口尺寸变化时重新适配（仅在已有图片时）
    if (!m_pixmap.isNull())
        applyScaledPixmap();
}

// 按当前视口大小决定显示尺寸：
//   - 图片本身比视口小 → 原始尺寸显示（不放大，避免模糊）；
//   - 超出视口 → 等比缩小到适应视口，超出部分仍可借滚动条查看。
void ImagePreviewProvider::applyScaledPixmap()
{
    // 留一点边距，避免缩放后恰好触发滚动条来回抖动
    const QSize viewport = ui->scrollArea->viewport()->size() - QSize(4, 4);

    if (m_pixmap.width() <= viewport.width() &&
        m_pixmap.height() <= viewport.height()) {
        ui->labelImage->setPixmap(m_pixmap);
    } else {
        ui->labelImage->setPixmap(
            m_pixmap.scaled(viewport, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    }
}
