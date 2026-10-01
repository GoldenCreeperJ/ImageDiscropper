// ============================================================================
// 文件：model/annotation_editor.h
// 作用：标注「编辑会话」状态机——持有 Core annotation::AnnotationSet（纯数据集合），
//       在其上叠加 GUI 交互态：编辑模式（EditType）/ 选中项 / 当前属性模板 / 撤销重做栈。
//       自原 Core annotation::AnnotationLayer 拆分而来（会话半边归 app 层，数据/合成半边留 Core）。
// 分块依据：
//   - EditType      : 当前编辑模式（DRAW / EDIT / MOVE）——纯交互枚举，Core 不再持有。
//   - AnnotationEditor : 组合 AnnotationSet（唯一数据真相源）+ redoStack_（重做栈）+ 会话态；
//       撤销保持**条目级**语义（列表即主栈、撤销弹尾入重做栈、新操作清重做），与原 AnnotationLayer 逐字一致。
// 说明：本类**零 Qt 依赖**，可被无头（CTest）直接编译进 unit_tests 作会话语义回归；
//       撤销/重做不再复用 core::HistoryManager，改在 AnnotationSet + redoStack_ 上 1:1 复刻其语义。
// ============================================================================
#pragma once

#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "annotation/annotation_set.h"
#include "core/color.h"
#include "core/image.h"
#include "core/point.h"
#include "geometry/shape_type.h"
#include "geometry/shapes.h"

namespace idc::gui {

// ---------------------------------------------------------------------------
// EditType：编辑模式（自原 Core annotation::EditType 迁入 GUI；交互概念，Core 不留）。
// ---------------------------------------------------------------------------
enum class EditType {
    DRAW,   // 绘制新形状
    EDIT,   // 选中并修改已有形状
    MOVE,   // 平移视图
};

// ---------------------------------------------------------------------------
// AnnotationEditor：标注编辑会话（数据委托 AnnotationSet，本类只管交互态 + 撤销重做）。
// ---------------------------------------------------------------------------
class AnnotationEditor {
public:
    // ------ 底图与会话 ------
    // 设置底图：拷贝持有；新底图＝新会话，清空标注集合与重做栈、复位选中。
    void setImage(const core::Image& img);
    const core::Image& image() const { return image_; }

    // 只读引用底层集合：供保存配置时把当前标注交给 EngineBridge 序列化。
    const annotation::AnnotationSet& set() const { return set_; }
    // 当前有效标注列表（＝撤销主栈，只读）。
    const std::vector<annotation::Annotation>& annotations() const { return set_.annotations(); }

    // ------ 当前工具属性 ------
    EditType editType() const { return editType_; }
    void setEditType(EditType t);           // 切模式时清空选中（避免跨模式残留）。

    geometry::ShapeType currentType() const { return currentType_; }
    void setCurrentType(const geometry::ShapeType t) { currentType_ = t; }

    const core::Color& currentColor() const { return currentColor_; }
    const core::Color& currentFillColor() const { return currentFillColor_; }
    int currentStrokeWidth() const { return currentStrokeWidth_; }
    bool currentFill() const { return currentFill_; }
    const std::string& currentText() const { return currentText_; }
    double currentFontSize() const { return currentFontSize_; }
    double currentCornerRadius() const { return currentCornerRadius_; }

    // 修改当前属性；EDIT 模式作用到 selectedAnnotation，DRAW 模式改当前默认（语义同原 AnnotationLayer）。
    void changeColor(const core::Color& c);
    void changeFillColor(const core::Color& c);
    void changeStrokeWidth(int w);
    void changeFill(bool fill);
    void changeText(const std::string& text);
    void changeFontSize(double size);
    void changeCornerRadius(double r);

    // ------ 形状提交与编辑 ------
    // 提交一个新形状：以当前属性模板组装 Annotation，入集合（清空重做栈）。空 shape 忽略。
    void addAnnotation(std::unique_ptr<geometry::Shape> shape);

    // 命中检测：委托集合（顶层优先，填充判内部、其余判轮廓邻近），未命中 nullopt。
    std::optional<std::size_t> hitTest(const core::Point2D& p) const { return set_.hitTest(p); }

    void selectAnnotation(std::optional<std::size_t> idx) { selectedIndex_ = idx; }
    std::optional<std::size_t> selectedAnnotationIndex() const { return selectedIndex_; }
    annotation::Annotation* selectedAnnotation();
    const annotation::Annotation* selectedAnnotation() const;

    // ------ 撤销 / 重做 / 清除 ------
    void revoke();   // 集合弹尾 → 重做栈；空集合不动。
    void redo();     // 重做栈顶 → 集合追加；空重做栈不动。
    void clear();    // 清集合 + 清重做栈 + 取消选中（保留底图）。
    // 删除指定下标（越界忽略）：属新操作清重做；同步修正选中下标。
    void removeAnnotation(std::size_t index);
    // 层序调整：from→to（下标即 z 序，大＝顶）；越界或 from==to 忽略；属新操作清重做；选中跟随。
    void moveAnnotation(std::size_t from, std::size_t to);
    // 整体替换（配置还原专用）：集合 restore + 清重做栈 + 取消选中（不推撤销快照）。
    void restoreAnnotations(std::vector<annotation::Annotation> items);

    // ------ 合成 ------
    // 以所持底图合成（委托集合 burnIn）；或显式传入 base（供底图与内部不一致的场景）。
    core::Image burnIn() const { return set_.burnIn(image_); }
    core::Image burnIn(const core::Image& base) const { return set_.burnIn(base); }

private:
    // EDIT 模式且选中有效时返回选中项可写引用，否则 nullptr。
    annotation::Annotation* editableSelected();

    annotation::AnnotationSet set_;               // 数据集合（唯一标注真相源，含 zOrder 不变式）
    std::vector<annotation::Annotation> redoStack_; // 重做栈（撤销弹尾入此、重做回推集合）
    core::Image image_;                           // 底图（供无参 burnIn）

    EditType editType_{EditType::DRAW};
    geometry::ShapeType currentType_{geometry::ShapeType::LINE};
    core::Color currentColor_{core::kBlack};
    core::Color currentFillColor_{core::kBlack};
    bool fillColorLinked_{true};   // 填充色是否跟随边框色（未显式改过填充色时为真）
    int currentStrokeWidth_{2};
    bool currentFill_{false};
    std::string currentText_{"A"};
    double currentFontSize_{200.0};
    double currentCornerRadius_{0.0};

    std::optional<std::size_t> selectedIndex_;
};

} // namespace idc::gui
