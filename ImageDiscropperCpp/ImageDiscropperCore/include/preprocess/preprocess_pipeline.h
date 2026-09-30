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

    // 就地替换指定位置的操作步骤（用于编辑已有步骤的参数）；index 越界时忽略。
    void setStep(std::size_t index, PreprocessConfig cfg);

    // 上移指定位置的操作步骤（用于调整执行顺序）；已在首位时忽略。
    void moveUp(std::size_t index);

    // 下移指定位置的操作步骤（用于调整执行顺序）；已在末位时忽略。
    void moveDown(std::size_t index);

    // 清空所有步骤。
    void clear();

    // 当前步骤数量。
    std::size_t size() const { return steps_.size(); }

    // 队列是否为空（无任何步骤）。
    bool empty() const { return steps_.empty(); }

    // 只读访问步骤列表。
    const std::vector<PreprocessConfig>& steps() const { return steps_; }

    // 两条队列逐步相等（依赖 PreprocessConfig 各 op 的 operator==）：用于 base_ 前缀失效比对等。
    bool operator==(const PreprocessPipeline& other) const { return steps_ == other.steps_; }

    // 将流水线应用到 src，返回结果图像。不修改 src。
    core::Image apply(const core::Image& src) const;

    // 执行层融合入口（与 apply 逐像素等价，含尺寸/格式）：先把队列压成内部 ExecStep 计划
    // 再单趟执行——色彩步折成单次位映射 f(v)=(v&K)^J（channelMap 融合核），几何步折成单次
    // D4 重排（remapD4 融合核），灰度化幂等去重，灰度态反色按奇偶合并；像素趟数恒 ≤ 块数×4，
    // 与队列长度解耦。块界唯一为 ResizeOp（双线性取整与逐像素映射/置换不可交换）；色彩与几何可跨
    // gray 合并（逐像素字节映射与精确置换互为可交换，几何无损前提是 GRAY 搬运单字节直拷）。
    // ExecStep 为 cpp 内部类型：不进 PreprocessConfig / schema / 序列化 / GUI 词表，对外仅增本方法。
    // 注：apply 逐步语义保留不动，充当本方法的等价性参照 oracle；旧声明层折叠入口 folded() 已删除，
    //     折叠口径统一收敛到本方法（执行层），不再保留两套折叠。
    core::Image applyExec(const core::Image& src) const;

    // 几何推演结果（纯元数据，不碰像素）：供 GUI 在请求期计算几何签名。
    struct GeometryOut { int width; int height; int rotQuad; };

    // 给定输入尺寸，按各步语义推演输出尺寸与净旋转象限：
    //   width/height —— resize 设绝对值、rotate 90/270 交换宽高，gray/invert/split/flip 保持；
    //   rotQuad     —— 所有 rotate 角度累加后 mod 4（0..3），忽略 flip（不入签名）。
    GeometryOut geometryOutput(int width, int height) const;

private:
    std::vector<PreprocessConfig> steps_;
};

// 便捷函数：直接对一张图像应用单个操作，返回结果。
core::Image applyOne(const core::Image& src, const PreprocessConfig& cfg);

} // namespace idc::preprocess
