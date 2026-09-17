// ============================================================================
// 文件：include/preprocess/preprocess_pipeline.h
// 作用：按顺序将一组 PreprocessConfig 应用到 core::Image，得到最终结果。
// ============================================================================
#pragma once

#include <vector>

#include "core/image.h"
#include "preprocess/preprocess_config.h"

namespace idc::preprocess {

// ---------------------------------------------------------------------------
// PreprocessPipeline：操作流水线
// 使用方式：
//   PreprocessPipeline pipe;
//   pipe.add(GrayOp{});
//   pipe.add(RotateOp{90});
//   core::Image out = pipe.apply(src);
// ---------------------------------------------------------------------------
class PreprocessPipeline {
public:
    // 追加一个操作步骤到队尾。
    void add(PreprocessConfig cfg);

    // 在指定位置插入一个操作步骤；index 超出范围时追加到队尾。
    void insert(std::size_t index, PreprocessConfig cfg);

    // 删除指定位置的操作步骤；index 越界时忽略。
    void remove(std::size_t index);

    // 上移指定位置的操作步骤（用于调整执行顺序）；已在首位时忽略。
    void moveUp(std::size_t index);

    // 清空所有步骤。
    void clear();

    // 当前步骤数量。
    std::size_t size() const { return steps_.size(); }

    // 只读访问步骤列表。
    const std::vector<PreprocessConfig>& steps() const { return steps_; }

    // 将流水线应用到 src，返回结果图像。不修改 src。
    core::Image apply(const core::Image& src) const;

private:
    std::vector<PreprocessConfig> steps_;
};

// 便捷函数：直接对一张图像应用单个操作，返回结果。
core::Image applyOne(const core::Image& src, const PreprocessConfig& cfg);

} // namespace idc::preprocess
