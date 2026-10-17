// ============================================================================
// 文件：model/annotation_bridge_pending.cpp
// 作用：AnnotationBridge 的「绘制流程」域成员实现（同类其它 TU 分块）——工具 pending 草稿重建、
//       两点形状 begin/update/commit、折线/画笔路径 begin/append/preview/commit、
//       文字添加。AnnotationBridge 类不拆分，仅按职责分 TU 编译。
// ============================================================================
#include "model/annotation_bridge.h"

#include <utility>

#include "annotation/shape_factory.h"

namespace idc::gui {


// ===========================================================================
// 4. 绘制流程
// ===========================================================================

// 依当前工具重建 pending_ 预览形状：折线 / 画笔用路径草稿，其余用起点 + 当前点。
void AnnotationBridge::rebuildPending(const core::Point2D current) {
    annotation::ShapeRequest req;
    req.type = shapeTypeForTool(tool_);
    req.text = editor_.currentText();
    req.fontSize = editor_.currentFontSize();
    req.cornerRadius = editor_.currentCornerRadius();
    if (tool_ == AnnoTool::Polyline || tool_ == AnnoTool::Brush) {
        req.path = pathDraft_;                 // buildShape 将 path 封装为 PathShape
    } else {
        req.p1 = anchor_;
        req.p2 = current;
    }
    pending_ = idc::annotation::buildShape(req);
}

void AnnotationBridge::beginShape(const core::Point2D p) {
    anchor_ = p;
    rebuildPending(p);             // 退化预览（起点＝终点），随拖拽更新
    emit pendingChanged();
}

void AnnotationBridge::updateShape(const core::Point2D p) {
    rebuildPending(p);
    emit pendingChanged();
}

void AnnotationBridge::commitShape() {
    if (!pending_) return;
    editor_.addAnnotation(std::move(pending_));  // 会话用当前 color/stroke/fill 封装为 Annotation
    pending_.reset();
    emit changed();
}

void AnnotationBridge::cancelPending() {
    if (!pending_ && pathDraft_.empty()) return;
    pending_.reset();
    pathDraft_ = geometry::Path{};
    emit pendingChanged();
}

void AnnotationBridge::beginPath(const core::Point2D p) {
    pathDraft_ = geometry::Path{};
    pathDraft_.moveTo(p.x, p.y);
    anchor_ = p;
    rebuildPending(p);
    emit pendingChanged();
}

void AnnotationBridge::appendPathPoint(const core::Point2D p) {
    pathDraft_.lineTo(p.x, p.y);
    rebuildPending(p);
    emit pendingChanged();
}

// 折线橡皮筋预览：以「已落顶点的草稿 + 一段到光标的临时连线」构造 pending，供移动时实时成形。
// 仅 Polyline 生效且需已有草稿；临时段写入 req.path 的副本，**不污染 pathDraft_**（左键点击才落顶点）。
void AnnotationBridge::previewPolyline(const core::Point2D cursor) {
    if (tool_ != AnnoTool::Polyline || pathDraft_.empty()) return;
    annotation::ShapeRequest req;
    req.type = geometry::ShapeType::Polyline;
    req.cornerRadius = editor_.currentCornerRadius();
    req.path = pathDraft_;                     // 拷贝草稿（含已落顶点）
    req.path.lineTo(cursor.x, cursor.y);       // 追加橡皮筋临时段（仅预览）
    pending_ = idc::annotation::buildShape(req);
    emit pendingChanged();
}

void AnnotationBridge::commitPath() {
    // 至少两个顶点（moveTo + lineTo）才构成有意义的折线 / 路径。
    if (pathDraft_.size() < 2) {
        cancelPending();
        return;
    }
    // 依 pathDraft_ 的正式顶点重建形状后提交：**不复用 pending_**——折线的 pending_ 可能是
    // previewPolyline 追加了橡皮筋临时段的预览形状，收笔时须丢弃该临时段，只保留已落顶点。
    annotation::ShapeRequest req;
    req.type = shapeTypeForTool(tool_);        // Polyline→Polyline，Brush→Path
    req.cornerRadius = editor_.currentCornerRadius();
    req.path = pathDraft_;
    editor_.addAnnotation(idc::annotation::buildShape(req));
    pending_.reset();
    pathDraft_ = geometry::Path{};
    emit changed();
}

void AnnotationBridge::addText(const core::Point2D p, const std::string& text) {
    if (text.empty()) return;
    annotation::ShapeRequest req;
    req.type = geometry::ShapeType::Text;
    req.p1 = p;
    req.p2 = p;
    req.text = text;
    req.fontSize = editor_.currentFontSize();
    editor_.changeText(text);                       // 同步为当前文本默认
    editor_.addAnnotation(idc::annotation::buildShape(req));
    emit changed();
}

} // namespace idc::gui
