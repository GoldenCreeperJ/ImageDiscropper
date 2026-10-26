// ============================================================================
// 文件：util/image_qt_adapter.cpp
// 作用：实现 core::Image → QImage/QPixmap 的深拷贝转换（见同名头文件说明）。
// 分块依据：仅两个自由函数，无状态；把「Core 像素布局 → Qt 像素布局」的映射
//           集中在此，其余 GUI 代码不直接触碰 Image::data() 字节细节。
// ============================================================================
#include "util/image_qt_adapter.h"

namespace idc::gui {

// 将 core::Image 转换为 QImage（深拷贝）。
QImage toQImage(const core::Image& img) {
    // 空图直接返回 null QImage，调用方据 isNull() 判断。
    if (img.empty()) {
        return {};
    }

    const int w = img.width();
    const int h = img.height();
    const auto* bytes = img.data();

    // Core Image 为行主序、紧凑排列（无行填充），故 bytesPerLine = 宽 × 每像素字节数。
    switch (img.format()) {
        case core::ImageFormat::Rgba: {
            // 构造只引用 Core 缓冲的临时 QImage，随即 .copy() 深拷贝脱离该缓冲。
            const QImage view(bytes, w, h, w * 4, QImage::Format_RGBA8888);
            return view.copy();
        }
        case core::ImageFormat::Rgb: {
            const QImage view(bytes, w, h, w * 3, QImage::Format_RGB888);
            return view.copy();
        }
        case core::ImageFormat::Gray: {
            const QImage view(bytes, w, h, w, QImage::Format_Grayscale8);
            return view.copy();
        }
    }
    return {};
}

// 将 core::Image 转换为 QPixmap：以**零拷贝 QImage 视图**直喂 QPixmap::fromImage（其内部深拷贝进
//   平台 pixmap，视图随调用结束失效）——免 toQImage 的 view.copy() 一份全图临时拷贝（仅省转换峰值）。
QPixmap toPixmap(const core::Image& img) {
    if (img.empty()) {
        return {};
    }

    const int w = img.width();
    const int h = img.height();
    const auto* bytes = img.data();

    // Core Image 为行主序、紧凑排列（无行填充），故 bytesPerLine = 宽 × 每像素字节数。
    switch (img.format()) {
        case core::ImageFormat::Rgba:
            return QPixmap::fromImage(QImage(bytes, w, h, w * 4, QImage::Format_RGBA8888));
        case core::ImageFormat::Rgb:
            return QPixmap::fromImage(QImage(bytes, w, h, w * 3, QImage::Format_RGB888));
        case core::ImageFormat::Gray:
            return QPixmap::fromImage(QImage(bytes, w, h, w, QImage::Format_Grayscale8));
    }
    return {};
}

} // namespace idc::gui
