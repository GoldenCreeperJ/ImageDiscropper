// ============================================================================
// 文件：include/core/pipeline_manager.h
// 作用：通用「声明式有序步骤队列」模板 Pipeline<T>——把预处理流水线
//       （PreprocessPipeline）反复用到的「按序增 / 删 / 移 / 改 + 边界钳制」这套
//       与元素类型无关的容器语义抽成单头泛型叶子，供任意 T 复用（现消费者：preprocess）。
// 设计：与 HistoryManager<T> 同构——纯模板头实现，语义只落在 std::vector 的下标操作与
//       「越界钳制 / 忽略」边界策略上，绝不含任何 T 专属逻辑（像素 / 几何推演留在消费者侧）。
//       消费者（PreprocessPipeline）**公开继承**本容器：存储与 CRUD 骨架直接作为其公开接口的一
//       部分，自身只追加 apply / applyExec / geometryOutput 等像素/几何语义方法（LSP 成立：
//       Core 侧无跨方法守卫不变式，单 vector 边界策略对任意消费者均适用）。
// 说明：模板实现直接放在头文件中，避免额外实例化文件。
// ============================================================================
#pragma once

#include <cstddef>
#include <utility>
#include <vector>

namespace idc::core {

// ---------------------------------------------------------------------------
// Pipeline<T>：声明式有序步骤队列（下标可编辑、可重排的轻量容器）。
// 边界策略（消费者 PreprocessPipeline 沿用至今、逐字复刻，是其「非空壳」的核心价值）：
//   - add(item)             : 追加到队尾。
//   - insert(index, item)   : 在 index 处插入；index 越界（>= size）钳制为「追加到队尾」。
//   - remove(index)         : 删除 index 处；index 越界则忽略（不抛、不动）。
//   - replaceAt(index, item): 就地替换 index 处（编辑参数）；index 越界则忽略。
//   - moveUp(index)         : 与上一项换序；已在首位（index==0）或越界则忽略。
//   - moveDown(index)       : 与下一项换序；已在末位或越界则忽略。
//   - clear()               : 清空全部条目。
// 备注：moveUp/moveDown 只做「相邻同类无关」的单格换序，不感知任何跨元素不变式
//       （geo→color 分区那类不变式属消费者 / 上层会话职责，本容器只负责机械换序）。
// ---------------------------------------------------------------------------
template <typename T>
class Pipeline {
public:
    Pipeline() = default;

    // 追加一个步骤到队尾。
    void add(T item) { items_.push_back(std::move(item)); }

    // 在指定位置插入；index 超出范围时钳制为追加到队尾。
    void insert(const std::size_t index, T item) {
        if (index >= items_.size()) items_.push_back(std::move(item));
        else items_.insert(items_.begin() + static_cast<std::ptrdiff_t>(index), std::move(item));
    }

    // 删除指定位置；index 越界时忽略。
    void remove(const std::size_t index) {
        if (index >= items_.size()) return;
        items_.erase(items_.begin() + static_cast<std::ptrdiff_t>(index));
    }

    // 就地替换指定位置（用于编辑已有条目的参数）；index 越界时忽略。
    void replaceAt(const std::size_t index, T item) {
        if (index >= items_.size()) return;
        items_[index] = std::move(item);
    }

    // 上移指定位置（调整顺序）；已在首位或越界时忽略。
    void moveUp(const std::size_t index) {
        if (index == 0 || index >= items_.size()) return;
        std::swap(items_[index], items_[index - 1]);
    }

    // 下移指定位置（调整顺序）；已在末位或越界时忽略。
    void moveDown(const std::size_t index) {
        if (index + 1 >= items_.size()) return;
        std::swap(items_[index], items_[index + 1]);
    }

    // 清空所有条目。
    void clear() { items_.clear(); }

    // 当前条目数量 / 是否为空。
    std::size_t size() const { return items_.size(); }
    bool empty() const { return items_.empty(); }

    // 只读访问条目列表（消费者据此渲染、迭代、比较；下标语义与外部扁平下标一致）。
    const std::vector<T>& items() const { return items_; }

    // 便捷区间访问（供范围 for 直接迭代本容器）。
    typename std::vector<T>::const_iterator begin() const { return items_.begin(); }
    typename std::vector<T>::const_iterator end() const { return items_.end(); }

    // 逐条相等（依赖 T 的 operator==）：供前缀失效比对、历史快照一致性等。
    bool operator==(const Pipeline& other) const { return items_ == other.items_; }

private:
    std::vector<T> items_;
};

} // namespace idc::core
