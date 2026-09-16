// ============================================================================
// 文件：include/engine/selection.h
// 作用：定义极性 Polarity 与选择集 Selection——Grid-Selection-Emit 阶段 ③④
//       （终稿 §2）的数据载体，也是 L1/L2/L3 三层共用“保留 / 剔除”开关的落点。
// 分块依据：
//   - Polarity：keep / remove 二元极性（§3.2 术语），L2 反向剔除的核心即此开关。
//   - Selection：被选中的单元格序号集合 + 极性；提供点选 / 框选 / 全选 / 反选
//     等集合操作（FR-L3.3），以及“按极性解析出最终保留集”的 resolve 接口。
//   - resolve 按极性取补集解析保留序号，真实实现见 src/engine/selection.cpp。
//   - applyPolarity：阶段③④ 自由函数，把解析结果作用于区域全集过滤出保留集 R（声明由
//     engine.h facade 下沉至此，使 selection.cpp 只依赖本头）。
// 说明：Selection 只承载选择状态与极性；像素级剔除由 split / applyPolarity / compose 完成。
// ============================================================================
#pragma once

#include <cstddef>
#include <vector>

#include "engine/region_set.h"

namespace idc::engine {

// ---------------------------------------------------------------------------
// Polarity：极性（终稿 §3.2 / §2 阶段④）。
// ---------------------------------------------------------------------------
enum class Polarity {
    KEEP,    // 保留选择集：R = S
    REMOVE,  // 剔除选择集：R = 全集 \ S（反向剔除，L2 内核）
};

// ---------------------------------------------------------------------------
// Selection：选择集。
// 内部以“是否被选中”的布尔向量表示，配合极性即可推导出保留集。
// ---------------------------------------------------------------------------
class Selection {
public:
    // 设置极性；一键在“保留框内 / 删除框内”之间切换（FR-L2.5）。
    void setPolarity(const Polarity p) { polarity_ = p; }
    // 读取当前极性。
    Polarity polarity() const { return polarity_; }

    // 将选择位向量重置为 cellCount 个“未选中”。
    void resize(const std::size_t cellCount) { selected_.assign(cellCount, false); }

    // 选中 / 取消选中 / 切换指定序号的单元。
    void select(const std::size_t index) { setFlag(index, true); }
    void deselect(const std::size_t index) { setFlag(index, false); }
    void toggle(const std::size_t index) {
        if (index < selected_.size()) selected_[index] = !selected_[index];
    }

    // 全选 / 全不选 / 反选（FR-L3.3）。
    // 注意：std::vector<bool> 迭代解引用返回 _Vb_reference 代理（右值），不能绑定
    //       auto& 非 const 引用；整体操作改用向量特化的 assign / flip()。
    void selectAll() { selected_.assign(selected_.size(), true); }
    void deselectAll() { selected_.assign(selected_.size(), false); }
    void invert() { selected_.flip(); }

    // 单元总数与被选中数量。
    std::size_t cellCount() const { return selected_.size(); }
    std::size_t selectedCount() const {
        std::size_t n = 0;
        for (const bool b : selected_) if (b) ++n;
        return n;
    }

    // 只读访问选择位向量。
    const std::vector<bool>& flags() const { return selected_; }

    // 按当前极性解析出“最终保留”的单元序号集合：
    //   KEEP   → 返回所有被选中的序号；
    //   REMOVE → 返回所有未被选中的序号（补集）。
    // 实现见 src/engine/selection.cpp。
    std::vector<std::size_t> resolve() const;

private:
    // 内部工具：安全设置某序号的选中标志。
    void setFlag(const std::size_t index, const bool value) {
        if (index < selected_.size()) selected_[index] = value;
    }

    Polarity polarity_{Polarity::KEEP};
    std::vector<bool> selected_;
};

// ③④ 依据选择集与极性，从全集 all 过滤出最终保留集 R（终稿 §2 阶段③④）：
//   keep → R = S；remove → R = 全集 \ S。定义见 src/engine/selection.cpp。
RegionSet applyPolarity(const RegionSet& all, const Selection& selection);

} // namespace idc::engine
