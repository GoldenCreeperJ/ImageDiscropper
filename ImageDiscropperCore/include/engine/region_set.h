// ============================================================================
// 文件：include/engine/region_set.h
// 作用：定义区域集合 RegionSet 与区域片段 Fragment——切割结果的容器。
// 分块依据：
//   - Fragment 对应终稿 §3.2“区域片段”：切割后产生的每一个独立矩形像素块。
//   - RegionSet 是若干 RectRegion 的有序集合，用于承载 split / remove 的产出，
//     也是导出（§5）阶段的输入。
//   - 仅承载“集合”语义（增删查、面积/包围盒统计），不含像素搬运逻辑。
// 说明：接口骨架，全部为平凡数据结构操作，可内联实现。
// ============================================================================
#pragma once

#include <algorithm>
#include <cstddef>
#include <vector>

#include "engine/region.h"

namespace idc::engine {

// ---------------------------------------------------------------------------
// Fragment：区域片段（一个矩形块 + 序号 + 种类）。
// index 用于分离导出的命名与重排排序；kind 标注其形态（中心/带/角等）。
// ---------------------------------------------------------------------------
struct Fragment {
    RectRegion region;                 // 片段的矩形范围
    int index{-1};                     // 序号（-1 表示尚未编号）
    RegionKind kind{RegionKind::RECT}; // 片段种类
};

// ---------------------------------------------------------------------------
// RegionSet：区域集合。
// ---------------------------------------------------------------------------
class RegionSet {
public:
    // 追加一个区域到集合末尾。
    void add(const RectRegion& r) { regions_.push_back(r); }
    // 清空集合。
    void clear() { regions_.clear(); }
    // 区域数量。
    std::size_t size() const { return regions_.size(); }
    // 集合是否为空。
    bool empty() const { return regions_.empty(); }

    // 只读 / 可写访问底层区域列表。
    const std::vector<RectRegion>& regions() const { return regions_; }
    std::vector<RectRegion>& regions() { return regions_; }

    // 下标访问（调用方保证索引合法）。
    const RectRegion& operator[](const std::size_t i) const { return regions_[i]; }

    // 迭代器透传，便于范围 for 遍历。
    auto begin() const { return regions_.begin(); }
    auto end() const { return regions_.end(); }

    // 所有区域的像素面积之和（用于 §6“保留区域为空”判定与统计）。
    long long totalArea() const {
        long long sum = 0;
        for (const auto& r : regions_) sum += r.area();
        return sum;
    }

    // 计算集合的整体包围盒；空集合返回全 0 区域。
    RectRegion boundingBox() const {
        if (regions_.empty()) return RectRegion{};
        int l = regions_[0].left, t = regions_[0].top;
        int r = regions_[0].right, b = regions_[0].bottom;
        for (const auto& g : regions_) {
            l = std::min(l, g.left);
            t = std::min(t, g.top);
            r = std::max(r, g.right);
            b = std::max(b, g.bottom);
        }
        return RectRegion(l, t, r, b);
    }

private:
    std::vector<RectRegion> regions_;
};

} // namespace idc::engine
