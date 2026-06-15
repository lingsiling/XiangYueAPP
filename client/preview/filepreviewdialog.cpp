#include "filepreviewdialog.h"
#include "previewprovider.h"
#include "imagepreviewprovider.h"

#include <QVBoxLayout>
#include <QMessageBox>
#include <QFile>
#include <QFileInfo>

// PDF 预览仅在装有 QtPdfWidgets 模块（定义了 HAVE_QT_PDF）的构建里编译。
// 未启用时，PDF 类型会在 previewData 里给出友好提示而非编译失败。
#ifdef HAVE_QT_PDF
#include "pdfpreviewprovider.h"
#endif

bool FilePreviewDialog::isPreviewable(const QString &fileName)
{
    using T = PreviewSupport::Type;
    const T t = PreviewSupport::detect(fileName);
    if (t == T::Image)
        return true;
#ifdef HAVE_QT_PDF
    if (t == T::Pdf)
        return true;
#endif
    return false;
}

void FilePreviewDialog::previewLocalFile(QWidget *parent, const QString &filePath)
{
    QFile f(filePath);
    if (!f.open(QIODevice::ReadOnly)) {
        QMessageBox::warning(parent, "预览", "无法打开文件：" + filePath);
        return;
    }
    const QByteArray data = f.readAll();
    f.close();

    previewData(parent, QFileInfo(filePath).fileName(), data);
}

void FilePreviewDialog::previewData(QWidget *parent, const QString &fileName, const QByteArray &data)
{
    using T = PreviewSupport::Type;
    const T t = PreviewSupport::detect(fileName);

    // 不支持的类型 / 当前构建未启用 PDF：直接给出友好提示，不弹空窗口
    if (t == T::Unsupported) {
        QMessageBox::information(parent, "预览",
            "暂不支持预览该类型文件，目前仅支持图片和 PDF。");
        return;
    }
#ifndef HAVE_QT_PDF
    if (t == T::Pdf) {
        QMessageBox::information(parent, "预览",
            "当前程序未启用 PDF 预览（缺少 QtPdf 模块）。");
        return;
    }
#endif

    if (data.isEmpty()) {
        QMessageBox::warning(parent, "预览", "文件内容为空，无法预览。");
        return;
    }

    // 栈上构造对话框并模态显示：Provider 解析失败时不展示空窗口，改为提示
    FilePreviewDialog dlg(parent, fileName, data);
    if (!dlg.m_loaded) {
        QMessageBox::warning(parent, "预览", "无法解析文件内容，文件可能已损坏。");
        return;
    }
    dlg.exec();
}

FilePreviewDialog::FilePreviewDialog(QWidget *parent,
                                     const QString &fileName,
                                     const QByteArray &data)
    : QDialog(parent)
{
    setWindowTitle(QString("预览 - %1").arg(fileName));
    resize(900, 700);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    // 按类型选择 Provider（工厂分发）。Provider 的 parent 设为 this，随对话框析构。
    PreviewProvider *provider = nullptr;
    switch (PreviewSupport::detect(fileName)) {
    case PreviewSupport::Type::Image:
        provider = new ImagePreviewProvider(this);
        break;
#ifdef HAVE_QT_PDF
    case PreviewSupport::Type::Pdf:
        provider = new PdfPreviewProvider(this);
        break;
#endif
    default:
        break;   // 不可达：类型已在 previewData 中过滤
    }

    if (provider) {
        m_loaded = provider->loadData(data);
        layout->addWidget(provider->widget());
    }
}
