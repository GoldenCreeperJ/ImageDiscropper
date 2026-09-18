// ============================================================================
// 文件：util/image_qt_adapter.h
// 作用：Core 像素容器 idc::core::Image 与 Qt 图像类型（QImage/QPixmap）之间的
//       适配转换。这是 GUI 的「视图关注点」——把 Core 的纯数据翻译成 Qt 能绘制
//       的形式，不含任何切割/几何/像素算法（CONTRIBUTING.md「分层纪律」）。
// 分块依据：仅承载 Image→QImage/QPixmap 的单向转换；反向（QImage→Image）GUI 阶段
//           不需要（GUI 从不生产像素，只消费 Core 产物），故不提供，避免误用。
// 说明：转换一律「深拷贝」像素——Core 的 Image 缓冲生命周期独立于 Qt 控件，
//       若浅引用其 data() 指针，Image 析构后 QPixmap 将悬垂。
// ============================================================================
#pragma once

#include <QPixmap>

#include "core/image.h"

namespace idc::gui {

// 将 core::Image 转换为 QImage（深拷贝像素，脱离 Core 缓冲生命周期）。
// 按 Image::format() 选择 QImage 格式：RGBA→Format_RGBA8888、RGB→Format_RGB888、
// GRAY→Format_Grayscale8。空图返回默认构造的 null QImage。
QImage toQImage(const core::Image& img);

// 将 core::Image 转换为 QPixmap（经 toQImage）。用于 QGraphicsPixmapItem 显示。
QPixmap toPixmap(const core::Image& img);

} // namespace idc::gui
