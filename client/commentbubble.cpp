#include "commentbubble.h"

// ============================================================
//  自定义绘制：每条评论左对齐，文字外画半透明圆角矩形气泡
//  气泡宽度自适应文字内容，不撑满整行，不显示选中高亮
// ============================================================
void CommentBubbleDelegate::paint(QPainter *painter, const QStyleOptionViewItem &opt,
                                  const QModelIndex &idx) const
{
    QStyleOptionViewItem myOpt = opt;
    myOpt.state &= ~QStyle::State_Selected;         // 禁用选中态高亮
    initStyleOption(&myOpt, idx);

    const QString text = idx.data(Qt::DisplayRole).toString();
    painter->save();

    // 用 QFontMetrics 精确计算文字实际需要的宽度和高度
    QFontMetrics fm(myOpt.font);
    const int maxWidth = opt.rect.width() - 24;      // 左右各留 12px 边距
    const QRect textRect = fm.boundingRect(0, 0, maxWidth, 0,
        Qt::TextWordWrap, text);
    const int bubbleW = qMin(textRect.width() + 24, maxWidth + 24);
    const int bubbleH = textRect.height() + 16;      // 上下各 8px padding

    // 绘制半透明圆角气泡背景（白色 12% 不透明度）
    painter->setPen(Qt::NoPen);
    painter->setBrush(QColor(255, 255, 255, 30));
    painter->drawRoundedRect(opt.rect.left() + 4, opt.rect.top() + 2,
                             bubbleW, bubbleH, 10, 10);

    // 绘制白色文字（左对齐，允许换行）
    painter->setPen(Qt::white);
    painter->drawText(opt.rect.left() + 16, opt.rect.top() + 6,
                      maxWidth, opt.rect.height(),
                      Qt::TextWordWrap, text);

    painter->restore();
}

// ============================================================
//  自动计算每行高度：根据文字实际渲染高度 + padding
//  使得列表每行的高度刚好包裹气泡，不会多余空白也不会截断
// ============================================================
QSize CommentBubbleDelegate::sizeHint(const QStyleOptionViewItem &opt,
                                      const QModelIndex &idx) const
{
    QStyleOptionViewItem myOpt = opt;
    initStyleOption(&myOpt, idx);
    QFontMetrics fm(myOpt.font);
    const int maxWidth = opt.rect.width() - 24;
    const QRect textRect = fm.boundingRect(0, 0, maxWidth, 0,
        Qt::TextWordWrap, idx.data(Qt::DisplayRole).toString());
    return QSize(opt.rect.width(), textRect.height() + 20);
}
