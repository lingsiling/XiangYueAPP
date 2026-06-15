#ifndef PREVIEWPROVIDER_H
#define PREVIEWPROVIDER_H

#include <QString>
#include <QByteArray>
#include <QFileInfo>
#include <QStringList>

class QWidget;

// ============================================================
//  文件预览模块 —— 预览提供者接口（低耦合核心）
//
//  设计目标：FilePreviewDialog 只面向本接口工作，完全不关心
//  “图片 / PDF” 的具体渲染细节。新增一种可预览类型时，只需
//  再实现一个 PreviewProvider 子类并在工厂里登记，宿主对话框
//  与上层调用方（上传界面 / 资源详情界面）都无需改动。
//
//  约定：预览一律以【内存字节】为输入（loadData(QByteArray)），
//  既适配“资源详情”从服务端流入内存的数据（不落盘），也适配
//  “上传界面”本地文件读入内存的数据 —— 二者共用同一条渲染路径。
// ============================================================
class PreviewProvider
{
public:
    virtual ~PreviewProvider() = default;

    // 用内存中的完整文件字节加载内容；解析成功返回 true，失败（数据损坏/格式不符）返回 false。
    virtual bool loadData(const QByteArray &data) = 0;

    // 返回承载预览内容的可视控件。Provider 子类同时继承 QWidget，
    // 故通常直接 return this；生命周期交由其 parent（宿主对话框）管理。
    virtual QWidget *widget() = 0;
};

// ============================================================
//  预览类型判定 —— 仅按扩展名识别，供宿主对话框选择 Provider，
//  也供上层在点击“预览”前做友好提示（isPreviewable）。
// ============================================================
namespace PreviewSupport
{
enum class Type {
    Unsupported,   // 不支持预览
    Image,         // 图片
    Pdf            // PDF
};

// 支持的图片扩展名（小写，不含点）
inline const QStringList &imageSuffixes()
{
    static const QStringList s = {
        "png", "jpg", "jpeg", "bmp", "gif", "webp", "svg", "ico", "tif", "tiff"
    };
    return s;
}

// 按文件名（或路径）的扩展名判断预览类型
inline Type detect(const QString &fileName)
{
    const QString suffix = QFileInfo(fileName).suffix().toLower();
    if (suffix == QLatin1String("pdf"))
        return Type::Pdf;
    if (imageSuffixes().contains(suffix))
        return Type::Image;
    return Type::Unsupported;
}
} // namespace PreviewSupport

#endif // PREVIEWPROVIDER_H
