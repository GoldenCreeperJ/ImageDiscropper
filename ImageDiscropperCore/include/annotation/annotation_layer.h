// ============================================================================
// 文件：include/annotation/annotation_layer.h
// 作用：标注会话的核心状态机，管理标注列表、当前工具、当前属性、以及撤销重做逻辑。
// 分块依据：
//   - Annotation        : 一次提交的标注记录（几何 + 样式）
//   - EditType      : 当前编辑模式（DRAW / EDIT / MOVE）
//   - AnnotationLayer  : 会话状态机，聚合上述数据与撤销重做栈
// ============================================================================
#pragma once

#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "core/color.h"
#include "core/image.h"
#include "core/point.h"
#include "annotation/rasterizer.h"
#include "geometry/shape_type.h"
#include "geometry/shapes.h"
#include "history/history_manager.h"

namespace idc::annotation {

// ---------------------------------------------------------------------------
// Annotation：一个已提交的形状记录（几何 + 样式属性）
// ---------------------------------------------------------------------------
struct Annotation {
    std::unique_ptr<geometry::Shape> shape;  // 几何数据（多态）
    core::Color color{core::kBlack};         // 颜色
    int strokeWidth{2};                       // 线宽
    bool fillType{false};                     // 是否填充
    geometry::ShapeType shapeType{geometry::ShapeType::LINE}; // 形状类型

    // 拷贝构造：深拷贝 shape。
    Annotation(const Annotation& o);
    Annotation& operator=(const Annotation& o);
    Annotation(Annotation&&) noexcept = default;
    Annotation& operator=(Annotation&&) noexcept = default;
    Annotation() = default;
};

// ---------------------------------------------------------------------------
// EditType：编辑模式
// 分块依据：每种模式对应用户在 GUI 中选择的工具类别，只保留纯逻辑所需的三种。
// ---------------------------------------------------------------------------
enum class EditType {
    DRAW,   // 绘制新形状
    EDIT,   // 选中并修改已有形状
    MOVE,   // 平移视图
};

// 将字符串（"Draw"/"Edit"/"Move"）解析为 EditType。
EditType parseEditType(const std::string& s);
// 将 EditType 转换为字符串，便于日志。
const char* editTypeName(EditType t);

// ---------------------------------------------------------------------------
// AnnotationLayer：标注会话状态
// ---------------------------------------------------------------------------
class AnnotationLayer {
public:
    AnnotationLayer();

    // ------ 图像与形状访问 ------
    // 设置底图（拷贝一份内部持有）。
    void setImage(const core::Image& img);
    // 获取当前底图（只读引用）。
    const core::Image& image() const { return image_; }
    // 获取当前形状列表（只读引用）——即撤销重做主栈（当前有效标注）。
    const std::vector<Annotation>& annotations() const { return history_.undoStack(); }

    // ------ 当前工具属性 ------
    EditType editType() const { return editType_; }
    void setEditType(EditType t);

    geometry::ShapeType currentType() const { return currentType_; }
    void setCurrentType(const geometry::ShapeType t) { currentType_ = t; }

    const core::Color& currentColor() const { return currentColor_; }
    int currentStrokeWidth() const { return currentStrokeWidth_; }
    bool currentFill() const { return currentFill_; }
    const std::string& currentText() const { return currentText_; }
    double currentFontSize() const { return currentFontSize_; }

    // 修改当前属性；在 EDIT 模式下会作用到 selectedAnnotation。
    void changeColor(const core::Color& c);
    void changeStrokeWidth(int w);
    void changeFill(bool fill);
    void changeText(const std::string& text);
    void changeFontSize(double size);

    // ------ 形状提交与编辑 ------
    // 提交一个新形状（由 ShapeFactory 构造后调用）。
    void addAnnotation(std::unique_ptr<geometry::Shape> shape);

    // 命中检测：返回顶层第一个包含 p 的形状索引，未命中返回 nullopt。
    std::optional<std::size_t> hitTest(const core::Point2D& p) const;

    // 设置选中形状索引（用于 EDIT 模式）。
    void selectAnnotation(std::optional<std::size_t> idx);
    std::optional<std::size_t> selectedAnnotationIndex() const { return selectedIndex_; }
    // 返回当前选中形状指针，可能为 nullptr。
    Annotation* selectedAnnotation();
    const Annotation* selectedAnnotation() const;

    // ------ 撤销 / 重做 / 清除 ------
    // 撤销：把最后一个形状移入重做栈（history_.popToRedo），可再次 redo 恢复。
    void revoke();
    // 重做：从重做栈恢复最近撤销的形状（history_.popFromRedo）。
    void redo();
    // 清除所有形状（同时清空重做栈），保留底图。
    void clear();

    // ------ 合成 ------
    // 将当前底图与所有形状合成为一张 RGBA 图像并返回，不修改内部状态。
    core::Image burnIn() const;

private:
    core::Image image_;                              // 当前底图
    // 已提交形状列表 + 撤销重做：复用通用 history::HistoryManager<Annotation>——其主栈
    // （undoStack）即「当前有效标注列表」，重做栈承载被撤销、等待恢复的标注；不再自建
    // annotations_ / garbage_ 两个向量（消除与 history 模块的重复实现）。
    history::HistoryManager<Annotation> history_;
    EditType editType_{EditType::DRAW};              // 当前编辑模式

    // 当前工具属性
    geometry::ShapeType currentType_{geometry::ShapeType::LINE};
    core::Color currentColor_{core::kBlack};
    int currentStrokeWidth_{2};
    bool currentFill_{false};
    std::string currentText_{"A"};
    double currentFontSize_{200.0};

    // 选中形状索引（EDIT 模式）
    std::optional<std::size_t> selectedIndex_;
};

} // namespace idc::annotation
