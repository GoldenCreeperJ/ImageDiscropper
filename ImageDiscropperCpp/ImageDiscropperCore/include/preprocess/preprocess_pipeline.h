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

    // 返回一个**逐像素语义等价、但趟数更少**的折叠流水线（不修改本对象）——对**连续同类段**各自就地规整，段间绝不跨越：
    //   · 连续几何步（RotateOp / FlipOp）→ 规范 ≤2 op（1 RotateOp + 至多 1 FlipOp）：连续 rotate/flip 复合是纯像素
    //     位置置换（D4），折叠后逐像素与逐尺寸恒等；
    //   · 连续色彩步（GrayOp / SplitOp / InvertOp）→ 规范形：首个 gray 前的 Split/Invert 折成至多 Split+Invert（逐通道
    //     位映射 f(v)=(v&K)^J）、gray 幂等（重复丢弃）、gray 后 Split 死步丢弃且 Invert 按奇偶折成至多 1 步；纯位运算 +
    //     取整点不变，故逐像素恒等；
    //   · ResizeOp 及各段之间 **原地保留、绝不跨越重排**（resize 双线性与 gray 截断使色彩/几何并非总能交换）。
    // 注：仅减少同类连续段的重放趟数与死步，不改变 apply/geometryOutput 的结果；调用方按需选择是否走折叠。
    // 前提：灰度态在整个流水线内单调不回退（几何 / resize 均保持灰度），且输入图像为多通道（载入器强解 RGBA），
    //       pre-gray 位映射合并据此成立；若直接喂入 GRAY 图作为起点则超出该前提（不属正常使用）。
    PreprocessPipeline folded() const;

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
