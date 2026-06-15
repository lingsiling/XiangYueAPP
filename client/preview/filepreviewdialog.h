#ifndef FILEPREVIEWDIALOG_H
#define FILEPREVIEWDIALOG_H

#include <QDialog>
#include <QByteArray>

// ============================================================
//  文件预览宿主对话框（统一入口，低耦合）
//
//  职责：根据文件名（扩展名）选择合适的 PreviewProvider（图片 / PDF），
//  把它的控件嵌进一个窗口里显示。对外只暴露两个静态入口：
//
//    - previewLocalFile(parent, path)        —— 上传界面：文件就在本地磁盘
//    - previewData(parent, name, bytes)      —— 资源详情：文件已从服务端流入内存（不落盘）
//
//  两条入口最终都汇聚到“以内存字节渲染”的同一路径，因此渲染逻辑零重复。
//  上层调用方无需 #include 任何 Provider，也不感知 QtPdf 是否可用。
// ============================================================
class FilePreviewDialog : public QDialog
{
    Q_OBJECT

public:
    // 预览本地文件：读入字节后转交 previewData。读不到文件会弹出提示。
    static void previewLocalFile(QWidget *parent, const QString &filePath);

    // 预览内存数据：name 仅用于判类型与窗口标题，data 为完整文件字节。
    static void previewData(QWidget *parent, const QString &fileName, const QByteArray &data);

    // 该文件名是否支持预览（按扩展名）。供上层在点“预览”前做友好提示。
    static bool isPreviewable(const QString &fileName);

private:
    explicit FilePreviewDialog(QWidget *parent,
                               const QString &fileName,
                               const QByteArray &data);

    bool m_loaded = false;   // Provider 是否成功解析了数据
};

#endif // FILEPREVIEWDIALOG_H
