// ============================================================================
// 文件：util/preview_scaler.cpp
// 作用：实现 makePreview——委托 Core processing::resize 生成降采样预览副本。
// 分块依据：GUI 不自实现重采样（A-0.3），只计算目标尺寸与放大系数，缩放交给 Core。
// ============================================================================
#include "util/preview_scaler.h"

#include <algorithm>
#include <cmath>

#include "processing/geometric_ops.h"

namespace idc::gui {

// 生成用于画布显示的降采样副本（详见头文件说明）。
PreviewImage makePreview(const idc::core::Image& src, const int maxDimension) {
    PreviewImage out;
    if (src.empty()) {
        return out; // 空图：系数保持 1.0，image 为空。
    }

    const int w = src.width();
    const int h = src.height();
    const int longest = std::max(w, h);

    // 无需缩放：最长边已在限制内（或限制非法），直接返回原图副本、系数 1.0。
    if (maxDimension <= 0 || longest <= maxDimension) {
        out.image = src; // Image 赋值即深拷贝。
        out.scaleX = 1.0;
        out.scaleY = 1.0;
        return out;
    }

    // 需缩放：按最长边等比缩到 maxDimension，最近邻（快、预览足够）。
    const double s = static_cast<double>(maxDimension) / static_cast<double>(longest);
    const int nw = std::max(1, static_cast<int>(std::llround(w * s)));
    const int nh = std::max(1, static_cast<int>(std::llround(h * s)));
    out.image = idc::processing::resize(src, nw, nh, idc::processing::ResampleMode::NEAREST);

    // 放大系数以「实际预览尺寸」为准（round 可能与理论值有 1px 级偏差）。
    out.scaleX = static_cast<double>(w) / static_cast<double>(out.image.width());
    out.scaleY = static_cast<double>(h) / static_cast<double>(out.image.height());
    return out;
}

} // namespace idc::gui
