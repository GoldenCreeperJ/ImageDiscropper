// ============================================================================
// 文件：util/preview_scaler.h
// 作用：为画布显示生成「降采样预览副本」（NFR-3 / G-14）——8000×8000 大图直接
//       渲染会拖垮交互，故显示时用最长边受限的缩小副本，导出时才回到原分辨率。
// 分块依据：只负责「生成预览图 + 给出预览→原图的放大系数」，不含任何切割/几何；
//           缩放本身委托 Core 的 pixel_ops::resize（不重复造轮子，A-0.3）。
// 说明：画布场景坐标统一采用「原图像素坐标」；底图 item 用这里的放大系数把小的
//       预览 pixmap 变换到原图尺寸，从而遮罩/切割线/选区都能按原图坐标直接叠加。
// ============================================================================
#pragma once

#include "core/image.h"

namespace idc::gui {

// 预览结果：降采样副本 + 预览→原图的放大系数（原图尺寸 / 预览尺寸）。
struct PreviewImage {
    core::Image image;   // 预览图（scale==1 时为原图副本）
    double scaleX{1.0};       // 横向放大系数：原图宽 / 预览宽
    double scaleY{1.0};       // 纵向放大系数：原图高 / 预览高
};

// 生成用于画布显示的降采样副本：最长边不超过 maxDimension 时原样返回（系数 1.0）；
// 否则按最近邻缩放到最长边 = maxDimension。maxDimension <= 0 视为不缩放。
PreviewImage makePreview(const core::Image& src, int maxDimension);

} // namespace idc::gui
