// ============================================================================
// 文件：model/annotation_bridge.h
// 作用：标注域的「桥」——GUI 中唯一持有并驱动标注编辑会话 AnnotationEditor 的类
//       （类比 EngineBridge 之于切割引擎）。它把画布手势 / 面板意图翻译成对会话 / Core 标注
//       API 的调用（buildShape / addAnnotation / hitTest / rasterize …），自身不含任何
//       几何计算或光栅化实现（CONTRIBUTING.md「分层纪律」）。
// 分块依据：
//   - AnnoTool         : GUI 侧标注工具枚举（选择 + 各形状 + 多线段 + 文字 + 画笔），
//                        与 Core geometry::ShapeType 对应（Select 无对应形状）。
//   - AnnotationBridge : 拥有 AnnotationEditor（内含 Core annotation::AnnotationSet 数据集合），
//                        暴露绘制 / 选择 / 移动 / 删除 / 撤销重做 / 图层可见性 / 导出烧录等意图方法，
//                        并以信号驱动画布重绘。
// 说明：
//   · 标注层与底图**分离**——画布用矢量叠加渲染（见 canvas/annotation_item），不改底图像素；
//     仅在导出时按需 burnIn(base) 合成（CONTRIBUTING.md「分层纪律」 标注与切割解耦、CONTRIBUTING.md「分层纪律」 预处理→标注→切割→导出）。
//   · 撤销 / 重做复用 AnnotationEditor 内建 revoke()/redo()（条目级会话态），GUI 不自建历史栈。
// ============================================================================
#pragma once

#include <QObject>

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "core/color.h"
#include "core/image.h"
#include "core/point.h"
#include "model/annotation_editor.h"
#include "geometry/path.h"
#include "geometry/shape_type.h"
#include "geometry/shapes.h"

namespace idc::gui {

// ---------------------------------------------------------------------------
// AnnoTool：GUI 标注工具枚举。
// 分块依据：与 Core ShapeType 对应，但额外含 Select（选择 / 移动，无几何形状）。
// ---------------------------------------------------------------------------
enum class AnnoTool {
    Select,          // 选择 / 移动（Edit 模式）
    Rect,            // 矩形
    Square,          // 正方形
    Rhombus,         // 菱形
    Circle,          // 圆形
    Ellipse,         // 椭圆
    IsosTri,         // 等腰三角形
    EquTri,          // 等边三角形
    RectTri,         // 直角三角形
    EquRectTri,      // 等腰直角三角形
    Line,            // 直线
    Polyline,        // 多线段（连续追加顶点）
    Text,            // 文字
    Brush,           // 画笔（自由路径）
};

// 将 GUI 工具映射为 Core 形状类型；Select 无对应形状，返回 Line 作占位（不会被使用）。
geometry::ShapeType shapeTypeForTool(AnnoTool tool);

// ---------------------------------------------------------------------------
// AnnotationBridge：标注域桥（QObject，信号驱动画布 / 面板刷新）。
// ---------------------------------------------------------------------------
class AnnotationBridge : public QObject {
    Q_OBJECT
public:
    explicit AnnotationBridge(QObject* parent = nullptr);

    // ---- 底图与会话 ----
    // 设置底图：委托 editor 开新标注会话（内部清空旧标注；**不拷贝持有像素**）。
    // 载入新图 / 维度变化的预处理后调用；同维度预处理不清标注（矢量叠加 + 导出注入当前 base）。
    void setBaseImage(const core::Image& base);

    // ---- 工具与属性 ----
    AnnoTool currentTool() const { return tool_; }
    // 切换工具：Select→Edit 模式（属性作用选中项），其余→Draw 模式（属性作为下一次绘制默认）。
    void setTool(AnnoTool tool);

    core::Color currentColor() const { return editor_.currentColor(); }
    core::Color currentFillColor() const { return editor_.currentFillColor(); }
    int currentStrokeWidth() const { return editor_.currentStrokeWidth(); }
    bool currentFill() const { return editor_.currentFill(); }
    std::string currentText() const { return editor_.currentText(); }
    double currentFontSize() const { return editor_.currentFontSize(); }
    double currentCornerRadius() const { return editor_.currentCornerRadius(); }

    // 写回属性：委托 change*（Edit 模式作用选中项，Draw 模式改当前默认），随后发 changed()。
    void setColor(const core::Color& c);   // 边框 / 描边色
    void setFillColor(const core::Color& c); // 填充色（独立于边框色）
    void setStrokeWidth(int w);
    void setFill(bool fill);
    void setText(const std::string& text);
    void setFontSize(double size);
    void setCornerRadius(double r);   // 转角圆角半径（px，0＝直角）

    // ---- 两点形状绘制流程（矩形 / 圆 / 直线 / 三角 …）----
    void beginShape(core::Point2D p);   // 按下：记起点，切 Draw，准备预览
    void updateShape(core::Point2D p);  // 拖拽：以当前点重建 pending 预览，发 changed()
    void commitShape();                      // 释放：把 pending 提交为一个标注，发 changed()
    void cancelPending();                    // 取消当前预览（Esc / 切换工具）

    // ---- 折线 / 画笔绘制流程（连续追加顶点）----
    void beginPath(core::Point2D p);    // 起笔：新建路径草稿并 moveTo
    void appendPathPoint(core::Point2D p); // 追加一个正式顶点（lineTo），刷新预览
    // 折线橡皮筋预览：以路径草稿 + 到光标 cursor 的临时连线构造 pending（**不改 pathDraft_**），
    // 供「移动时实时预览落点与连线」；左键再次点击才由 appendPathPoint 落为正式顶点。
    void previewPolyline(core::Point2D cursor);
    void commitPath();                       // 收笔：把路径草稿提交为一个标注
    bool hasPathDraft() const { return !pathDraft_.empty(); }

    // ---- 文字 ----
    void addText(core::Point2D p, const std::string& text); // 在 p 处添加文字标注

    // ---- 选择 / 移动 / 删除（复用 Core，不在 GUI 算几何）----
    void selectAt(core::Point2D p);     // 按下选中（命中栈内保持当前选中，否则选最顶；委托 editor_.pickAt）
    void cycleAt(core::Point2D p);      // 原地点击推进同点连击循环（重叠标注轮流选中；委托 editor_.cycleAt）
    void clearSelection();                   // 取消选中
    std::optional<std::size_t> selectedIndex() const { return editor_.selectedAnnotationIndex(); }
    // 平移选中标注：委托 Core Shape::translateWorld 就地偏移形状自身参数，**保留具体类型**
    // （不展平重建为通用 Path，故 Text 字形、参数化元数据与控制点语义均不丢失）。
    void moveSelectedBy(double dx, double dy);
    void removeSelected();                   // 删除选中标注（委托 Core removeAnnotation）

    // ---- 变换选中标注（缩放 / 旋转 / 翻转）----
    // 委托 Core Shape 的**非破坏性仿射矩阵** applyObbTransform（沿自身轴局部缩放 + 绕世界中心刚性旋转），
    // 保留具体类型与文字字形（不退化为 PATH）。sx/sy 为相对当前的缩放增量系数（1.0=不变、负即翻转）、
    // rotateDeg 为旋转角增量；无选中或无变化则忽略。属性面板与画布 OBB 手柄共用本入口。
    void transformSelected(double sx, double sy, double rotateDeg);

    // ---- 层序（叠放次序）：以标注下标为 z 序（大＝顶，见 hitTest/burnIn）----
    // 均作用于选中项，委托 AnnotationEditor::moveAnnotation 重排；无选中则忽略。随后发 changed()/selectionChanged()。
    void raiseSelected();         // 上移一层（向顶层）
    void lowerSelected();         // 下移一层（向底层）
    void bringSelectedToFront();  // 置于顶层
    void sendSelectedToBack();    // 置于底层

    // ---- 撤销 / 重做 / 清除（复用 Core 内建分层快照）----
    void undo();                             // → Core revoke()
    void redo();                             // → Core redo()
    void clearAll();                         // → Core clear()

    // 只读引用底层集合：供保存配置时把当前标注交给 EngineBridge::saveConfig 序列化。
    const annotation::AnnotationSet& set() const { return editor_.set(); }
    // 用载入的标注整体替换当前列表（委托 AnnotationEditor::restoreAnnotations：不推撤销快照、取消选中），
    // 随后发 changed()/selectionChanged() 驱动画布与面板刷新。调用方须保证已在预处理队列
    // 重放（含尺寸门清标注）之后执行，否则还原会被尺寸门误清。
    void replaceAnnotations(std::vector<annotation::Annotation> items);

    // ---- 图层可见性 / 导出烧录（GUI 侧标志，Core 无关）----
    bool layerVisible() const { return layerVisible_; }
    void setLayerVisible(bool on);
    bool burnInEnabled() const { return burnInEnabled_; }
    void setBurnIn(bool on);

    // ---- 导出合成 ----
    // 以传入的 base（当前工作图）为底，逐个调 Core rasterize 合成所有标注，返回新 RGBA 图。
    // 不复用会话所持底图（其可能预处理后陈旧），确保与最新底图一致。
    core::Image burnIn(const core::Image& base) const;

    // ---- 只读访问（供画布渲染）----
    const std::vector<annotation::Annotation>& annotations() const { return editor_.annotations(); }
    std::size_t count() const { return editor_.annotations().size(); }
    // 当前拖拽预览形状（未提交）；无预览时返回 nullptr。画布据此画橡皮筋矢量。
    const geometry::Shape* pendingShape() const { return pending_.get(); }

    // ---- 属性回显（供右侧标注属性页）----
    // 有选中项则返回其属性，否则返回当前工具默认属性。
    core::Color displayColor() const;
    // 选中形状的填充色（有选中取其 fillColor），否则当前默认填充色。
    core::Color displayFillColor() const;
    int displayStrokeWidth() const;
    bool displayFill() const;
    // 选中形状的转角圆角半径（取形状几何），否则当前默认；无选中时取 layer 当前值。
    double displayCornerRadius() const;
    // 当前上下文是否支持转角倒角 / 内部填充（供属性面板置灰不适用控件）：有选中按选中形状类型
    // （直线 / 圆 / 椭圆 / 文字无转角；仅直线 / 文字不可填充）；绘制工具按工具类型；Select 无选中一律可用
    // （编辑的是下一次绘制的默认值，不受上一工具残值错禁）。
    bool contextSupportsCornerRadius() const;
    bool contextSupportsFill() const;
    // 选中标注的累积 OBB 变换参数（绝对值、带符号；缩放为负即翻转）——供属性面板忠实回显底层真实变换。
    // 无选中 / 无形状时返回恒等（1,1,0）。
    double displayObbScaleX() const;
    double displayObbScaleY() const;
    double displayObbRotationDeg() const;

signals:
    void changed();           // 标注列表 / 属性变化：画布需全量重绘标注层
    void pendingChanged();    // 仅拖拽预览（橡皮筋）变化：画布只刷新预览图元（避免逐帧重建已提交标注）
    void selectionChanged();  // 选中项变化：画布高亮 + 属性面板同步
    void toolChanged();       // 工具变化：面板按钮组同步

private:
    // 依当前工具与预览锚点重建 pending_（两点形状用 p1/p2，折线 / 画笔用 pathDraft_）。
    void rebuildPending(core::Point2D current);

    AnnotationEditor editor_;  // 标注编辑会话（内含 Core AnnotationSet 数据集合，唯一真相源）
    AnnoTool tool_{AnnoTool::Select};

    // 绘制预览状态（仅 GUI 侧临时，不进 Core 历史）
    core::Point2D anchor_{};                       // 两点形状起点
    std::unique_ptr<geometry::Shape> pending_;     // 当前预览形状
    geometry::Path pathDraft_;                     // 折线 / 画笔路径草稿

    bool layerVisible_{true};    // 标注图层可见性（图层面板开关）
    bool burnInEnabled_{false};  // 导出时是否烧录标注（图层面板开关）
};

} // namespace idc::gui
