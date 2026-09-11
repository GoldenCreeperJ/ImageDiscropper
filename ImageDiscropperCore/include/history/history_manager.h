// ============================================================================
// 文件：include/history/history_manager.h
// 作用：通用撤销 / 重做栈模板 HistoryManager<T>。
// 说明：模板实现直接放在头文件中，避免额外实例化文件。
// ============================================================================
#pragma once

#include <cstddef>
#include <optional>
#include <vector>

namespace idc::history {

// ---------------------------------------------------------------------------
// HistoryManager<T>：泛型撤销 / 重做栈
// 语义：
//   - push(item)           : 追加一个新条目，同时清空重做栈（新操作会切断"未来"）
//   - popToRedo()          : 弹出主栈栈顶到重做栈，返回该条目（撤销）
//   - popFromRedo()        : 从重做栈弹回到主栈，返回该条目（重做）
//   - clear()/clearRedo()  : 清空对应栈
// ---------------------------------------------------------------------------
template <typename T>
class HistoryManager {
public:
    HistoryManager() = default;

    // 追加一个条目；同时清空重做栈，对应"新操作切断未来"的常见语义。
    void push(T item) {
        undoStack_.push_back(std::move(item));
        redoStack_.clear();
    }

    // 撤销：把主栈栈顶搬到重做栈，返回该条目的副本；主栈为空返回 nullopt。
    std::optional<T> popToRedo() {
        if (undoStack_.empty()) return std::nullopt;
        redoStack_.push_back(std::move(undoStack_.back()));
        undoStack_.pop_back();
        return redoStack_.back();
    }

    // 重做：把重做栈栈顶搬回主栈，返回该条目的副本；重做栈为空返回 nullopt。
    std::optional<T> popFromRedo() {
        if (redoStack_.empty()) return std::nullopt;
        undoStack_.push_back(std::move(redoStack_.back()));
        redoStack_.pop_back();
        return undoStack_.back();
    }

    // 查看主栈栈顶（不弹出）；主栈为空返回 nullptr。
    const T* top() const {
        return undoStack_.empty() ? nullptr : &undoStack_.back();
    }

    // 主栈与重做栈是否非空。
    bool canUndo() const { return !undoStack_.empty(); }
    bool canRedo() const { return !redoStack_.empty(); }

    // 栈大小。
    std::size_t undoSize() const { return undoStack_.size(); }
    std::size_t redoSize() const { return redoStack_.size(); }

    // 只读访问整个主栈。
    const std::vector<T>& undoStack() const { return undoStack_; }
    // 只读访问整个重做栈。
    const std::vector<T>& redoStack() const { return redoStack_; }

    // 清空主栈（保留重做栈）。
    void clear() { undoStack_.clear(); }
    // 清空重做栈（保留主栈）。
    void clearRedo() { redoStack_.clear(); }
    // 同时清空两个栈。
    void clearAll() { undoStack_.clear(); redoStack_.clear(); }

private:
    std::vector<T> undoStack_;
    std::vector<T> redoStack_;
};

} // namespace idc::history
