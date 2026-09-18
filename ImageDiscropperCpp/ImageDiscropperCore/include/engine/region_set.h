// ============================================================================
// 文件：include/engine/region_set.h
// 作用：定义区域集合 RegionSet 与区域片段 Fragment——切割结果的容器。
// 分块依据：
//   - Fragment 对应 SPEC §2.2“区域片段”：切割后产生的每一个独立矩形像素块。
//   - RegionSet 是若干 RectRegion 的有序集合，用于承载 split / remove 的产出，
//     也是导出（SPEC §4）阶段的输入。
//   - 仅承载“集合”语义（增删查、面积/包围盒统计），不含像素搬运逻辑。
// 说明：RegionSet 以 Fragment（区域 + 序号 + 种类）为元素，使单元序号能贯穿
//       split → applyPolarity → compose 全流程（坍缩位置映射与重排排序均依赖）。
// ============================================================================
#pragma once

#include <algorithm>
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
    int row{-1};                       // 源网格行号（-1 表示非网格来源；供 {row} 命名）
    int col{-1};                       // 源网格列号（-1 表示非网格来源；供 {col} 命名）
};

// ---------------------------------------------------------------------------
// RegionSet：区域集合。
// ---------------------------------------------------------------------------
class RegionSet {
public:
    // 追加一个区域（兼容接口）：序号 -1、种类 RECT、行列 -1。
    void add(const RectRegion& r) { fragments_.push_back(Fragment{r, -1, RegionKind::RECT, -1, -1}); }
    // 追加一个带序号 / 种类 / 行列的片段（split 产出用，序号与行列贯穿后续合成与命名）。
    void add(const RectRegion& r, const int index, const RegionKind kind,
             const int row = -1, const int col = -1) {
        fragments_.push_back(Fragment{r, index, kind, row, col});
    }
    // 追加一个现成片段。
    void add(const Fragment& f) { fragments_.push_back(f); }
    // 清空集合。
    void clear() { fragments_.clear(); }
    // 片段数量。
    std::size_t size() const { return fragments_.size(); }
    // 集合是否为空。
    bool empty() const { return fragments_.empty(); }

    // 只读 / 可写访问底层片段列表。
    const std::vector<Fragment>& fragments() const { return fragments_; }
    std::vector<Fragment>& fragments() { return fragments_; }

    // 下标访问（调用方保证索引合法），返回片段。
    const Fragment& operator[](const std::size_t i) const { return fragments_[i]; }

    // 迭代器透传，便于范围 for 遍历片段。
    auto begin() const { return fragments_.begin(); }
    auto end() const { return fragments_.end(); }
    auto begin() { return fragments_.begin(); }
    auto end() { return fragments_.end(); }

    // 所有片段的像素面积之和（统计用；为 0 即保留区域为空，对应 SPEC §5 的 E-7）。
    long long totalArea() const {
        long long sum = 0;
        for (const auto& f : fragments_) sum += f.region.area();
        return sum;
    }

    // 计算集合的整体包围盒；空集合返回全 0 区域。
    RectRegion boundingBox() const {
        if (fragments_.empty()) return RectRegion{};
        int l = fragments_[0].region.left, t = fragments_[0].region.top;
        int r = fragments_[0].region.right, b = fragments_[0].region.bottom;
        for (const auto& f : fragments_) {
            l = std::min(l, f.region.left);
            t = std::min(t, f.region.top);
            r = std::max(r, f.region.right);
            b = std::max(b, f.region.bottom);
        }
        return RectRegion(l, t, r, b);
    }

private:
    std::vector<Fragment> fragments_;
};

} // namespace idc::engine
