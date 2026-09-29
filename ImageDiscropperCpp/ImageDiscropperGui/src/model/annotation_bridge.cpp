// ============================================================================
// 文件：src/model/annotation_bridge.cpp
// 作用：实现 AnnotationBridge——把画布手势 / 面板意图翻译成对 Core 标注 API 的调用。
// 分块依据：
//   1. 工具→形状映射    shapeTypeForTool
//   2. 构造与底图/工具   AnnotationBridge / setBaseImage / setTool
//   3. 属性写回          setColor / setStrokeWidth / setFill / setText / setFontSize
//   4. 绘制流程          两点形状（begin/update/commit）+ 折线画笔（begin/append/commit）+ 文字
//   5. 选择/移动/删除     selectAt / moveSelectedBy / removeSelected（复用 Core，不算几何）
//   6. 撤销重做/图层/烧录 undo/redo/clearAll、可见性、burnIn（逐个调 Core rasterize）
// 说明：所有几何构造走 Core buildShape、命中走 Core hitTest、合成走 Core rasterize——
//       本文件不含任何自实现的几何 / 光栅化（CONTRIBUTING.md「分层纪律」）。
// ============================================================================
#include "model/annotation_bridge.h"

#include <utility>

#include "annotation/rasterizer.h"
#include "annotation/shape_factory.h"

namespace idc::gui {

// ===========================================================================
// 1. 工具 → Core 形状类型映射
// ===========================================================================
geometry::ShapeType shapeTypeForTool(const AnnoTool tool) {
    using geometry::ShapeType;
    switch (tool) {
        case AnnoTool::RECT:          return ShapeType::RECTANGLE;
        case AnnoTool::SQUARE:        return ShapeType::SQUARE;
        case AnnoTool::RHOMBUS:       return ShapeType::RHOMBUS;
        case AnnoTool::CIRCLE:        return ShapeType::CIRCLE;
        case AnnoTool::ELLIPSE:       return ShapeType::ELLIPSE;
        case AnnoTool::ISOS_TRI:      return ShapeType::ISOS_TRIANGLE;
        case AnnoTool::EQU_TRI:       return ShapeType::EQU_TRIANGLE;
        case AnnoTool::RECT_TRI:      return ShapeType::RECT_TRIANGLE;
        case AnnoTool::EQU_RECT_TRI:  return ShapeType::EQU_RECT_TRIANGLE;
        case AnnoTool::LINE:          return ShapeType::LINE;
        case AnnoTool::POLYLINE:      return ShapeType::POLYLINE;
        case AnnoTool::BRUSH:         return ShapeType::PATH;
        case AnnoTool::TEXT:          return ShapeType::TEXT;
        case AnnoTool::SELECT:        return ShapeType::LINE; // 占位，SELECT 不构造形状
    }
    return ShapeType::LINE;
}

// ===========================================================================
// 2. 构造与底图 / 工具
// ===========================================================================
AnnotationBridge::AnnotationBridge(QObject* parent) : QObject(parent) {
    // 默认工具为 SELECT，与 Core 的 EDIT 模式对齐（属性写回作用到选中项）。
    layer_.setEditType(annotation::EditType::EDIT);
}

void AnnotationBridge::setBaseImage(const core::Image& base) {
    // Core setImage 会清空旧标注与重做栈（新底图＝新会话），并复位选中。
    layer_.setImage(base);
    pending_.reset();
    pathDraft_ = geometry::Path{};
    emit changed();
    emit selectionChanged();
}

void AnnotationBridge::setTool(const AnnoTool tool) {
    if (tool_ == tool) return;
    cancelPending();               // 切换工具前放弃未提交的预览 / 路径草稿
    tool_ = tool;
    if (tool_ == AnnoTool::SELECT) {
        layer_.setEditType(annotation::EditType::EDIT);
    } else {
        layer_.setEditType(annotation::EditType::DRAW);
        layer_.setCurrentType(shapeTypeForTool(tool_));
    }
    emit toolChanged();
    emit selectionChanged();       // setEditType 已复位选中
}

// ===========================================================================
// 3. 属性写回（委托 Core change*；EDIT 作用选中项，DRAW 改当前默认）
// ===========================================================================
void AnnotationBridge::setColor(const core::Color& c) { layer_.changeColor(c); emit changed(); }
void AnnotationBridge::setFillColor(const core::Color& c) { layer_.changeFillColor(c); emit changed(); }
void AnnotationBridge::setStrokeWidth(const int w) { layer_.changeStrokeWidth(w); emit changed(); }
void AnnotationBridge::setFill(const bool fill) { layer_.changeFill(fill); emit changed(); }
void AnnotationBridge::setText(const std::string& text) { layer_.changeText(text); emit changed(); }
void AnnotationBridge::setFontSize(const double size) { layer_.changeFontSize(size); emit changed(); }
void AnnotationBridge::setCornerRadius(const double r) { layer_.changeCornerRadius(r); emit changed(); }

// ===========================================================================
// 4. 绘制流程
// ===========================================================================

// 依当前工具重建 pending_ 预览形状：折线 / 画笔用路径草稿，其余用起点 + 当前点。
void AnnotationBridge::rebuildPending(const core::Point2D current) {
    annotation::ShapeRequest req;
    req.type = shapeTypeForTool(tool_);
    req.text = layer_.currentText();
    req.fontSize = layer_.currentFontSize();
    req.cornerRadius = layer_.currentCornerRadius();
    if (tool_ == AnnoTool::POLYLINE || tool_ == AnnoTool::BRUSH) {
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
    layer_.addAnnotation(std::move(pending_));  // Core 用当前 color/stroke/fill 封装为 Annotation
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
// 仅 POLYLINE 生效且需已有草稿；临时段写入 req.path 的副本，**不污染 pathDraft_**（左键点击才落顶点）。
void AnnotationBridge::previewPolyline(const core::Point2D cursor) {
    if (tool_ != AnnoTool::POLYLINE || pathDraft_.empty()) return;
    annotation::ShapeRequest req;
    req.type = geometry::ShapeType::POLYLINE;
    req.cornerRadius = layer_.currentCornerRadius();
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
    req.type = shapeTypeForTool(tool_);        // POLYLINE→POLYLINE，BRUSH→PATH
    req.cornerRadius = layer_.currentCornerRadius();
    req.path = pathDraft_;
    layer_.addAnnotation(idc::annotation::buildShape(req));
    pending_.reset();
    pathDraft_ = geometry::Path{};
    emit changed();
}

void AnnotationBridge::addText(const core::Point2D p, const std::string& text) {
    if (text.empty()) return;
    annotation::ShapeRequest req;
    req.type = geometry::ShapeType::TEXT;
    req.p1 = p;
    req.p2 = p;
    req.text = text;
    req.fontSize = layer_.currentFontSize();
    layer_.changeText(text);                       // 同步为当前文本默认
    layer_.addAnnotation(idc::annotation::buildShape(req));
    emit changed();
}

// ===========================================================================
// 5. 选择 / 移动 / 删除（复用 Core）
// ===========================================================================
void AnnotationBridge::selectAt(const core::Point2D p) {
    layer_.selectAnnotation(layer_.hitTest(p));    // 命中检测全在 Core
    emit selectionChanged();
    emit changed();
}

void AnnotationBridge::clearSelection() {
    layer_.selectAnnotation(std::nullopt);
    emit selectionChanged();
    emit changed();
}

void AnnotationBridge::moveSelectedBy(const double dx, const double dy) {
    const annotation::Annotation* a = layer_.selectedAnnotation();
    if (!a || !a->shape) return;
    // 复用 Core 几何：世界系平移形状（内部把世界位移换算到局部系后偏移自身参数），保留具体类型与参数化身份
    // ——不再展平为通用 PATH，故 TEXT 字形、矩形/圆等元数据与控制点语义均不丢失（shapeType 保持不变）；
    // 且形状经旋转/缩放后拖动方向仍跟随光标（translateWorld 而非 translate）。
    a->shape->translateWorld(dx, dy);
    emit changed();
}

void AnnotationBridge::removeSelected() {
    const std::optional<std::size_t> idx = layer_.selectedAnnotationIndex();
    if (!idx) return;
    layer_.removeAnnotation(*idx);                 // 委托 Core 删除（属新操作，会清空重做栈）
    emit changed();
    emit selectionChanged();
}

// 变换选中标注：缩放 + 旋转。委托 Core Shape 的**非破坏性仿射矩阵**（OBB 变换 applyObbTransform：
// 沿形状自身轴缩放（局部系右乘）+ 绕世界中心刚性旋转（世界系左乘），二者分处矩阵左右故已拉伸形状再旋转也不剪切），
// **保留具体类型与文字字形**（不退化）；sx/sy 为负即翻转。属性面板与画布包围盒手柄共用本入口。
// 无选中 / 无变化则忽略。
void AnnotationBridge::transformSelected(const double sx, const double sy, const double rotateDeg) {
    const annotation::Annotation* a = layer_.selectedAnnotation();
    if (!a || !a->shape) return;
    const bool scaleChanged = std::fabs(sx - 1.0) > 1e-9 || std::fabs(sy - 1.0) > 1e-9;
    if (const bool rotateChanged = std::fabs(rotateDeg) > 1e-9; !scaleChanged && !rotateChanged) return;
    a->shape->applyObbTransform(sx, sy, rotateDeg);
    emit changed();
}

// ===========================================================================
// 5b. 层序（叠放次序）：下标即 z 序（大＝顶）；作用于选中项，委托 Core moveAnnotation 重排
// ===========================================================================
void AnnotationBridge::raiseSelected() {
    const std::optional<std::size_t> idx = layer_.selectedAnnotationIndex();
    if (!idx) return;
    if (const std::size_t n = layer_.annotations().size(); *idx + 1 >= n) return;                 // 已在顶层
    layer_.moveAnnotation(*idx, *idx + 1);
    emit changed();
    emit selectionChanged();
}

void AnnotationBridge::lowerSelected() {
    const std::optional<std::size_t> idx = layer_.selectedAnnotationIndex();
    if (!idx || *idx == 0) return;             // 无选中或已在底层
    layer_.moveAnnotation(*idx, *idx - 1);
    emit changed();
    emit selectionChanged();
}

void AnnotationBridge::bringSelectedToFront() {
    const std::optional<std::size_t> idx = layer_.selectedAnnotationIndex();
    if (!idx) return;
    const std::size_t n = layer_.annotations().size();
    if (n == 0 || *idx == n - 1) return;       // 已在顶层
    layer_.moveAnnotation(*idx, n - 1);
    emit changed();
    emit selectionChanged();
}

void AnnotationBridge::sendSelectedToBack() {
    const std::optional<std::size_t> idx = layer_.selectedAnnotationIndex();
    if (!idx || *idx == 0) return;             // 无选中或已在底层
    layer_.moveAnnotation(*idx, 0);
    emit changed();
    emit selectionChanged();
}

// ===========================================================================
// 6. 撤销重做 / 图层可见性 / 导出烧录
// ===========================================================================
void AnnotationBridge::undo() { layer_.revoke(); emit changed(); emit selectionChanged(); }
void AnnotationBridge::redo() { layer_.redo(); emit changed(); }
void AnnotationBridge::clearAll() { layer_.clear(); emit changed(); emit selectionChanged(); }

// 配置还原：整体替换标注列表（委托 Core restoreAnnotations，不推撤销快照），再发信号刷新画布/面板。
void AnnotationBridge::replaceAnnotations(std::vector<annotation::Annotation> items) {
    layer_.restoreAnnotations(std::move(items));
    emit changed();
    emit selectionChanged(); // 还原后无选中，面板同步退出属性回显。
}

void AnnotationBridge::setLayerVisible(const bool on) {
    if (layerVisible_ == on) return;
    layerVisible_ = on;
    emit changed();
}

void AnnotationBridge::setBurnIn(const bool on) {
    burnInEnabled_ = on;   // 仅影响导出，无需重绘画布
}

core::Image AnnotationBridge::burnIn(const core::Image& base) const {
    // 以传入的当前工作图为底（而非 Core 内部可能陈旧的 image_），逐个调 Core rasterize 合成。
    core::Image out = base.empty()
        ? core::Image(1, 1, core::ImageFormat::RGBA)
        : base.toRGBA();
    for (const annotation::Annotation& a : layer_.annotations()) {
        if (!a.shape) continue;
        annotation::PaintStyle style;
        style.color = a.color;
        style.fillColor = a.fillColor;
        style.strokeWidth = a.strokeWidth;
        style.fill = a.fillType;
        style.antialias = true;
        idc::annotation::rasterize(out, *a.shape, style);
    }
    return out;
}

// ===========================================================================
// 7. 属性回显（供右侧标注属性页）
// ===========================================================================

// 有选中项则取其属性，否则取当前工具默认（与 Core change* 的 EDIT/DRAW 分支一致）。
core::Color AnnotationBridge::displayColor() const {
    const annotation::Annotation* a = layer_.selectedAnnotation();
    return a ? a->color : layer_.currentColor();
}

core::Color AnnotationBridge::displayFillColor() const {
    const annotation::Annotation* a = layer_.selectedAnnotation();
    return a ? a->fillColor : layer_.currentFillColor();
}

int AnnotationBridge::displayStrokeWidth() const {
    const annotation::Annotation* a = layer_.selectedAnnotation();
    return a ? a->strokeWidth : layer_.currentStrokeWidth();
}

bool AnnotationBridge::displayFill() const {
    const annotation::Annotation* a = layer_.selectedAnnotation();
    return a ? a->fillType : layer_.currentFill();
}

// 选中形状的转角圆角半径（取自形状几何）；无选中 / 无形状则取当前默认。
double AnnotationBridge::displayCornerRadius() const {
    const annotation::Annotation* a = layer_.selectedAnnotation();
    return a && a->shape ? a->shape->cornerRadius() : layer_.currentCornerRadius();
}

// ---- 形状类型 → 能力判定（纯 UI 置灰策略，非几何计算；键于 Core ShapeType 枚举）----
//   - 可填充：除直线（2 点无内部）与文字（字形，填充色不参与）外的形状均可填充——含折线 / 画笔等开放链，
//     渲染时按偶奇规则首尾隐式闭合填充（Qt 预览 fillPath 与 Core rasterize 既有实现一致，均以 !isLine 为准）。
//   - 可倒角：仅具顶点转角的形状（闭合多边形 + 折线）；直线（2 点无内部顶点）/ 圆 / 椭圆 / 画笔 / 文字无转角概念。
namespace {
bool supportsFillFor(const geometry::ShapeType t) {
    // 直线无内部、文字为字形（填充色不参与），其余形状（含开放折线 / 画笔）均可填充。
    return t != geometry::ShapeType::LINE && t != geometry::ShapeType::TEXT;
}
bool supportsCornerRadiusFor(const geometry::ShapeType t) {
    switch (t) {
        case geometry::ShapeType::RECTANGLE:
        case geometry::ShapeType::SQUARE:
        case geometry::ShapeType::RHOMBUS:
        case geometry::ShapeType::POLYLINE:
        case geometry::ShapeType::ISOS_TRIANGLE:
        case geometry::ShapeType::EQU_TRIANGLE:
        case geometry::ShapeType::RECT_TRIANGLE:
        case geometry::ShapeType::EQU_RECT_TRIANGLE:
            return true;
        default:
            return false;
    }
}
} // namespace

// 上下文判定：有选中 → 按选中形状类型；绘制工具激活 → 按当前工具类型；
// SELECT 且无选中 → 不做限制（一律可用）。此时面板编辑的是「下一次绘制」的默认值，而 currentType_
// 是上一绘制工具的残值（Core 仅在切到非 SELECT 工具时更新）；若据残值置灰，会出现
// 「用直线后切回选择/移动，填充/圆角控件莫名点不了」的错禁问题。
bool AnnotationBridge::contextSupportsFill() const {
    if (const annotation::Annotation* a = layer_.selectedAnnotation()) return supportsFillFor(a->shapeType);
    if (tool_ == AnnoTool::SELECT) return true;
    return supportsFillFor(layer_.currentType());
}
bool AnnotationBridge::contextSupportsCornerRadius() const {
    if (const annotation::Annotation* a = layer_.selectedAnnotation()) return supportsCornerRadiusFor(a->shapeType);
    if (tool_ == AnnoTool::SELECT) return true;
    return supportsCornerRadiusFor(layer_.currentType());
}

// 选中标注的累积 OBB 变换参数（绝对值），直接取自 Core Shape 同步维护的 obbScaleX/Y、obbRotationDeg；
// 无选中 / 无形状时返回恒等（1,1,0）——供属性面板忠实回显底层真实变换（不自行分解矩阵，避免符号歧义）。
double AnnotationBridge::displayObbScaleX() const {
    const annotation::Annotation* a = layer_.selectedAnnotation();
    return a && a->shape ? a->shape->obbScaleX() : 1.0;
}
double AnnotationBridge::displayObbScaleY() const {
    const annotation::Annotation* a = layer_.selectedAnnotation();
    return a && a->shape ? a->shape->obbScaleY() : 1.0;
}
double AnnotationBridge::displayObbRotationDeg() const {
    const annotation::Annotation* a = layer_.selectedAnnotation();
    return a && a->shape ? a->shape->obbRotationDeg() : 0.0;
}

} // namespace idc::gui
