// ============================================================================
// 文件：model/annotation_editor.cpp
// 作用：实现 include/model/annotation_editor.h 声明的 AnnotationEditor 会话状态机。
//       自原 Core annotation::AnnotationLayer 的「会话半边」迁入——数据/合成委托
//       AnnotationSet，本文件只编排交互态（模式/选中/属性模板）与撤销重做。
// 分块依据：
//   1. 底图与会话      —— setImage / setEditType
//   2. 属性修改        —— change*（EDIT 作用选中项、DRAW 改当前模板的双分支）
//   3. 形状提交与编辑  —— addAnnotation / selectedAnnotation
//   4. 撤销 / 重做 / 清除 —— revoke/redo/clear/removeAnnotation/moveAnnotation/restoreAnnotations
// 说明：撤销重做不再复用 core::HistoryManager，改在 AnnotationSet + redoStack_ 上
//       1:1 复刻其**条目级**语义（列表即主栈、撤销弹尾入重做栈、新操作清重做）。
// ============================================================================
#include "model/annotation_editor.h"

#include <algorithm>

namespace idc::gui {

// ===========================================================================
// 底图与会话
// ===========================================================================

// 设置底图：拷贝一份到内部，同时清空标注集合与重做栈（新的底图意味着新的编辑会话）。
void AnnotationEditor::setImage(const core::Image& img) {
    image_ = img.clone();
    set_.clear();               // 新底图 = 新会话：一并清空集合与重做栈
    redoStack_.clear();
    selectedIndex_.reset();
}

// 切换编辑模式：MOVE / EDIT / DRAW。切换时清空当前选中，避免跨模式残留选中项。
void AnnotationEditor::setEditType(const EditType t) {
    editType_ = t;
    selectedIndex_.reset();
}

// ===========================================================================
// 属性修改
// ===========================================================================

// EDIT 模式且选中有效时返回选中项可写引用，否则 nullptr。
annotation::Annotation* AnnotationEditor::editableSelected() {
    if (editType_ != EditType::EDIT || !selectedIndex_ ||
        *selectedIndex_ >= set_.size()) {
        return nullptr;
    }
    return &set_.mutableAnnotations()[*selectedIndex_];
}

// 修改颜色（边框 / 描边色）：EDIT 模式下作用到选中形状；否则修改当前工具颜色。
// 若当前填充色仍与边框色联动（未显式改过填充色），则同步更新默认填充色，使新标注填充保持旧观感。
void AnnotationEditor::changeColor(const core::Color& c) {
    if (annotation::Annotation* a = editableSelected()) {
        a->style.color = c;
    } else {
        currentColor_ = c;
        if (fillColorLinked_) currentFillColor_ = c;
    }
}

// 修改填充色：EDIT 作用选中项的 fillColor；DRAW 改当前默认填充色并断开与边框色的联动。
void AnnotationEditor::changeFillColor(const core::Color& c) {
    if (annotation::Annotation* a = editableSelected()) {
        a->style.fillColor = c;
    } else {
        currentFillColor_ = c;
        fillColorLinked_ = false;   // 用户显式指定填充色后不再跟随边框色
    }
}

// 修改线宽。
void AnnotationEditor::changeStrokeWidth(const int w) {
    if (annotation::Annotation* a = editableSelected()) {
        a->style.strokeWidth = std::max(1, w);
    } else {
        currentStrokeWidth_ = std::max(1, w);
    }
}

// 修改填充开关。
void AnnotationEditor::changeFill(const bool fill) {
    if (annotation::Annotation* a = editableSelected()) {
        a->style.fill = fill;
    } else {
        currentFill_ = fill;
    }
}

// 修改当前文本内容（TEXT 类型使用）。
void AnnotationEditor::changeText(const std::string& text) { currentText_ = text; }

// 修改当前字号（TEXT 类型使用）。
void AnnotationEditor::changeFontSize(const double size) { currentFontSize_ = std::max(1.0, size); }

// 修改转角圆角半径：EDIT 模式下就地写到选中形状的几何（无转角概念的形状为空调用），
// 否则记为下一次绘制的默认圆角半径。与颜色/填充的事后编辑一致：不推历史快照（可撤销的仍是 add/remove）。
void AnnotationEditor::changeCornerRadius(const double r) {
    const double radius = r < 0.0 ? 0.0 : r;
    if (annotation::Annotation* a = editableSelected()) {
        if (a->shape) a->shape->setCornerRadius(radius);
    } else {
        currentCornerRadius_ = radius;
    }
}

// ===========================================================================
// 形状提交与编辑
// ===========================================================================

// 提交一个新形状：以当前属性模板组装 Annotation 后追加进集合（新操作清空重做栈，切断“未来”）。
void AnnotationEditor::addAnnotation(std::unique_ptr<geometry::Shape> shape) {
    if (!shape) return;
    annotation::Annotation ps;
    ps.style.color = currentColor_;
    ps.style.fillColor = currentFillColor_;
    ps.style.strokeWidth = currentStrokeWidth_;
    ps.style.fill = currentFill_;
    ps.shape = std::move(shape);
    set_.add(std::move(ps));    // add 内部置 zOrder ＝ 尾下标并规范化
    redoStack_.clear();         // 新操作切断“未来”
}

// 获取当前选中形状指针（可写）；未选中或越界返回 nullptr。
annotation::Annotation* AnnotationEditor::selectedAnnotation() {
    if (!selectedIndex_ || *selectedIndex_ >= set_.size()) return nullptr;
    return &set_.mutableAnnotations()[*selectedIndex_];
}

// 获取当前选中形状指针（只读）。
const annotation::Annotation* AnnotationEditor::selectedAnnotation() const {
    if (!selectedIndex_ || *selectedIndex_ >= set_.size()) return nullptr;
    return &set_.annotations()[*selectedIndex_];
}

// ===========================================================================
// 撤销 / 重做 / 清除
// ===========================================================================

// 撤销：把集合栈顶（顶层）标注移入重做栈（AnnotationSet::popBack）；集合为空时不做任何事。
void AnnotationEditor::revoke() {
    auto a = set_.popBack();
    if (!a) return;
    redoStack_.push_back(std::move(*a));
    selectedIndex_.reset();
}

// 重做：把重做栈栈顶标注移回集合（追加）；重做栈为空时不做任何事。
void AnnotationEditor::redo() {
    if (redoStack_.empty()) return;
    annotation::Annotation a = std::move(redoStack_.back());
    redoStack_.pop_back();
    set_.add(std::move(a));
}

// 清除所有标注与重做栈，但保留底图；同时清空选中。
void AnnotationEditor::clear() {
    set_.clear();
    redoStack_.clear();
    selectedIndex_.reset();
}

// 删除指定下标的标注：从集合擦除该项，作为一次新操作清空重做栈，
// 并同步修正选中下标（指向被删项则取消选中，位于其后则前移一位）。下标越界时不做任何事。
void AnnotationEditor::removeAnnotation(const std::size_t index) {
    if (!set_.removeAt(index)) return;
    redoStack_.clear();         // 新操作切断“未来”（与 add 语义一致）
    if (selectedIndex_) {
        if (*selectedIndex_ == index) selectedIndex_.reset();
        else if (*selectedIndex_ > index) --*selectedIndex_;
    }
}

// 层序调整：把 from 处标注就地移到 to（下标即 z 序，大＝顶）。区间内其余项沿移动方向平移一格、
// 相对顺序不变。不增删标注、但属用户新操作→清空重做栈；选中下标跟随被移动项。
void AnnotationEditor::moveAnnotation(const std::size_t from, const std::size_t to) {
    if (!set_.moveItem(from, to)) return;
    redoStack_.clear();
    if (selectedIndex_) {
        if (*selectedIndex_ == from) selectedIndex_ = to;
        else if (from < to && *selectedIndex_ > from && *selectedIndex_ <= to) --*selectedIndex_;
        else if (to < from && *selectedIndex_ >= to && *selectedIndex_ < from) ++*selectedIndex_;
    }
}

// 整体替换标注集合：收编给定顺序并清重做栈、取消选中（旧下标已无意义）。
// 供配置加载（SPEC §7 annotations 段）还原带样式标注。
void AnnotationEditor::restoreAnnotations(std::vector<annotation::Annotation> items) {
    set_.restore(std::move(items));
    redoStack_.clear();
    selectedIndex_.reset();
}

} // namespace idc::gui
