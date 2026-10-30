// ============================================================================
// 文件：canvas/items/annotation_item.h
// 作用：画布上的「单个标注」矢量图元——持有一份 Core annotation::Annotation 拷贝，
//       用 Shape::worldPath() → QPainterPath 以矢量方式叠加渲染（不改底图像素，CONTRIBUTING.md「分层纪律」）。
//       选中时**定向包围盒（OBB）+ 8 个缩放手柄 + 1 个旋转手柄**由独立**顶层**图元绘制在
//       kAnnotationPreview 层（z=25，见 z_order.h）——与标注绘制预览（橡皮筋）同层、恒位于已提交
//       标注之上；Qt 6 场景按顶层图元子树递归绘制，子图元 z 逃不出父级子树、压不过同层后画的标注，
//       故必须独立成层；形状本体仍在本图元（标注层）。
//       手柄拖拽做缩放/拉伸/旋转（拖角越过对边产生负缩放＝翻转），拖拽中实时矢量预览、释放发
//       transformRequested(sx,sy,deg)；拖形状本体则平移，释放发 moveRequested(dx,dy)。
//       二者均交上层委托 Core（本图元不自算几何，CONTRIBUTING.md「分层纪律」）。
// 分块依据：
//   - 继承 DraggableCanvasItem：平移手势循环与 setPos 视觉反馈在基类（见 canvas/items/draggable_canvas_item.h），
//     本图元只留送达翻译 onDragFinished（clicked / moveRequested 信号）；手柄拖拽（缩放/旋转）自管。
//   - 形状几何全在 Core（buildShape/toPath），本图元只做「渲染一条既有形状 + 拖动它」。
//   - 拖拽安全：拖动期间只做 setPos 视觉反馈，释放时先复位 pos 再发信号（基类保证），
//     且场景以增量方式刷新（数量不变→不删除正在拖动的图元），避免自删崩溃。
// 说明：z 序 = zorder::kAnnotation，位于底图之上、遮罩/网格/选区之下（本目录 README「图层 z 序」）。
// ============================================================================
#pragma once

#include <QPainterPath>
#include <QPointF>

#include <optional>

#include "annotation/annotation_set.h"
#include "canvas/items/draggable_canvas_item.h"   // 平移拖拽手势基类（手势循环 + setPos 视觉反馈；本类留送达翻译）
#include "core/color.h"            // core::Color（colorAtScene 返回值：标注自身显示色）
#include "geometry/shapes.h"   // AffineTransform / BoundingBox（OBB 手柄拖拽预览用）
#include "util/gui_nums.h"     // nums::canvas（手柄尺寸的场景口径兜底初值）

namespace idc::gui {

// ---------------------------------------------------------------------------
// AnnotationItem：单个标注的矢量渲染图元（可拖动平移）。
// ---------------------------------------------------------------------------
class AnnotationItem : public DraggableCanvasItem {
    Q_OBJECT
public:
    explicit AnnotationItem(QGraphicsItem* parent = nullptr);
    ~AnnotationItem() override;   // 释放顶层 OBB 覆盖层（非子图元，不随父级自动销毁）

    // 载入一份标注拷贝并重建渲染路径（几何来自 Core Shape::toPath）。
    void setAnnotation(const annotation::Annotation& ann);

    // 选中态：绘制虚线高亮框 + 控制点，且允许被拖动平移。
    void setSelectedState(bool on);
    bool isSelectedState() const { return selected_; }

    // 控制点手柄边长（场景单位）；由 CanvasView 依缩放换算，使其屏幕观感恒定。
    void setHandleSize(qreal sceneUnits);

    // 是否处于任意手势中：平移拖拽（基类手势）或手柄拖拽（activeHandle_）均计——
    // 上层据此在拖拽期跳过对本图元的几何回设（updateAnnotations 护栏）。
    bool isDragging() const override {
        return DraggableCanvasItem::isDragging() || activeHandle_ != Handle::None;
    }

    // 选中标注的 OBB 手柄优先命中（场景在基类分发前调用，标注选取态）：命中即进入手柄拖拽并抓取鼠标，
    // 后续移动/释放直达本图元——上层标注盖住手柄点位时按下也能穿透落到选中标注（选中框恒可操作）。
    // 未命中（未选中/无形状/不在手柄半径内）返回 false，场景交基类走正常分发。
    bool tryHandlePress(const QPointF& scenePos);

    // 外部打断（缩放抢占）取消手柄拖拽：弃掉预览回到当前形状，不发 transformRequested（提交语义只属于释放）。
    // 与 tryHandlePress 的显式抓取配套：抓取被场景解除后释放不再到达本图元，activeHandle_ 必须就地归位，
    // 否则 isDragging() 恒真——上层跳过对本图元的几何回设，此标注从此拒绝模型回写且永久显示预览态 OBB。
    // 解除抓取由场景统一负责（本方法不碰鼠标）。
    void cancelHandleDrag();

    // 平移拖拽由场景驱动（按下归属与移动/释放转发都在 CanvasScene）：手势循环在基类
    // DraggableCanvasItem，本图元经 onDragFinished 收到收尾数据并翻译成自身信号。

    // 命中场景坐标处「本标注的显示色」（供状态栏取色优先于底图像素——标注是矢量叠加、不进 working()）：
    // 依渲染规则判定——描边叠于填充之上故先描边后填充；文字取其描边色（近似，按字形包围盒）。
    // 命中返回该分量颜色，未命中返回 nullopt。用缓存 path_（拖拽预览态为起始几何，取色场景无碍）。
    std::optional<core::Color> colorAtScene(const QPointF& scenePos) const;

    // QGraphicsItem 接口。
    QRectF boundingRect() const override;
    QPainterPath shape() const override;   // 抓取轮廓（填充形状与文字含整个内部，余取描边带），改善拖动手感
    void paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget) override;

signals:
    // 左键按下本图元：上报场景坐标，交上层用 Core hitTest 决定选中项（几何命中在 Core，CONTRIBUTING.md「分层纪律」）。
    // 选中会同步骤回灌 setSelectedState(true)，拖动由场景转发给选中图元（单击选中 + 同手势移动）。
    void pressed(const QPointF& scenePos);
    // 原地点击（按下-释放无移动）：交上层推进同点连击循环（重叠标注轮流选中），不触发平移。
    void clicked(const QPointF& scenePos);
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
    // 同步顶层 OBB 覆盖层的场景进出/位置/显隐（顶层图元不随父级自动进出场景、不随父级变换）。
    QVariant itemChange(GraphicsItemChange change, const QVariant& value) override;
    // 平移手势送达（基类钩子）：wasClick→clicked(scenePos)（交上层推进同点连击循环）；
    //   否则→moveRequested(dx,dy)（仅选中且净位移非零时）。
    void onDragFinished(bool wasClick, const QPointF& scenePos, double dx, double dy) override;

private:
    void rebuildPath();   // 由 ann_.shape 重建 path_ 与 bounds_

    // 选中 OBB 高亮框：**顶层图元**（QGraphicsItem 父为空，仅持有 owner 指针），绝对 z＝kAnnotationPreview，
    //   只绘 OBB 与手柄、不接鼠标（手柄抓取仍在本图元 shape()/mousePress 判定）。顶层是硬约束——Qt 6
    //   场景按顶层图元子树递归绘制，子图元 z 逃不出父级子树、压不过同层后画的标注。位置跟随 owner
    //   （场景进出/pos/显隐经 AnnotationItem::itemChange 同步）；坐标系＝owner 局部系（经 pos 平移）。
    //   嵌套类＝本类成员，可访问私有成员（ann_/computeObb/handleSize_ 等）。
    class ObbOverlayItem : public QGraphicsItem {
    public:
        explicit ObbOverlayItem(AnnotationItem* owner);
        QRectF boundingRect() const override;
        void paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget) override;
        // 父图元几何/选中变化时调用：报备包围盒变更并重绘（prepareGeometryChange 为保护成员，故经此入口）。
        void syncGeometry();

    private:
        AnnotationItem* owner_;
    };
    ObbOverlayItem* obbOverlay_{nullptr};

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
    qreal handleSize_{nums::canvas::kFallbackHandleScenePx};   // 视图下发前的场景兜底初值（见 util/gui_nums.h canvas 组）。

    // 手柄拖拽（缩放/旋转）状态：与平移互斥（activeHandle_ != None 时走预览分支）。
    Handle activeHandle_{Handle::None};
    QPointF dragCenterWorld_{};   // 手势起点时 OBB 世界中心（旋转参考）
    QPointF rotateStartVec_{};    // 起点时旋转手柄相对中心的方向（算旋转增量）
    double previewSx_{1.0};       // 预览缩放系数（相对起点）
    double previewSy_{1.0};
    double previewRot_{0.0};      // 预览旋转角度（度，相对起点）
};

} // namespace idc::gui
