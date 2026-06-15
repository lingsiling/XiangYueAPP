#ifndef PDFPREVIEWPROVIDER_H
#define PDFPREVIEWPROVIDER_H

#include <QWidget>
#include "previewprovider.h"

class QPdfDocument;
class QBuffer;

namespace Ui {
class PdfPreviewProvider;
}

// ============================================================
//  PDF 预览控件（依赖 QtPdf / QtPdfWidgets）
//
//  界面在 pdfpreviewprovider.ui 中：一个 QPdfView。
//  采用 MultiPage 连续翻页模式 + FitToWidth 适宽缩放，
//  天然支持【上下滚动】浏览整篇文档。
//
//  数据来源为内存字节（不落盘）：用 QBuffer 包裹 QByteArray 交给
//  QPdfDocument::load(QIODevice*)。注意 QPdfDocument 惰性读取，
//  故 QByteArray 与 QBuffer 必须随本控件存活，不能用临时变量。
//
//  整个类仅在定义了 HAVE_QT_PDF 时才编译（见 .pro 的 qtHaveModule 门控）。
// ============================================================
class PdfPreviewProvider : public QWidget, public PreviewProvider
{
    Q_OBJECT

public:
    explicit PdfPreviewProvider(QWidget *parent = nullptr);
    ~PdfPreviewProvider() override;

    // PreviewProvider 接口
    bool loadData(const QByteArray &data) override;   // 从内存字节装载 PDF
    QWidget *widget() override { return this; }

private:
    Ui::PdfPreviewProvider *ui;
    QPdfDocument *m_document = nullptr;   // PDF 文档对象
    QByteArray   m_data;                  // PDF 原始字节（须随文档存活）
    QBuffer     *m_buffer = nullptr;      // 包裹 m_data 的 QIODevice（须随文档存活）
};

#endif // PDFPREVIEWPROVIDER_H
