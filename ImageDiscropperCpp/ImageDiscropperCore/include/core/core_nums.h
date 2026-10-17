// ============================================================================
// 文件：include/core/core_nums.h（Core 常量层 · 数字）
// 作用：把 Core 各模块里「会被人工调整」的数值（阈值 / 范围上下限 / 出厂默认值 / 业务边界）
//       收编为命名常量，集中一处便于核对与调参。纯呈现口径（屏幕像素、布局尺寸）不在本层，
//       它们属 GUI 常量层（include/util/gui_nums.h）。
// 分块依据：按来源模块 —— engine / preprocess / annotation / canvas / generate / geometry，
//       每组注释标明实际读取处。
// 命名：k + 语义名；单位入名（…Px / …Deg / …Ms / …Ratio），值旁边的注释必须写「调它影响什么」。
// 类型：inline constexpr int / double / uint32_t（无 Qt 依赖，Core 层保持 std-only）。
// 不提取（保留原样，勿为提而提）：
//   · 数学与换算常数：255（alpha 满值）、360/180（角度制）、2π、0.5（半像素）、1024、100（百分比）。
//   · 「数据即定义」的表：pixel_kernels.cpp 卷积核系数、色彩矩阵、color.cpp 分量位权与 0xFF 掩码。
//   · 循环边界 0/1/-1、size() 比较、理论不可达兜底值。
//   · 已在模块头就地命名并注释清楚的护栏常量：generate/pattern.h 的 kMaxDimension / kMaxTotalPixels。
//     它们属公开 API 契约的一部分，再搬一层只多一次跳转，故保持原位、本层不重复定义。
//   · §7 读取里「无值即不生效」的 0/1 型占位回退（width/height/x/y/cols/rows/zOrder 缺省 0）：
//     语义即「没写＝不设」，抽成常量反而掩盖意图，保持就地。
//   · tests/ 与 examples/ 内的期望值和示例值（属测试断言，改动即改测试意图）。
// 约定：新模块头只写全限定名（如 nums::engine::kDefaultQuality），不在公共头内引入别名。
// ============================================================================
#pragma once

#include <cstdint>

namespace idc::core::nums {

// engine：SPEC §7 配置读写的兜底默认与边界（src/engine/engine_config_json*.cpp、
//       include/engine/composition.h、src/io/image_io.cpp）。
namespace engine {
inline constexpr int kDefaultOpacity = 255;      // 缺 opacity 键时的回退值＝全不透明乘子。四处共用：顶层画布级、
                                                 // 底图 generate 段、图层 transform、旧 opacity 迁移步的 mul。
                                                 // 调它只改「配置文件没写该键」时的观感，不影响显式写出的值。
inline constexpr int kDefaultQuality = 90;       // emit.quality 出厂默认：CompositionParams / EmitParams 两处成员初值
                                                 // 与 §7 读取回退同源。只影响 JPEG/WebP 有损质量档位。
inline constexpr int kQualityMin = 1;            // 质量下限：写盘前 clamp 的下界（0 会让部分编码器按「默认质量」处理，故禁）。
inline constexpr int kQualityMax = 100;          // 质量上限：同时是 JPEG 编码器的合法上界。
} // namespace engine

// preprocess：预处理步的参数边界（src/preprocess/preprocess_pipeline.cpp、pixel_kernels.cpp、
//       include/preprocess/preprocess_config.h）。
namespace preprocess {
inline constexpr int kMaskLength = 3;            // 通道掩码串长度（逐位对应 R/G/B）。长度不符即整体回退全通道路径；
                                                 // 改动须与 texts::defaults::kMaskAllOn/Off 同步，否则回退判断失效。
} // namespace preprocess

// annotation：标注样式默认、几何校验容差与视图缩放边界
//       （include/annotation/{rasterizer,shape_factory,view_transform}.h、src/annotation/*.cpp）。
namespace annotation {
inline constexpr double kDefaultFontSizePx = 200.0; // TEXT 标注出厂字号（原图像素，随变换缩放）：ShapeRequest 成员初值
                                                    // 与 §7 缺 fontSize 时的回退同源；GUI 属性面板默认值亦引此。
inline constexpr int kDefaultStrokeWidth = 2;       // 描边宽度默认 px：PaintStyle 成员初值 + §7 缺 strokeWidth 的回退。
inline constexpr int kMinStrokeWidth = 1;           // 描边宽度下限：光栅化与上层 setter 共同钳制（0 会只剩填充）。
inline constexpr double kDegenerateEpsPx = 1e-3;    // 锚点退化容差 px：两锚点距离小于它即视为「没画出尺寸」，
                                                    // 形状工厂返回空、JSON 校验报因。调大会把小尺寸真实标注一并判废。
inline constexpr double kHitTolerancePx = 5.0;      // hitTest 命中容差：距轮廓 ≤ 该值 + strokeWidth 即命中（屏幕无关，场景像素）。
inline constexpr double kDefaultViewScale = 1.0;    // ViewTransform 初始缩放倍率（1＝1:1）。
inline constexpr double kMinViewScale = 0.05;       // 缩放倍率下限（5%）：GUI 缩放百分比下限的原始口径。
inline constexpr double kMaxViewScale = 10.0;       // 缩放倍率上限（1000%）。
inline constexpr double kViewScaleStep = 0.1;       // 滚轮一格步进倍率：wheelRotation × 本值后累加到 scale。
inline constexpr double kScaleRoundingFactor = 100.0; // 缩放值量化因子（＝保留 2 位小数），避免滚轮累计浮点尾差显示成 1.0000000000000002。
} // namespace annotation

// canvas：图层合成相关边界（src/canvas/）。
//       本组暂无条目：layer.cpp 的 3×3 锚点表属「数据即定义」，composite/image 的 255/0.5 属换算公式，
//       均按判据保留原位；新增可调数值时在此登记。
namespace canvas {
} // namespace canvas

// generate：程序化图案的参数默认与边界（include/generate/pattern.h、src/generate/pattern.cpp、
//       src/engine/engine_config_json_internal.h 的 §7 读取回退）。
namespace generate {
inline constexpr int kDefaultPeriod = 32;      // 周期 / 格子边长 / 环间距默认 px（PatternParams 初值与 §7 回退同源）。
inline constexpr int kMinPeriod = 2;           // 周期下限：小于 2 会让取模图案退化成纯色，生成前钳制。
inline constexpr int kDefaultLineWidth = 1;    // 网格线 / 同心环线宽默认 px。
inline constexpr int kMinLineWidth = 1;        // 线宽下限（0＝无线，视为非法输入而非关闭）。
inline constexpr int kDefaultDotRadius = 2;    // 点阵点半径默认 px。
inline constexpr int kMinDotRadius = 1;        // 点半径下限。
inline constexpr int kMinRadialSectors = 4;      // 放射线（RadialRays）最小扇区数：按周期算出的扇区过少时以本值兜底，
                                                 // 避免小图 / 大周期退化成单条半径线。
} // namespace generate

// geometry：形状几何的容差与边界（include/geometry/{affine_transform,path}.h）。
namespace geometry {
inline constexpr double kMatrixEps = 1e-12;     // 仿射矩阵浮点容差：既是「是否为单位矩阵」的判定阈值，也是
                                                // 「行列式是否近 0（奇异）」的判定阈值，两处同值故共用一条。
inline constexpr double kZeroLengthEps = 1e-12;  // 零长线段阈值（src/geometry/{path,shapes}.cpp）：与上行同值但**语义不同**，
                                                // 故不合并——path.cpp 点到线段距离比的是**长度平方**（相当于 1e-6 px 边长），
                                                // shapes.cpp 顶点倒角比的是**长度本身**。调它会影响极短边 / 零长边的退化处理。
inline constexpr double kDefaultFlatnessPx = 1.0; // 贝塞尔展平默认容差（场景像素）：越小段数越多、轮廓越贴曲线。
inline constexpr double kHitFlatnessPx = 0.5;     // 命中/轮廓距离查询用的展平容差：比绘制更细一档，避免命中判定偏移半像素。
inline constexpr double kRasterFlatnessPx = 0.5;  // 标注烧录（rasterize）用的展平容差。与上一行现同值但**分开定义**：
                                                  // 调它只影响烧录输出的轮廓精度，不应连带改动命中手感。
inline constexpr double kFlatnessFloor = 1e-6;     // 展平容差下限：调用方传 0 / 极小时不至于除零或算出无限分段。
inline constexpr int kMaxQuadSegments = 64;        // 二次曲线单次展平的折线段数上限（性能护栏）：调大更贴曲线、更慢。
inline constexpr int kMaxCubicSegments = 96;       // 三次曲线的对应上限（同偏差下需更多段，故高于二次）。
} // namespace geometry

} // namespace idc::core::nums
