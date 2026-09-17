// ============================================================================
// 文件：include/annotation/view_transform.h
// 作用：视图坐标变换工具，实现屏幕坐标 ↔ 逻辑坐标换算、以指定点为中心的
//       缩放、图像适配到视口等功能。
// ============================================================================
#pragma once

#include "core/point.h"

namespace idc::annotation {

// ---------------------------------------------------------------------------
// ViewTransform：视图变换状态
// 说明：屏幕坐标 = 逻辑坐标 * scale + offset
// ---------------------------------------------------------------------------
class ViewTransform {
public:
    ViewTransform();

    // 当前缩放倍率与偏移量。
    double scale() const { return scale_; }
    const core::Point2D& offset() const { return offset_; }

    // 设置缩放倍率，自动 clamp 到 [minScale, maxScale]。
    void setScale(double s);
    // 设置偏移量。
    void setOffset(const core::Point2D& o) { offset_ = o; }

    // 缩放上下限。
    double minScale() const { return minScale_; }
    double maxScale() const { return maxScale_; }
    void setScaleRange(double mn, double mx);

    // 屏幕坐标 → 逻辑坐标。
    core::Point2D screenToLogical(const core::Point2D& screen) const;
    // 逻辑坐标 → 屏幕坐标。
    core::Point2D logicalToScreen(const core::Point2D& logical) const;

    // 以屏幕坐标 anchor 为中心，按滚轮增量 wheelRotation 缩放。
    // wheelRotation 正值表示缩小、负值表示放大（沿用常见滚轮方向约定）。
    void scaleAboutPoint(double wheelRotation, const core::Point2D& anchor);

    // 将指定尺寸的图像适配到 viewport 中，居中放置。
    void fitToViewport(int imageWidth, int imageHeight,
                       int viewportWidth, int viewportHeight);

    // 单步步长（每次滚轮缩放改变的比例）。
    double scaleStep() const { return scaleStep_; }
    void setScaleStep(const double step) { scaleStep_ = step; }

private:
    double scale_{1.0};
    core::Point2D offset_{0.0, 0.0};
    double minScale_{0.05};
    double maxScale_{10.0};
    double scaleStep_{0.1};
};

} // namespace idc::annotation
