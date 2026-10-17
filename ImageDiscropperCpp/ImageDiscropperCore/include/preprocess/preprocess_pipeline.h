// ============================================================================
// 文件：include/preprocess/preprocess_pipeline.h
// 作用：按顺序将一组 PreprocessConfig 应用到 core::Image，得到最终结果。
// ============================================================================
#pragma once

#include <utility>
#include <vector>

#include "core/image.h"
#include "core/pipeline.h"
#include "preprocess/preprocess_config.h"

namespace idc::preprocess {

// ---------------------------------------------------------------------------
// PreprocessPipeline：操作流水线（**公开继承** core::Pipeline<PreprocessConfig>，
// is-a 建立：预处理管线就是一个「PreprocessConfig 的有序步骤队列」再加像素/几何语义）。
// 存储 + CRUD（add / insert / remove / replaceAt / moveUp / moveDown / clear / size / empty / items /
// begin / end / operator==）及「越界钳制 / 忽略」边界策略均由基类代劳；本类只保留
// 与 PreprocessConfig 语义绑定的方法（apply / applyExec / geometryOutput / mapRectGeometry / mapDeltaToInput）。
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
    // 注：apply 逐步语义保留不动，充当本方法的等价性参照 oracle；折叠口径统一收敛于本执行层方法。
    core::Image applyExec(const core::Image& src) const;

    // 几何推演结果（纯元数据，不碰像素）：供 GUI 在请求期计算几何签名。
    struct GeometryOut { int width; int height; int rotQuad; };

    // 给定输入尺寸，按各步语义推演输出尺寸与净旋转象限：
    //   width/height —— resize 设绝对值、rotate 90/270 交换宽高，gray/invert/split/flip 保持；
    //   rotQuad     —— 所有 rotate 角度累加后 mod 4（0..3），忽略 flip（不入签名）。
    GeometryOut geometryOutput(int width, int height) const;

    // ---------------------------------------------------------------------------
    // 轴对齐矩形的几何映射（与 applyOne/remapD4/resize 的逐像素几何严格同口径）。
    //   用途：GUI 画布底图 = 画布级有效管线作用到 composeCanvas 产物（工作图坐标系），而图层落位框
    //   来自 canvas::layerPlacement（画布坐标系）。施加画布级几何步（rotate/flip/resize）后两坐标系分叉，
    //   故需把落位框正向映射到工作图坐标系、把拖动增量逆向映射回画布坐标系，使选框与合成内容对齐、随光标同向。
    // ---------------------------------------------------------------------------
    // 映射后的矩形（输出图坐标系，左上原点，连续坐标：x/y 可非整、width/height 为跨度）。
    struct MappedRect { double x; double y; double width; double height; };

    // 正向：把输入图（inW×inH）坐标系内的轴对齐矩形 [x,y,width,height]（连续坐标），按本管线**依序**各几何步
    //   映到输出坐标系：色彩步（gray/invert/split）直通；resize 按输出/输入尺寸线性缩放；rotate(90/180/270)/flip
    //   做刚体（含镜像）顶点对射后取包围盒。空管线 / 仅色彩步 → 原样返回。
    MappedRect mapRectGeometry(double x, double y, double width, double height, int inW, int inH) const;

    // 逆向：把输出（工作图）坐标系内的一处平移增量 (dx,dy) 映回输入（画布）坐标系增量（逆序撤销各几何步的
    //   线性部分：resize 缩放取倒数、rotate/flip 用其逆变换；色彩步直通）。inW/inH 为输入（画布）尺寸。
    void mapDeltaToInput(double dx, double dy, int inW, int inH, double& outDx, double& outDy) const;
};

// 便捷函数：直接对一张图像应用单个操作，返回结果。
core::Image applyOne(const core::Image& src, const PreprocessConfig& cfg);

} // namespace idc::preprocess
