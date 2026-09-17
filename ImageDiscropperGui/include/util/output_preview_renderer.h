// ============================================================================
// 文件：util/output_preview_renderer.h
// 作用：导出面板「输出图像预览」的**离屏渲染器**（无状态自由函数）——把 Core 已算好的
//       engine::Composition 渲染成一张降采样缩略图（不落盘、不再跑 Core），并可选先把标注矢量
//       烘焙进源图（供「导出时烧录标注」的所见即所得预览，G-11 / guideline §4.6）。
// 分块依据：纯视图渲染工具（A-0.1）——只消费 Core 结果（Composition / Annotation）+ Qt 绘图，
//           不含切割 / 合成 / 几何逻辑；与 preview_scaler 同属 util「预览生成」关注点。把渲染从
//           app 层 MainWindow **下沉**到此，令 app 只做编排（消除边界模糊与 MainWindow 职责膨胀）。
// 说明：坐标约定——placements 的 source 在 working（原图）坐标系；invX/invY 为 working→源图 的
//       放大系数（＝1 / 源图相对原图的缩放），把 source 映射到源图像素；缩略图再按 maxDim 等比缩放。
//       标注烘焙复用 util/annotation_qt_painter::paintAnnotation，故与画布图元渲染外观一致。
// ============================================================================
#pragma once

#include <QPixmap>

#include <vector>

namespace idc::annotation { struct Annotation; }
namespace idc::engine { struct Composition; }

namespace idc::gui {

// 把标注矢量烘焙到源图副本上（working→源图 用 invX/invY 变换），返回新图；不修改传入的 src。
// 逐条调用 paintAnnotation（与画布 AnnotationItem 同一渲染规则），只在源图（通常 ≤512）上绘制一次，
// 成本与标注数成正比、与切割块数无关；随后的逐块 blit 自然让标注随像素被切割落位。src 为空则原样返回。
QPixmap bakeAnnotationsInto(const QPixmap& src,
                            double invX, double invY,
                            const std::vector<idc::annotation::Annotation>& annotations);

// 按已算好的 Composition 把源图 src 渲染为最长边 ≤ maxDim 的输出缩略图：
//   · 合并模式（canvasWidth/Height > 0）：等比缩放的输出画布，按 placement 的 source→dest blit（所见即所得）；
//   · 分离模式（无统一画布，canvas 为 0）：拼成触图（cols=ceil(√n)），每块等比缩放进格子居中。
// placements 为空 / src 为空 / maxDim<=0 / 触图格子过小 → 返回空 QPixmap（调用方回退占位文案）。
QPixmap composeOutputThumbnail(const idc::engine::Composition& comp,
                               const QPixmap& src,
                               double invX, double invY,
                               int maxDim);

} // namespace idc::gui
