// ============================================================================
// 文件：include/preprocess/preprocess_pipeline.h
// 作用：按顺序将一组 PreprocessConfig 应用到 core::Image，得到最终结果。
// ============================================================================
#pragma once

#include <utility>
#include <vector>

#include "core/image.h"
#include "core/pipeline_manager.h"
#include "preprocess/preprocess_config.h"

namespace idc::preprocess {

// ---------------------------------------------------------------------------
// PreprocessPipeline：操作流水线（**公开继承** core::Pipeline<PreprocessConfig>，
// is-a 建立：预处理管线就是一个「PreprocessConfig 的有序步骤队列」再加像素/几何语义）。
// 存储 + CRUD（add / insert / remove / moveUp / moveDown / clear / size / empty / items /
// begin / end / operator==）及「越界钳制 / 忽略」边界策略均由基类代劳；本类只保留
// 与 PreprocessConfig 语义绑定的方法（apply / applyExec / geometryOutput）与域内别名。
// 使用方式：
//   PreprocessPipeline pipe;
//   pipe.add(GrayOp{});
//   pipe.add(RotateOp{90});
//   core::Image out = pipe.apply(src);
// ---------------------------------------------------------------------------
class PreprocessPipeline : public core::Pipeline<PreprocessConfig> {
public:
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
};

// 便捷函数：直接对一张图像应用单个操作，返回结果。
core::Image applyOne(const core::Image& src, const PreprocessConfig& cfg);

} // namespace idc::preprocess
