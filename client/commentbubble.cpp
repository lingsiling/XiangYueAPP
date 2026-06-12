#include "commentbubble.h"

// ============================================================
//  布局常量（paint 与 sizeHint 必须使用同一套数值，
//  否则会出现气泡高度与行高不匹配的截断/空白）
// ============================================================
namespace {
constexpr int kHMargin   = 12;   // 气泡距列表左/右边缘的水平外边距
constexpr int kPadding   = 12;   // 气泡内文字的左右内边距
constexpr int kVPadding  = 8;    // 气泡内文字的上下内边距
constexpr int kVMargin   = 2;    // 气泡距行顶部的垂直外边距

// 计算气泡内文字内容的最大可用宽度：
// 约占整行的 3/4，保证对侧始终留白，呈现聊天气泡效果
inline int contentMaxWidth(int rowWidth)
{
    return rowWidth * 3 / 4 - kPadding * 2;
}
}

// ============================================================
//  自定义绘制：根据 IsMineRole 决定气泡左/右对齐
//    - 别人的评论：靠左，半透明白色气泡
//    - 自己的评论：靠右，半透明蓝色气泡（视觉区分）
//  气泡宽度自适应文字内容，不撑满整行，不显示选中高亮
// ============================================================
void CommentBubbleDelegate::paint(QPainter *painter, const QStyleOptionViewItem &opt,
                                  const QModelIndex &idx) const
{
    QStyleOptionViewItem myOpt = opt;
    myOpt.state &= ~QStyle::State_Selected;         // 禁用选中态高亮
    initStyleOption(&myOpt, idx);

    const QString text = idx.data(Qt::DisplayRole).toString();
    // 读取归属标记：true=自己的评论(靠右)，false=别人的评论(靠左)
    const bool isMine = idx.data(IsMineRole).toBool();

    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);

    // 用 QFontMetrics 精确计算文字在限定宽度内换行后的实际尺寸
    QFontMetrics fm(myOpt.font);
    const int maxContentW = contentMaxWidth(opt.rect.width());
    const QRect textRect = fm.boundingRect(0, 0, maxContentW, 0,
                                           Qt::TextWordWrap, text);

    // 气泡尺寸 = 文字尺寸 + 内边距
    const int contentW = textRect.width();
    const int bubbleW  = contentW + kPadding * 2;
    const int bubbleH  = textRect.height() + kVPadding * 2;

    // 根据归属决定气泡水平起点：自己的靠右，别人的靠左
    const int bubbleX = isMine
        ? (opt.rect.right() - kHMargin - bubbleW)   // 靠右对齐
        : (opt.rect.left()  + kHMargin);            // 靠左对齐
    const int bubbleY = opt.rect.top() + kVMargin;

    // 绘制半透明圆角气泡背景（自己/别人用不同色调区分）
    painter->setPen(Qt::NoPen);
    painter->setBrush(isMine ? QColor(64, 158, 255, 90)   // 自己：淡蓝
                             : QColor(255, 255, 255, 30)); // 别人：淡白
    painter->drawRoundedRect(bubbleX, bubbleY, bubbleW, bubbleH, 10, 10);

    // 绘制白色文字（在气泡内部，允许换行）
    painter->setPen(Qt::white);
    painter->drawText(QRect(bubbleX + kPadding, bubbleY + kVPadding,
                            contentW, textRect.height()),
                      Qt::TextWordWrap, text);

    painter->restore();
}

// ============================================================
//  自动计算每行高度：根据文字实际渲染高度 + 上下边距
//  使列表每行高度刚好包裹气泡，不会多余空白也不会截断
//  注意：必须与 paint() 使用相同的内容最大宽度算法
// ============================================================
QSize CommentBubbleDelegate::sizeHint(const QStyleOptionViewItem &opt,
                                      const QModelIndex &idx) const
{
    QStyleOptionViewItem myOpt = opt;
    initStyleOption(&myOpt, idx);

    QFontMetrics fm(myOpt.font);
    const int maxContentW = contentMaxWidth(opt.rect.width());
    const QRect textRect = fm.boundingRect(0, 0, maxContentW, 0,
        Qt::TextWordWrap, idx.data(Qt::DisplayRole).toString());

    // 行高 = 文字高度 + 上下内边距 + 上下外边距余量
    return QSize(opt.rect.width(),
                 textRect.height() + kVPadding * 2 + kVMargin * 2);
}
