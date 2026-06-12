#ifndef COMMENTBUBBLE_H
#define COMMENTBUBBLE_H

#include <QStyledItemDelegate>
#include <QPainter>
#include <QFontMetrics>

// ============================================================
//  CommentBubbleDelegate — ResourceDetailDialog 评论聊天气泡代理
//
//  职责：美化 listWidgetComments 中每条评论的显示效果
//  效果：每条评论左对齐，文字外绘半透明圆角矩形气泡
//        气泡宽度自适应文字内容，不撑满整行
//        无选中/悬停高亮
//
//  使用方式（低耦合，一行设置即可）：
//    ui->listWidgetComments->setItemDelegate(
//        new CommentBubbleDelegate(ui->listWidgetComments));
//
//  遵循开闭原则：新增 UI 美化逻辑无需修改业务代码
// ============================================================
class CommentBubbleDelegate : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    // ---- 自定义绘制（实现在 .cpp 中） ----
    void paint(QPainter *painter, const QStyleOptionViewItem &opt,
               const QModelIndex &idx) const override;

    // ---- 自动计算行高（实现在 .cpp 中） ----
    QSize sizeHint(const QStyleOptionViewItem &opt,
                   const QModelIndex &idx) const override;
};

#endif // COMMENTBUBBLE_H
