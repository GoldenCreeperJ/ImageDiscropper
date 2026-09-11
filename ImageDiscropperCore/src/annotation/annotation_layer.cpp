// ============================================================================
// 文件：src/annotation/annotation_layer.cpp
// 作用：实现 include/annotation/annotation_layer.h 声明的 Annotation 拷贝语义与
//       AnnotationLayer 会话状态机。
// 分块依据：
//   1. Annotation 拷贝语义      —— 因为 unique_ptr<Shape> 不可默认拷贝
//   2. EditType 辅助函数    —— 字符串 ↔ 枚举映射
//   3. AnnotationLayer 各成员  —— 会话状态机主体
// ============================================================================
#include "annotation/annotation_layer.h"

#include <algorithm>

#include "annotation/rasterizer.h"

namespace idc::annotation {

// ===========================================================================
// Annotation 拷贝语义
// ===========================================================================

// 拷贝构造：深拷贝 shape 指针指向的几何对象。
Annotation::Annotation(const Annotation& o)
    : shape(o.shape ? o.shape->clone() : nullptr),
      color(o.color),
      strokeWidth(o.strokeWidth),
      fillType(o.fillType),
      shapeType(o.shapeType) {}

// 拷贝赋值：先释放旧 shape，再深拷贝。
Annotation& Annotation::operator=(const Annotation& o) {
    if (this != &o) {
        shape = o.shape ? o.shape->clone() : nullptr;
        color = o.color;
        strokeWidth = o.strokeWidth;
        fillType = o.fillType;
        shapeType = o.shapeType;
    }
    return *this;
}

// ===========================================================================
// EditType 辅助函数
// ===========================================================================

// 字符串 → EditType，默认 DRAW。
EditType parseEditType(const std::string& s) {
    if (s == "Edit") return EditType::EDIT;
    if (s == "Move") return EditType::MOVE;
    return EditType::DRAW;
}

// EditType → 字符串。
const char* editTypeName(const EditType t) {
    switch (t) {
        case EditType::DRAW: return "Draw";
        case EditType::EDIT: return "Edit";
        case EditType::MOVE: return "Move";
    }
    return "Draw";
}

// ===========================================================================
// AnnotationLayer
// ===========================================================================

// 构造：默认底图为空、模式为 DRAW、当前工具为 LINE、颜色黑、线宽 2、不填充。
AnnotationLayer::AnnotationLayer() = default;

// 设置底图：拷贝一份到内部，同时清空形状与垃圾栈（新的底图意味着新的编辑会话）。
void AnnotationLayer::setImage(const core::Image& img) {
    image_ = img.clone();
    annotations_.clear();
    garbage_.clear();
    selectedIndex_.reset();
}

// 切换编辑模式：MOVE / EDIT / DRAW。
// 切换时清空当前选中，避免跨模式残留选中项。
void AnnotationLayer::setEditType(const EditType t) {
    editType_ = t;
    selectedIndex_.reset();
}

// ------ 属性修改 ------

// 修改颜色：EDIT 模式下作用到选中形状；否则修改当前工具颜色。
void AnnotationLayer::changeColor(const core::Color& c) {
    if (editType_ == EditType::EDIT && selectedIndex_ && *selectedIndex_ < annotations_.size()) {
        annotations_[*selectedIndex_].color = c;
    } else {
        currentColor_ = c;
    }
}

// 修改线宽。
void AnnotationLayer::changeStrokeWidth(const int w) {
    if (editType_ == EditType::EDIT && selectedIndex_ && *selectedIndex_ < annotations_.size()) {
        annotations_[*selectedIndex_].strokeWidth = std::max(1, w);
    } else {
        currentStrokeWidth_ = std::max(1, w);
    }
}

// 修改填充开关。
void AnnotationLayer::changeFill(const bool fill) {
    if (editType_ == EditType::EDIT && selectedIndex_ && *selectedIndex_ < annotations_.size()) {
        annotations_[*selectedIndex_].fillType = fill;
    } else {
        currentFill_ = fill;
    }
}

// 修改当前文本内容（TEXT 类型使用）。
void AnnotationLayer::changeText(const std::string& text) { currentText_ = text; }

// 修改当前字号（TEXT 类型使用）。
void AnnotationLayer::changeFontSize(const double size) { currentFontSize_ = std::max(1.0, size); }

// ------ 形状提交与命中 ------

// 提交一个新形状：将其封装为 Annotation 追加到 annotations_，同时清空垃圾栈
// （新操作切断"未来"，符合常见撤销重做语义）。
void AnnotationLayer::addAnnotation(std::unique_ptr<geometry::Shape> shape) {
    if (!shape) return;
    Annotation ps;
    ps.shapeType = shape->type();
    ps.color = currentColor_;
    ps.strokeWidth = currentStrokeWidth_;
    ps.fillType = currentFill_;
    ps.shape = std::move(shape);
    annotations_.push_back(std::move(ps));
    garbage_.clear();
}

// 命中检测：从顶层（最后绘制的）开始遍历，返回第一个命中的索引。
// 判定规则：
//   - 填充形状：使用 Path::contains 判断点是否在内部
//   - 未填充形状：使用 Path::distanceToOutline 判断点是否靠近轮廓
//     （容差 = 5.0 + strokeWidth）
std::optional<std::size_t> AnnotationLayer::hitTest(const core::Point2D& p) const {
    for (std::size_t i = annotations_.size(); i-- > 0;) {
        const Annotation& ps = annotations_[i];
        if (!ps.shape) continue;
        const geometry::Path path = ps.shape->toPath();

        // 填充且非直线：优先按内部判定
        if (ps.fillType && ps.shapeType != geometry::ShapeType::LINE) {
            if (path.contains(p.x, p.y)) return i;
        }
        // 其余情况按轮廓距离判定
        if (const double dist = path.distanceToOutline(p.x, p.y); dist < 5.0 + ps.strokeWidth) return i;
    }
    return std::nullopt;
}

// 设置选中形状索引；传入 nullopt 表示取消选中。
void AnnotationLayer::selectAnnotation(const std::optional<std::size_t> idx) {
    selectedIndex_ = idx;
}

// 获取当前选中形状指针（可写）；未选中或越界返回 nullptr。
Annotation* AnnotationLayer::selectedAnnotation() {
    if (!selectedIndex_ || *selectedIndex_ >= annotations_.size()) return nullptr;
    return &annotations_[*selectedIndex_];
}

// 获取当前选中形状指针（只读）。
const Annotation* AnnotationLayer::selectedAnnotation() const {
    if (!selectedIndex_ || *selectedIndex_ >= annotations_.size()) return nullptr;
    return &annotations_[*selectedIndex_];
}

// ------ 撤销 / 重做 / 清除 ------

// 撤销：把 annotations_ 末尾的形状搬到 garbage_ 末尾；annotations_ 为空时不做任何事。
void AnnotationLayer::revoke() {
    if (annotations_.empty()) return;
    garbage_.push_back(std::move(annotations_.back()));
    annotations_.pop_back();
    selectedIndex_.reset();
}

// 重做：把 garbage_ 末尾的形状搬回 annotations_ 末尾；garbage_ 为空时不做任何事。
void AnnotationLayer::redo() {
    if (garbage_.empty()) return;
    annotations_.push_back(std::move(garbage_.back()));
    garbage_.pop_back();
}

// 清除所有形状与垃圾栈，但保留底图；同时清空选中。
void AnnotationLayer::clear() {
    annotations_.clear();
    garbage_.clear();
    selectedIndex_.reset();
}

// ------ 合成 ------

// 合成：以当前底图为基础，逐个光栅化所有形状，返回新图像。
core::Image AnnotationLayer::burnIn() const {
    core::Image out = image_.empty()
        ? core::Image(1, 1, core::ImageFormat::RGBA)
        : image_.toRGBA();

    for (const Annotation& ps : annotations_) {
        if (!ps.shape) continue;
        PaintStyle style;
        style.color = ps.color;
        style.strokeWidth = ps.strokeWidth;
        style.fill = ps.fillType;
        style.antialias = true;
        rasterize(out, *ps.shape, style);
    }
    return out;
}

} // namespace idc::annotation
