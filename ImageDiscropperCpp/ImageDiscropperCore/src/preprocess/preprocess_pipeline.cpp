// ============================================================================
// 文件：src/preprocess/preprocess_pipeline.cpp
// 作用：实现 PreprocessPipeline 的步骤管理与执行逻辑。
//       applyOne 是核心分派函数：按 PreprocessConfig 的实际类型调用
//       pixel_ops 模块中的对应算法，返回新图像。
// ============================================================================
#include "preprocess/preprocess_pipeline.h"

#include <algorithm>
#include <type_traits>

#include "pixel_ops/color_ops.h"
#include "pixel_ops/geometric_ops.h"

namespace idc::preprocess {

// ---------------------------------------------------------------------------
// 公共接口：对一张图像应用单个操作，返回结果
// 分块依据：每种 PreprocessConfig 子类对应一个 if constexpr 分支，
//          由 std::visit 在编译期完成类型分派，避免运行时 switch。
// ---------------------------------------------------------------------------
core::Image applyOne(const core::Image& src, const PreprocessConfig& cfg) {
    return std::visit([&](const auto& op) -> core::Image {
        using T = std::decay_t<decltype(op)>;

        if constexpr (std::is_same_v<T, GrayOp>) {
            return pixel_ops::toGray(src);
        }
        else if constexpr (std::is_same_v<T, SplitOp>) {
            return pixel_ops::splitChannels(src, op.mask);
        }
        else if constexpr (std::is_same_v<T, InvertOp>) {
            return pixel_ops::invertChannels(src, op.mask);
        }
        else if constexpr (std::is_same_v<T, RotateOp>) {
            return pixel_ops::rotate(src, op.angle);
        }
        else if constexpr (std::is_same_v<T, FlipOp>) {
            return pixel_ops::flip(src, op.horizontal);
        }
        else {
            return src.clone();
        }
    }, cfg);
}

// ---------------------------------------------------------------------------
// PreprocessPipeline：步骤管理
// ---------------------------------------------------------------------------

// 追加步骤到队尾。
void PreprocessPipeline::add(PreprocessConfig cfg) {
    steps_.push_back(std::move(cfg));
}

// 在指定位置插入；index 超出范围时追加到队尾。
void PreprocessPipeline::insert(const std::size_t index, PreprocessConfig cfg) {
    if (index >= steps_.size()) steps_.push_back(std::move(cfg));
    else steps_.insert(steps_.begin() + static_cast<std::ptrdiff_t>(index), std::move(cfg));
}

// 删除指定位置步骤；越界忽略。
void PreprocessPipeline::remove(const std::size_t index) {
    if (index >= steps_.size()) return;
    steps_.erase(steps_.begin() + static_cast<std::ptrdiff_t>(index));
}

// 上移指定位置步骤；已在首位时忽略。
void PreprocessPipeline::moveUp(const std::size_t index) {
    if (index == 0 || index >= steps_.size()) return;
    std::swap(steps_[index], steps_[index - 1]);
}

// 清空所有步骤。
void PreprocessPipeline::clear() { steps_.clear(); }

// 按顺序应用所有步骤到 src，返回结果图像。
core::Image PreprocessPipeline::apply(const core::Image& src) const {
    core::Image cur = src.clone();
    for (const auto& cfg : steps_) {
        cur = applyOne(cur, cfg);
    }
    return cur;
}

} // namespace idc::preprocess
