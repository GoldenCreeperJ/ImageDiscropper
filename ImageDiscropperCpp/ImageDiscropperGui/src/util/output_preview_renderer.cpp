// ============================================================================
// 文件：src/util/output_preview_renderer.cpp
// 作用：实现导出输出预览的离屏渲染（见同名头说明）——bakeAnnotationsInto 把标注烘焙进源图，
//       composeOutputThumbnail 按 Core Composition 把源图 blit 成降采样输出缩略图。
// 分块依据：从 app 层 MainWindow 下沉的**纯渲染**逻辑；只消费 Core 结果 + Qt 绘图，不含引擎/几何计算。
// 说明：标注绘制复用 util/annotation_qt_painter::paintAnnotation（与画布图元同一外观）；坐标映射与
//       Core exportSeparate/exportMerged 的 source→dest 语义一致（预览忠实，溢出块由 drawPixmap 裁剪）。
// ============================================================================
#include "util/output_preview_renderer.h"

#include <cmath>

#include <QColor>
#include <QPainter>
#include <QRectF>
#include <QTransform>

#include "annotation/annotation_layer.h"   // Annotation / Shape 访问
#include "engine/composition.h"            // Composition / Placement
#include "engine/region.h"                 // RectRegion（empty/width/height）
#include "geometry/shapes.h"               // Shape::worldPath / transform
#include "util/annotation_qt_painter.h"    // paintAnnotation（标注矢量绘制公共实现）
#include "util/path_qt_adapter.h"          // toQPainterPath

namespace idc::gui {

QPixmap bakeAnnotationsInto(const QPixmap& src, const double invX, const double invY,
                            const std::vector<idc::annotation::Annotation>& annotations) {
    if (src.isNull() || annotations.empty()) return src;

    QPixmap out = src;   // QPixmap 隐式共享，QPainter 绘制时自动 detach，不修改传入的 src。
    QPainter p(&out);
    p.setRenderHint(QPainter::Antialiasing, true);
    // working 坐标 → 源图坐标（invX/invY ＝ 1 / 源图相对原图的缩放系数）。
    p.setTransform(QTransform::fromScale(invX, invY));
    for (const idc::annotation::Annotation& ann : annotations) {
        if (!ann.shape) continue;
        // 世界路径（已套用形状的非破坏性变换）；文字字形变换由 paintAnnotation 内部处理。
        const QPainterPath world = toQPainterPath(ann.shape->worldPath());
        paintAnnotation(p, ann, world, ann.shape->transform());
    }
    p.end();
    return out;
}

QPixmap composeOutputThumbnail(const idc::engine::Composition& comp, const QPixmap& src,
                               const double invX, const double invY, const int maxDim) {
    if (comp.placements.empty() || src.isNull() || maxDim <= 0) return QPixmap();
    const idc::core::Color pad = comp.padColor;
    const QColor padQ(pad.r, pad.g, pad.b, pad.a);

    // 合并模式（坍缩/重排）：有统一画布 → 等比缩到 maxDim 后按 dest 落位 blit（所见即所得）。
    if (comp.canvasWidth > 0 && comp.canvasHeight > 0) {
        const double s = std::min({static_cast<double>(maxDim) / comp.canvasWidth,
                                   static_cast<double>(maxDim) / comp.canvasHeight, 1.0});
        const int tw = std::max(1, static_cast<int>(std::lround(comp.canvasWidth * s)));
        const int th = std::max(1, static_cast<int>(std::lround(comp.canvasHeight * s)));
        QPixmap out(tw, th);
        out.fill(padQ);   // padColor 透明时填充透明（与 Core exportMerged 一致）。
        QPainter p(&out);
        p.setRenderHint(QPainter::SmoothPixmapTransform);
        for (const idc::engine::Placement& pl : comp.placements) {
            if (pl.source.empty()) continue;   // 退化块跳过（与 Core 导出行为一致）。
            const QRectF srcrect(pl.source.left * invX, pl.source.top * invY,
                                 pl.source.width() * invX, pl.source.height() * invY);
            const QRectF dst(pl.dest.left * s, pl.dest.top * s,
                             pl.dest.width() * s, pl.dest.height() * s);
            p.drawPixmap(dst, src, srcrect);
        }
        p.end();
        return out;
    }

    // 分离模式：无统一画布（canvasWidth/Height=0）→ 拼接触图（cols=ceil(sqrt(n))），每块等比缩放进格子居中。
    const int n = static_cast<int>(comp.placements.size());
    int cols = static_cast<int>(std::ceil(std::sqrt(static_cast<double>(n))));
    if (cols < 1) cols = 1;
    const int rows = std::max(1, (n + cols - 1) / cols);
    const int gap = 2;
    const int cellW = maxDim / cols;
    const int cellH = maxDim / rows;
    if (cellW <= 2 * gap || cellH <= 2 * gap) return QPixmap();  // 块太多、格子过小 → 不出图（避免糊成一团）。
    QPixmap out(cols * cellW, rows * cellH);
    out.fill(padQ);
    QPainter p(&out);
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    for (int i = 0; i < n; ++i) {
        const idc::engine::Placement& pl = comp.placements[static_cast<std::size_t>(i)];
        const double sw = static_cast<double>(pl.source.width());
        const double sh = static_cast<double>(pl.source.height());
        if (sw <= 0.0 || sh <= 0.0) continue;
        const int r = i / cols, c = i % cols;
        const QRectF cell(c * cellW + gap, r * cellH + gap, cellW - 2 * gap, cellH - 2 * gap);
        const double fs = std::min(cell.width() / sw, cell.height() / sh);  // 等比适配格子
        const double dw = sw * fs, dh = sh * fs;
        const QRectF dst(cell.x() + (cell.width() - dw) / 2.0,
                         cell.y() + (cell.height() - dh) / 2.0, dw, dh);
        const QRectF srcrect(pl.source.left * invX, pl.source.top * invY, sw * invX, sh * invY);
        p.drawPixmap(dst, src, srcrect);
    }
    p.end();
    return out;
}

} // namespace idc::gui
