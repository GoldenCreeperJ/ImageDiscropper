// ============================================================================
// 文件：include/pixel_ops/geometric_ops.h
// 作用：几何变换类算法。
// ============================================================================
#pragma once

#include "core/image.h"

namespace idc::pixel_ops {

// 旋转：只支持 90 的整数倍（90 / 180 / 270），其他角度返回未旋转的副本。
// 参数 angle 单位为度，允许负值（内部会归一化到 [0, 360)）。
core::Image rotate(const core::Image& src, int angle);

// 翻转：horizontal = true 为左右翻转，false 为上下翻转。
core::Image flip(const core::Image& src, bool horizontal = true);

// 缩放算法。
enum class ResampleMode {
    NEAREST,    // 最近邻，速度快，锯齿明显
    BILINEAR,   // 双线性插值，速度中等，效果均衡
};

// 图像尺寸调整（缩放）：按目标宽高进行重采样。
core::Image resize(const core::Image& src, int newWidth, int newHeight,
                   ResampleMode mode = ResampleMode::BILINEAR);

// 按比例缩放：scale 必须 > 0。
core::Image scale(const core::Image& src, double scale,
                  ResampleMode mode = ResampleMode::BILINEAR);

} // namespace idc::pixel_ops
