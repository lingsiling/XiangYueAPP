#ifndef COMMENTBUBBLE_H
#define COMMENTBUBBLE_H

#include <QStyledItemDelegate>
#include <QPainter>
#include <QFontMetrics>

// ============================================================
//  CommentBubbleDelegate — ResourceDetailDialog 评论聊天气泡代理
//
//  职责：美化 listWidgetComments 中每条评论的显示效果，并实现
//        "自己的评论靠右、别人的评论靠左" 的聊天式布局
//
//  效果：
//    - 别人的评论：气泡靠左对齐
//    - 自己的评论：气泡靠右对齐（颜色稍作区分）
//    - 气泡宽度自适应文字内容，不撑满整行，对侧留白
//    - 无选中/悬停高亮
//
//  低耦合设计：
//    - delegate 不感知"当前登录用户是谁"，只读取 item 上的
//      IsMineRole 布尔标记来决定左右对齐
//    - 是否为本人评论由 ResourceDetailDialog 在填充列表时写入，
//      布局逻辑（本文件）与业务逻辑（dialog）彻底分离
//
//  使用方式（一行设置即可）：
//    ui->listWidgetComments->setItemDelegate(
//        new CommentBubbleDelegate(ui->listWidgetComments));
//    // 填充每条评论时标记归属：
//    item->setData(CommentBubbleDelegate::IsMineRole, isMine);
//
//  遵循开闭原则：新增 UI 美化逻辑无需修改业务代码
// ============================================================
class CommentBubbleDelegate : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    // ---- 自定义数据角色：标记该评论是否为"当前用户自己发布" ----
    //   true  → 气泡靠右显示（自己的评论）
    //   false → 气泡靠左显示（别人的评论）
    // 取 Qt::UserRole + 2，避开 ResourceDetailDialog 已占用的
    // Qt::UserRole（评论ID）与 Qt::UserRole + 1（评论者ID）
    static constexpr int IsMineRole = Qt::UserRole + 2;

    // ---- 自定义绘制（实现在 .cpp 中） ----
    void paint(QPainter *painter, const QStyleOptionViewItem &opt,
               const QModelIndex &idx) const override;

    // ---- 自动计算行高（实现在 .cpp 中） ----
    QSize sizeHint(const QStyleOptionViewItem &opt,
                   const QModelIndex &idx) const override;
};

#endif // COMMENTBUBBLE_H
