// ============================================================================
// 文件：include/engine/selection.h
// 作用：定义极性 Polarity 与选择集 Selection——Grid-Selection-Emit 阶段 ③④
//       （终稿 §2）的数据载体，也是 L1/L2/L3 三层共用“保留 / 剔除”开关的落点。
// 分块依据：
//   - Polarity：keep / remove 二元极性（§3.2 术语），L2 反向剔除的核心即此开关。
//   - Selection：被选中的单元格序号集合 + 极性；提供点选 / 框选 / 全选 / 反选
//     等集合操作（FR-L3.3），以及“按极性解析出最终保留集”的 resolve 接口。
//   - resolve 属于待实现算法（依赖极性取补集），仅声明，桩实现见 selection.cpp。
// 说明：接口骨架，不实现像素级剔除。
// ============================================================================
#pragma once

#include <cstddef>
#include <vector>

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
    void selectAll() { for (auto& b : selected_) b = true; }
    void deselectAll() { for (auto& b : selected_) b = false; }
    void invert() { for (auto& b : selected_) b = !b; }

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
    // 声明占位，桩实现见 src/engine/selection.cpp，真正实现留待 MVP 阶段。
    std::vector<std::size_t> resolve() const;

private:
    // 内部工具：安全设置某序号的选中标志。
    void setFlag(const std::size_t index, const bool value) {
        if (index < selected_.size()) selected_[index] = value;
    }

    Polarity polarity_{Polarity::KEEP};
    std::vector<bool> selected_;
};

} // namespace idc::engine
