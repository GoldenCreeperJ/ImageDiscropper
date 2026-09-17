// ============================================================================
// 文件：src/annotation/view_transform.cpp
// 作用：实现 include/annotation/view_transform.h 声明的视图坐标变换。
// ============================================================================
#include "annotation/view_transform.h"

#include <algorithm>
#include <cmath>

namespace idc::annotation {

// 构造：默认 scale = 1、offset = (0, 0)、范围 [0.05, 10.0]、步长 0.1。
ViewTransform::ViewTransform() = default;

// 设置缩放倍率，自动 clamp 到 [minScale_, maxScale_]。
void ViewTransform::setScale(const double s) {
    scale_ = std::clamp(s, minScale_, maxScale_);
}

// 设置缩放范围，若 mn > mx 则交换。
void ViewTransform::setScaleRange(double mn, double mx) {
    if (mn > mx) std::swap(mn, mx);
    minScale_ = mn;
    maxScale_ = mx;
    scale_ = std::clamp(scale_, minScale_, maxScale_);
}

// 屏幕坐标 → 逻辑坐标：(screen - offset) / scale。
core::Point2D ViewTransform::screenToLogical(const core::Point2D& screen) const {
    return {(screen.x - offset_.x) / scale_, (screen.y - offset_.y) / scale_};
}

// 逻辑坐标 → 屏幕坐标：logical * scale + offset。
core::Point2D ViewTransform::logicalToScreen(const core::Point2D& logical) const {
    return {logical.x * scale_ + offset_.x, logical.y * scale_ + offset_.y};
}

// 以 anchor 为中心缩放：保持 anchor 处的逻辑坐标不变，调整 scale 与 offset。
void ViewTransform::scaleAboutPoint(const double wheelRotation, const core::Point2D& anchor) {
    // 缩放前 anchor 对应的逻辑坐标
    const core::Point2D oldLogical = screenToLogical(anchor);
    // 计算新 scale（wheelRotation > 0 时缩小，< 0 时放大）
    const double newScale = std::clamp(scale_ - wheelRotation * scaleStep_, minScale_, maxScale_);
    // 保留两位小数（四舍五入），避免缩放值出现过长的浮点尾数
    scale_ = std::round(newScale * 100.0) / 100.0;
    // 反解新 offset，使得 anchor 仍然对应 oldLogical
    offset_ = {anchor.x - oldLogical.x * scale_, anchor.y - oldLogical.y * scale_};
}

// 将图像适配到视口：scale = min(vw/iw, vh/ih)，offset 居中。
void ViewTransform::fitToViewport(const int imageWidth, const int imageHeight,
                                  const int viewportWidth, const int viewportHeight) {
    if (imageWidth <= 0 || imageHeight <= 0) {
        scale_ = 1.0;
        offset_ = {0.0, 0.0};
        return;
    }
    const double sx = static_cast<double>(viewportWidth) / imageWidth;
    const double sy = static_cast<double>(viewportHeight) / imageHeight;
    const double s = std::clamp(std::min(sx, sy), minScale_, maxScale_);
    // 保留两位小数
    scale_ = std::round(s * 100.0) / 100.0;
    offset_ = {(viewportWidth - imageWidth * scale_) / 2.0,
               (viewportHeight - imageHeight * scale_) / 2.0};
}

} // namespace idc::annotation
