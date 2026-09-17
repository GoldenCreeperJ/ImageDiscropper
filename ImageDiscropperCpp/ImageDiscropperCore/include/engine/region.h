// ============================================================================
// 文件：include/engine/region.h
// 作用：定义引擎最基础的区域几何类型——轴对齐矩形区域 RectRegion 与区域种类
//       RegionKind，以及水平带 / 垂直带的构造工具。
// 分块依据：
//   - 本文件只承载“区域”这一数据概念（终稿 §3.2 术语表 / §3.3 基本形状），
//     不涉及切割线（cut_line.h）、区域集合（region_set.h）或网格（grid.h）。
//   - 坐标约定严格遵循终稿 §3.1：整数像素、原点左上、x 向右 y 向下、
//     区间左闭右开 [left, right) × [top, bottom)。
// 说明：本文件仅定义区域数据结构与平凡访问器，不含切割算法（切割线见 cut_line.h）。
// ============================================================================
#pragma once

#include <cstdint>

namespace idc::engine {

// ---------------------------------------------------------------------------
// RegionKind：区域种类，对应终稿 §3.3 的五种基本形状。
// 分块依据：一个枚举值 = 一种由切割线诱导出的区域形态。
// ---------------------------------------------------------------------------
enum class RegionKind {
    RECT,             // 中心矩形 Rect(x1, y1, x2, y2)
    HORIZONTAL_BAND,  // 水平带 HBand(y1, y2) = Rect(0, y1, W, y2)
    VERTICAL_BAND,    // 垂直带 VBand(x1, x2) = Rect(x1, 0, x2, H)
    CROSS,            // 十字区域 Cross = HBand ∪ VBand
    CORNERS,          // 四角区域 I \ Cross
};

// 将区域种类转换为可读字符串，便于日志与调试。
inline const char* regionKindName(const RegionKind k) {
    switch (k) {
        case RegionKind::RECT: return "RECT";
        case RegionKind::HORIZONTAL_BAND: return "HORIZONTAL_BAND";
        case RegionKind::VERTICAL_BAND: return "VERTICAL_BAND";
        case RegionKind::CROSS: return "CROSS";
        case RegionKind::CORNERS: return "CORNERS";
    }
    return "UNKNOWN";
}

// ---------------------------------------------------------------------------
// RectRegion：轴对齐矩形区域。
// 语义：像素归属统一为左闭右开，即包含 left/top、不含 right/bottom，
//       杜绝终稿 §4.3.5 所述“边界像素重复或丢失”。
// ---------------------------------------------------------------------------
struct RectRegion {
    int left{0};
    int top{0};
    int right{0};
    int bottom{0};

    constexpr RectRegion() = default;
    constexpr RectRegion(const int l, const int t, const int r, const int b)
        : left(l), top(t), right(r), bottom(b) {}

    // 区域宽度（right - left），可能为 0（退化区域）。
    constexpr int width() const { return right - left; }
    // 区域高度（bottom - top），可能为 0（退化区域）。
    constexpr int height() const { return bottom - top; }
    // 是否为空：宽或高 <= 0 即视为空块，导出时应跳过（§4.3.5 贴边选框）。
    constexpr bool empty() const { return width() <= 0 || height() <= 0; }
    // 像素面积；空区域面积为 0。使用 long long 防止大图溢出。
    constexpr long long area() const {
        return empty() ? 0LL : static_cast<long long>(width()) * height();
    }
    // 判断整数像素坐标是否落在区域内（左闭右开）。
    constexpr bool contains(const int x, const int y) const {
        return x >= left && x < right && y >= top && y < bottom;
    }
    constexpr bool operator==(const RectRegion& o) const {
        return left == o.left && top == o.top && right == o.right && bottom == o.bottom;
    }
    constexpr bool operator!=(const RectRegion& o) const { return !(*this == o); }
};

// 构造水平带：横贯整幅图像（宽 = imageWidth），纵向区间 [y1, y2)。
constexpr RectRegion horizontalBand(const int y1, const int y2, const int imageWidth) {
    return RectRegion(0, y1, imageWidth, y2);
}

// 构造垂直带：纵贯整幅图像（高 = imageHeight），横向区间 [x1, x2)。
constexpr RectRegion verticalBand(const int x1, const int x2, const int imageHeight) {
    return RectRegion(x1, 0, x2, imageHeight);
}

} // namespace idc::engine
