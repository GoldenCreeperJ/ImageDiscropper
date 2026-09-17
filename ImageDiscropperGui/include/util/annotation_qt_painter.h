// ============================================================================
// 文件：util/annotation_qt_painter.h
// 作用：把单个 Core annotation::Annotation 以矢量方式绘制到 QPainter 的**公共渲染函数**——
//       非文字形状描边 + 可选填充；文字（TEXT）直接绘真实字形。供画布图元 AnnotationItem::paint
//       与导出输出预览的标注烘焙（util/output_preview_renderer）共用，消除两处重复的标注绘制逻辑。
// 分块依据：仅承载「Annotation（几何 + 样式）→ QPainter 绘制指令」的渲染翻译（视图关注点，A-0.1），
//           不含任何几何计算，也不含选中高亮 / OBB 手柄等图元专属交互（那些留在 AnnotationItem）。
// 说明：调用方须先把 painter 的变换设为「世界（原图）坐标 → 设备坐标」并按需设好裁剪；本函数只在
//       该世界坐标系下按标注样式发绘制指令，除 TEXT 分支临时叠加字形变换外，不改动 painter 的世界变换。
// ============================================================================
#pragma once

#include <QPainterPath>

class QPainter;

namespace idc::annotation { struct Annotation; }
namespace idc::geometry { class AffineTransform; }

namespace idc::gui {

// 在当前 painter 变换（世界→设备）下绘制一条标注：
//   · 非文字形状：用 worldDrawPath（调用方给出的世界坐标路径——普通态＝Shape::worldPath()、
//     拖拽预览态＝xf.applyToPath(toPath())）按 color/strokeWidth 描边，fillType 为真时用同色填充；
//   · 文字（TEXT）：以 textXf（局部→世界的有效变换）临时叠加到 painter 后绘制真实字形（字号 fontSize）。
// 仅发绘制指令；不绘选中高亮 / 手柄（图元专属，留在 AnnotationItem）。ann.shape 为空则忽略。
void paintAnnotation(QPainter& painter,
                     const idc::annotation::Annotation& ann,
                     const QPainterPath& worldDrawPath,
                     const idc::geometry::AffineTransform& textXf);

} // namespace idc::gui
