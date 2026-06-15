#include "pdfpreviewprovider.h"
#include "ui_pdfpreviewprovider.h"

#include <QPdfDocument>
#include <QPdfView>
#include <QBuffer>

PdfPreviewProvider::PdfPreviewProvider(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::PdfPreviewProvider)
    , m_document(new QPdfDocument(this))
{
    ui->setupUi(this);

    // 连续多页 + 适应宽度：滚动条天然支持上下翻阅整篇文档
    ui->pdfView->setPageMode(QPdfView::PageMode::MultiPage);
    ui->pdfView->setZoomMode(QPdfView::ZoomMode::FitToWidth);
    ui->pdfView->setDocument(m_document);
}

PdfPreviewProvider::~PdfPreviewProvider()
{
    // m_buffer 父对象为 this，会随之析构；此处显式关闭更稳妥
    if (m_buffer)
        m_buffer->close();
    delete ui;
}

// 从内存字节装载 PDF（不落盘）：QBuffer 包裹 QByteArray 作为 QIODevice 喂给文档。
// QByteArray / QBuffer 均为成员，生命周期与本控件一致，满足 QPdfDocument 惰性读取的要求。
bool PdfPreviewProvider::loadData(const QByteArray &data)
{
    m_data = data;

    // 重建 buffer（重复预览时复用同一控件的情况）
    if (m_buffer) {
        m_buffer->close();
        m_buffer->deleteLater();
    }
    m_buffer = new QBuffer(&m_data, this);
    if (!m_buffer->open(QIODevice::ReadOnly))
        return false;

    m_document->load(m_buffer);

    // load(QIODevice*) 为同步加载，直接以最终状态判定是否成功
    return m_document->status() == QPdfDocument::Status::Ready;
}
