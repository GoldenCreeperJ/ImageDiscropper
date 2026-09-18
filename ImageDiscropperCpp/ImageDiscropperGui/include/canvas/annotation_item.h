// ============================================================================
// 文件：canvas/annotation_item.h
// 作用：画布上的「单个标注」矢量图元——持有一份 Core annotation::Annotation 拷贝，
//       用 Shape::worldPath() → QPainterPath 以矢量方式叠加渲染（不改底图像素，A-0.15）。
//       选中时绘制**定向包围盒（OBB）+ 8 个缩放手柄 + 1 个旋转手柄**：拖手柄做缩放/拉伸/旋转
//       （拖角越过对边产生负缩放＝翻转），拖拽中实时矢量预览、释放发 transformRequested(sx,sy,deg)；
//       拖形状本体则平移，释放发 moveRequested(dx,dy)。二者均交上层委托 Core（本图元不自算几何，A-0.1）。
// 分块依据：
//   - 仿 SelectionRectItem 的 QGraphicsObject 范式（Q_OBJECT + 自绘 paint + 自管拖拽），
//     但只处理「渲染一条既有形状 + 拖动它」，形状几何全在 Core（buildShape/toPath）。
//   - 拖拽安全（见记忆教训）：拖动期间只做 setPos 视觉反馈，释放时先复位 pos 再发信号，
//     且场景以增量方式刷新（数量不变→不删除正在拖动的图元），避免自删崩溃。
// 说明：z 序 = zorder::kAnnotation(10)，位于底图之上、遮罩/网格/选区之下（§4.2 图层顺序）。
// ============================================================================
#pragma once

#include <QGraphicsObject>
#include <QPainterPath>
#include <QPointF>

#include "annotation/annotation_layer.h"
#include "geometry/shapes.h"   // AffineTransform / BoundingBox（OBB 手柄拖拽预览用）

namespace idc::gui {

// ---------------------------------------------------------------------------
// AnnotationItem：单个标注的矢量渲染图元（可拖动平移）。
// ---------------------------------------------------------------------------
class AnnotationItem : public QGraphicsObject {
    Q_OBJECT
public:
    explicit AnnotationItem(QGraphicsItem* parent = nullptr);

    // 载入一份标注拷贝并重建渲染路径（几何来自 Core Shape::toPath）。
    void setAnnotation(const annotation::Annotation& ann);

    // 选中态：绘制虚线高亮框 + 控制点，且允许被拖动平移。
    void setSelectedState(bool on);
    bool isSelectedState() const { return selected_; }

    // 控制点手柄边长（场景单位）；由 CanvasView 依缩放换算，使其屏幕观感恒定。
    void setHandleSize(qreal sceneUnits);

    // 是否正处于拖动平移手势中（供上层在拖拽期间跳过对该图元的几何回设）。
    bool isDragging() const { return dragging_; }

    // QGraphicsItem 接口。
    QRectF boundingRect() const override;
    QPainterPath shape() const override;   // 描边轮廓（填充形状则含内部），改善拖动抓取手感
    void paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget) override;

signals:
    // 左键按下本图元：上报场景坐标，交上层用 Core hitTest 决定选中项（几何命中在 Core，A-0.1）。
    // 选中会同步骤回灌 setSelectedState(true)，于是本次手势即可接着拖动平移（单击选中 + 同手势移动）。
    void pressed(const QPointF& scenePos);
    // 拖动平移结束：请求把该标注平移 (dx,dy) 个场景（原图）像素。上层委托 Core 重建几何。
    void moveRequested(double dx, double dy);
    // 拖拽定向包围盒手柄结束：请求对选中标注施加缩放(sx,sy)+旋转(rotateDeg)（相对手势起点，
    // Core 局部系）。sx/sy 为负即翻转（拖手柄越过对边）。上层委托 Core applyObbTransform。
    void transformRequested(double sx, double sy, double rotateDeg);
    // 手柄拖拽**进行中**逐帧上报当前预览的缩放/旋转（相对手势起点），供属性面板数值实时联动显示；
    // 仅用于回显，不写模型（提交仍由释放时的 transformRequested 一次性完成）。
    void transformPreview(double sx, double sy, double rotateDeg);

protected:
    void mousePressEvent(QGraphicsSceneMouseEvent* event) override;
    void mouseMoveEvent(QGraphicsSceneMouseEvent* event) override;
    void mouseReleaseEvent(QGraphicsSceneMouseEvent* event) override;

private:
    void rebuildPath();   // 由 ann_.shape 重建 path_ 与 bounds_

    // 定向包围盒（OBB）手柄：4 角双向缩放 + 4 边中点单向缩放 + 1 旋转手柄（仅选中时可拖）。
    enum class Handle { None, TL, T, TR, R, BR, B, BL, L, Rotate };
    // 依给定世界变换 xf 计算 OBB 的9 个手柄世界坐标（局部包围盒四角/四边中点经 xf 映射）。
    struct ObbFrame {
        QPointF corner[4];   // TL, TR, BR, BL
        QPointF edge[4];     // T, R, B, L 边中点
        QPointF rotate;      // 旋转手柄（顶边中点外推）
        QPointF center;      // OBB 中心
    };
    ObbFrame computeObb(const geometry::AffineTransform& xf) const;
    Handle hitHandle(const QPointF& scenePos, const ObbFrame& f) const;   // 命中哪个手柄（半径 handleSize_）
    void beginHandleDrag(Handle h, const QPointF& scenePos);            // 进入手柄拖拽：捕获起点参考
    void updateHandleDrag(const QPointF& scenePos);                      // 拖拽：算 sx/sy/rot 预览参数
    void updatePreviewBounds();                                          // 按预览形状扩展 bounds_（避免裁剪）

    annotation::Annotation ann_;   // 标注拷贝（几何 + 样式）
    QPainterPath path_;                 // 缓存的矢量路径（Shape::worldPath 翻译而来）
    QRectF bounds_{};                   // path_ 包围盒 + 线宽/手柄余量

    bool selected_{false};
    qreal handleSize_{8.0};

    bool dragging_{false};
    QPointF lastScenePos_{};   // 上一次场景坐标（平移增量用）
    QPointF accumDelta_{};     // 本次手势累计平移量（setPos 视觉反馈）

    // 手柄拖拽（缩放/旋转）状态：与平移互斥（activeHandle_ != None 时走预览分支）。
    Handle activeHandle_{Handle::None};
    QPointF dragCenterWorld_{};   // 手势起点时 OBB 世界中心（旋转参考）
    QPointF rotateStartVec_{};    // 起点时旋转手柄相对中心的方向（算旋转增量）
    double previewSx_{1.0};       // 预览缩放系数（相对起点）
    double previewSy_{1.0};
    double previewRot_{0.0};      // 预览旋转角度（度，相对起点）
};

} // namespace idc::gui
