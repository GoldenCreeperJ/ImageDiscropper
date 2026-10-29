// ============================================================================
// 文件：canvas/canvas_scene.h
// 作用：画布场景——按 z 序图层化组织：底图、遮罩、切割线、选区框。
//       场景坐标统一为「原图像素坐标」；底图用降采样 pixmap 经变换铺到原图尺寸，
//       于是遮罩/切割线/选区都能直接按原图坐标叠加（无需到处做坐标换算）。
// 分块依据：
//   - CanvasScene 只做「图层装配与刷新调度」，把遮罩交给 MaskLayer、选区与切割线交给 SelectionRectItem
//     （切割线即选区标记边的贯穿延伸，同一橙色图元、可直接拖边），自身不实现任何切割/几何（CONTRIBUTING.md「分层纪律」）。
//   - 所有数据来自 Core（EngineResult.kept / CutLineSet）与 Document（选区矩形），场景只渲染。
// 说明：交互（缩放/平移/框选/微调/右键）在 CanvasView；场景只持有并刷新图元。
// ============================================================================
#pragma once

#include <QGraphicsRectItem>
#include <QGraphicsScene>
#include <QPixmap>
#include <QPointF>
#include <QVector>

#include <cstddef>
#include <optional>
#include <vector>

class QGraphicsSceneMouseEvent;

#include "canvas/layer.h"
#include "canvas/pick_cycle.h"   // 同点连击循环选层状态机（与标注拾取共用同实现）
#include "canvas/items/draggable_canvas_item.h"   // 场景驱动平移拖拽的图元基类（拖拽句柄成员类型）
#include "core/color.h"        // core::Color（annotationColorAt 返回值）
#include "canvas/items/mask_layer.h"
#include "canvas/items/grid_layer.h"
#include "canvas/items/cell_picker_item.h"
#include "canvas/items/selection_rect_item.h"
#include "canvas/items/annotation_item.h"
#include "canvas/items/image_layer_item.h"
#include "engine/engine.h"
#include "util/gui_nums.h"   // nums::canvas（手柄尺寸的场景口径兜底初值）

namespace idc::gui {

class AnnotationBridge;   // 前置声明：updateAnnotations 只取 const 引用（避免 canvas→model 头耦合）

// ---------------------------------------------------------------------------
// CanvasScene：图层化画布场景。
// ---------------------------------------------------------------------------
class CanvasScene : public QGraphicsScene {
    Q_OBJECT
public:
    explicit CanvasScene(QObject* parent = nullptr);

    // 设置底图：preview 为降采样 pixmap，scaleX/Y 为预览→原图放大系数，fullW/H 为原图尺寸。
    // 会同步按 applySceneExtent 重设场景矩形与选区框的图像边界。
    void setBaseImage(const QPixmap& preview, double scaleX, double scaleY, int fullW, int fullH);

    // 清空画布全部内容（换图前 / 关闭图像时）。
    void clearAll();

    // 依 Core 结果刷新保留(绿)/删除(红)遮罩。
    void updateMasks(const engine::EngineResult& result);
    // 遮罩显隐（右键菜单「切换预览遮罩」）。
    void setMasksVisible(bool visible);
    bool masksVisible() const { return masksVisible_; }

    // Palette 改色后刷新需重建 pen 的图层（当前仅网格/诱导切割线：pen 在 rebuild 期烘焙、不随 update() 变色）。
    // 遮罩/选区/标注均为 paint 期取色，由调用方随后的 QGraphicsScene::update() 统一重绘。
    void refreshPaletteColors() const;

    // 吸附运行时开关：回设单矩形选区与全部 L2 多矩形选区图元的 snapEnabled（无需重建）。
    // 新建图元已在构造期从 CanvasPrefs 继承当前态，本方法只服务切换瞬间已存在的图元。
    void applySnapEnabled(bool on) const;

    // 依 Core 切割线集合，判定选区哪几条边有内部贯穿切割线，下发给选区框绘制并允许直接拖动。
    // rect 为当前选区（原图像素）；切割线即选区边的贯穿延伸，与选区同为一个橙色图元（不双重表示）。
    void updateCutLines(const engine::CutLineSet& lines, const engine::RectRegion& rect) const;
    void clearCutLines() const;

    // 依 Core 诱导网格刷新 L3 网格线（show=false 或非 L3 模式时清空）。
    // 网格线纯显示，与遮罩/选区解耦；单元区域均来自 Core Grid（CONTRIBUTING.md「分层纪律」）。
    // 同时把网格下发给单元点选图元（L3 交互）并按 show 切换其显隐。
    // 另把各单元区域并入内容边界并回写场景矩形：仅补白(Pad)策略下 Core 保留越界单元值（残缺单元伸出图像
    //   边界、虚拟起点可落在负坐标），那截越界需完整计入滚动范围才能被平移进视口；Discard / KeepPartial
    //   的单元恒在图内 → 并集就是原图（见 applySceneExtent）。
    void updateGrid(const engine::Grid& grid, bool show);
    void clearGrid();

    // L3 网格基准手柄（网格线层持有）的屏幕尺寸：随视图缩放换算，使手柄恒约 8 屏幕px。
    void setGridHandleSize(qreal sceneUnits);

    // 视图滚动余量（场景单位，由 CanvasView 按 `视口各边百分比 / 当前缩放` 折算：横向取视口宽、纵向取视口高）：
    // 场景矩形即视图滚动条范围，不给余量则「图大于窗」时平移到图像边缘就卡住（边缘的选区手柄 / 标注 / 补白
    // 区块被视口边贴死）。余量按**视口口径**百分比生效，故**与图幅与缩放档无关**（随窗口大小同比变化，故视图尺寸
    // 变化时需重推，见 CanvasView::resizeEvent）；场景矩形需先包住内容（原图 ∪ 补白单元，见 contentBounds），
    // 余量只是包在内容外面的一层。场景不认识缩放与视口，只接这两个已折算好的数值。
    void setScrollMargins(qreal marginXScene, qreal marginYScene);

    // 内容边界 = 原图 ∪ 全部网格单元区域（无网格时即原图）。补白(Pad) 策略下残缺单元会伸出图像边界，故内容
    // 可比原图大；供 fitToWindow 按**内容**（而非原图）铺满视口，使边界补白块在适应窗口时也完整可见。
    QRectF contentBounds() const;

    // 仅刷新 L2 多矩形的诱导切割线（不动单元点选图元）：以橙色切割线样式画各矩形十字带
    // 并集的诱导线，受 cutLinesVisible_ 门控；不显示 picker（picker 会 grab 鼠标、阻断画布框选追加矩形）。
    void updateMultiRectCutLines(const engine::Grid& grid, bool show);

    // 依 Document 的多矩形列表刷新可交互的橙色选区框（L2 MultiRect）：每个矩形一个
    // SelectionRectItem（可整体移动 / 四角缩放 / 拖边微调），与单矩形选区体验一致。
    // 增量维护（按数量增删末位图元、逐个刷新几何），拖拽中跳过对正在拖图元的回设。
    void updateMultiRects(const std::vector<engine::RectRegion>& rects);
    void clearMultiRects();
    // 多矩形选区框的手柄尺寸（随视图缩放换算，使手柄恒约 8 屏幕px）。
    void setMultiRectHandleSize(qreal sceneUnits);
    // 是否有任一多矩形选区框正被拖拽（供上层在拖拽期间跳过面板回同步）。
    bool isDraggingMultiRect() const;
    // 设置当前高亮（选中）的多矩形下标（-1 = 无）：对应选区框颜色略微加强，其余恢复普通。
    void setActiveMultiRect(int index);

    // 依 Document 的选择集与排序策略刷新单元点选图元的高亮（L3）。
    void updateCellSelection(const std::vector<int>& selected, engine::SortStrategy strategy) const;
    CellPickerItem* cellPickerItem() const { return pickerItem_; }
    // 单元编号角标图层显隐（图层面板「单元编号」开关；角标为单元点选图元的子图层，随其一起显隐）。
    void setCellNumberVisible(bool on) const;

    // L3 框选命中查询：委托单元点选图元返回与场景矩形相交的单元序号（无图元 / 单元选择不可交互时返回空）。
    // 供 CanvasView 橡皮筋框选从图像外起拖（未被 picker grab）时，仍能在 L3 选中单元。
    std::vector<int> cellsIntersecting(const QRectF& sceneRect) const;
    // 是否处于 L3 网格（updateGrid 置位、clearGrid 复位）：单元选择交互通道的存在判据。
    bool gridActive() const { return gridActive_; }
    // L3 单元选择交互是否可用：处于 L3 且「选取边框」图层可见。该图层隐藏时单击点选 / 拖拽框选 / Custom 调序
    // 一起失效（对齐选区框「隐藏图层=不可交互」）——点选图元随之隐藏，本查询让 L3 框选命中查询同样收敛，
    // 避免「点选失效、拖选照旧」两条通道门控分叉。
    bool cellSelectionInteractive() const { return gridActive_ && selectionVisible_; }

    // 选区框：依 Document 的矩形显示；无选区时隐藏。
    void syncSelection(const engine::RectRegion& rect, bool hasRect) const;
    SelectionRectItem* selectionItem() const { return selItem_; }
    // 选区框是否正被用户拖拽（供上层在拖拽期间跳过回设/面板回同步）。
    bool isDraggingSelection() const { return selItem_ && selItem_->isDragging(); }

    // ---- 标注图层 ----
    // 依 AnnotationBridge 增量维护标注矢量图元（每个 Core Annotation 一个 AnnotationItem）+ 拖拽预览图元。
    // 增量维护（按数量增删末位、逐个刷新几何），绝不 clear+重建（否则拖拽中删除正在处理事件的图元→崩溃）。
    void updateAnnotations(const AnnotationBridge& model);
    // 仅刷新拖拽预览图元（绘制手势逐帧调用），不触碰已提交标注图元（避免逐帧重建/克隆）。
    void updatePendingAnnotation(const AnnotationBridge& model);
    // 标注图层显隐（图层面板开关）：只切图元可见性，不删除。
    void setAnnotationsVisible(bool on);
    // 清空全部标注图元（换图 / 维度变化预处理 / 关闭图像时）。
    void clearAnnotations();
    // 标注控制点手柄尺寸（随视图缩放换算，使其屏幕观感恒定）。
    void setAnnotationHandleSize(qreal sceneUnits);
    // 命中场景坐标处的标注显示色（供状态栏取色优先于底图像素）：标注层可见时自顶（下标大＝顶）向下
    // 取首个可见且命中的标注图元，依其渲染规则返回描边/填充/文字色；无命中或标注层隐藏返回 nullopt。
    std::optional<core::Color> annotationColorAt(const QPointF& scenePos) const;
    // 底图图层显隐（图层面板开关）：仅切换 base 图元可见性，不影响导出。
    // 同时留存状态：状态栏取色须据此判定底图是否参与「所见」（底图隐藏时不得再读其像素）。
    void setBaseVisible(bool on);
    bool baseVisible() const { return baseVisible_; }

    // ---- 图片图层像素（多层画布解耦渲染：独立于底图的图层像素图元）----
    // 设置图层像素覆盖图元：preview 为「图层于透明底」的降采样 pixmap，scaleX/Y 为预览→原图放大系数，
    // fullW/H 为原图尺寸（与底图同一坐标系，保证逐像素对齐）。preview 为空则清空覆盖。
    void setImageLayerOverlay(const QPixmap& preview, double scaleX, double scaleY, int fullW, int fullH);
    // 图片图层显隐（图层面板开关）：纯显示切换——切图层像素覆盖图元与各落位框可见性，不重组工作图、不影响导出。
    void setImageLayersVisible(bool on);
    bool imageLayersVisible() const { return imageLayersVisible_; }

    // ---- 图片图层落位框（多层画布，z 位于底图与标注之间）----
    // 依图层栈（zOrder 升序）与 Core layerPlacement 增量维护落位框图元（按 layerId 对齐）；
    // 无可呈现位图的层（layerPlacement 返回 nullopt）跳过。selectedId 高亮当前编辑层。
    // canvasGeom 为画布级有效管线：底图经其几何步（rotate/flip/resize）作用后工作图坐标系与
    //   画布坐标系分叉，故落位框需经 mapRectGeometry 正向映射到工作图坐标系（与逐像素几何同口径，
    //   空管线/仅色彩步 → 恒等直通），使选框随画布几何同步变换、拾取/拖动命中对齐。
    void updateImageLayers(const std::vector<canvas::ImageLayer>& layers, int canvasW, int canvasH,
                           const preprocess::PreprocessPipeline& canvasGeom,
                           std::optional<int> selectedId);
    void clearImageLayers();
    // 仅切换已有落位框的选中高亮框（不重算落位几何）：供选中层变化时廉价刷新。
    void setImageLayerSelection(std::optional<int> selectedId);
    // 图层拾取模式：开→落位框登记为拾取候选（点选由场景级循环选层，非 z 序自动抓取，见 mousePressEvent）；
    //   关→仅示意不可交互（不抢既有交互）。
    void setImagePickMode(bool on);
    bool imagePickMode() const { return imagePickMode_; }

    // 标注选取模式（四态模式机之「标注选取」）：开→已提交标注图元接受左键选中 / 拖拽 / 手柄；
    // 关→标注图元不吃鼠标（Qt::NoButton），使框选 / 拾取手势不被标注抢走（新建中的标注不受影响）。
    void setAnnotationSelectMode(bool on);
    bool annotationSelectMode() const { return annotationSelectMode_; }
    // 标注拖拽（场景驱动）：按下链路选中后，由上层对选中图元调用 beginAnnotationDrag；
    // 此后移动/释放由本场景转发给该图元（被按项与选中项可能不同——重叠时拖动跟随选中项）。
    void beginAnnotationDrag(int index, const QPointF& scenePos);

    // ---- 其余图层显隐（图层面板开关；遵循「隐藏图层=不可交互」）----
    // 网格线 / 切割线（同一开关，按模式各司其职）：
    //   L3 → L1 灰色网格线（重建期门控，切换后由上层 refreshPreview 落地）；
    //   L1/L2 → 选区标记边向全图贯穿的延伸段（绘制 + 可抓）与 L2 多矩形诱导切割线。
    void setGridLinesVisible(bool on);
    bool gridLinesVisible() const { return gridVisible_; }
    // 选取边框图层：影响选区框矩形描边/填充/手柄绘制（隐藏同时禁用其拖拽交互）+ L3 单元选择高亮显隐。
    void setSelectionVisible(bool on);
    bool selectionVisible() const { return selectionVisible_; }

    int imageWidth() const { return imgW_; }
    int imageHeight() const { return imgH_; }

protected:
    // 图层拾取模式的**场景级**命中与拖拽：重叠落位框不靠 z 序取最顶，而是同点**点击**（按下-释放无拖动）
    //   沿叠放序循环切换选中层（去 z 序依赖，仿 Figma；循环态由 pickCycle_ 承担，与标注拾取共用同实现）。
    //   按下＝resolveOnPress（保持已选层、拖动跟随选中项），无拖动释放＝advanceOnClick（沿栈推进一位）。
    //   标注选取态另有一道**手柄优先穿透**（见实现）：选中标注的 OBB 手柄被上层标注盖住时按下仍落到手柄拖拽。
    //   非拾取模式 / 拾取点无候选 → 交基类走既有图元交互。
    void mousePressEvent(QGraphicsSceneMouseEvent* event) override;
    void mouseMoveEvent(QGraphicsSceneMouseEvent* event) override;
    void mouseReleaseEvent(QGraphicsSceneMouseEvent* event) override;

private:
    // L3 基准手柄：惰性创建两个手柄（网格线层持有）并接上其拖拽信号。
    void ensureGridHandles();
    // 拖拽中逐帧：按草稿参数交 Core Grid::build 重铺网格线（不写 Document，故无预览全量重算）。
    void onGridHandlePreviewed(const engine::GridParams& params);
    // 释放：恢复单元点选图元，并把最终参数一次性上交（gridParamsEdited）。
    void onGridHandleCommitted(const engine::GridParams& params);
    // 手柄可交互口径：L3 + 网格线图层可见 + 不在图片拾取/标注选取态（后者让位给它们的图元）。
    // 网格线图层隐藏 → 手柄不可拖（隐藏图层=不可交互）。
    void refreshGridHandleInteraction() const;

    // 场景驱动拖拽的释放收尾：清除按下时建立的鼠标抓取。本场景的释放分支自行消费事件、不交基类，
    // 而隐式抓取只在基类释放处理里随「最后按键抬起」解除——不清理则被按图元残留抓取，后续按下全被
    // 其吞掉（桌面靠悬停 move 触发 Qt 的自清理特例勉强恢复；触控无悬停 move，穿透选中即由此失效）。
    void releasePressGrab() const;

    // 收集 scenePos 处命中的图片图层 id（按 zValue 降序、顶在前；仅拾取模式下的可见图元）。
    std::vector<int> layerStackAt(const QPointF& scenePos) const;
    // 按 id 取回对应的落位框图元（无则 nullptr）。
    ImageLayerItem* imageLayerItemById(int id) const;

    // 重算场景矩形 = 内容边界 + 外圈余量（余量为视图注入的视口口径值，横/纵各自一个，故左右与上下可不等量）。
    // 内容边界本身可以不对称（补白只在某一侧伸出），场景矩形随之不对称——这是正确行为：整幅场景小于视口时 Qt
    // 按场景矩形居中，居中的是「内容」，而补白块本就是真实内容，本就该一起框进来（若按四边等量外扩反而会把
    // 空白一侧一并扩掉）。无越界时（非 L3 / 非补白）内容边界就是原图 → 各模式、各余量策略完全同口径。
    // setBaseImage / updateGrid / clearGrid / setScrollMargins 共用。
    void applySceneExtent();

signals:
    // 选区框被用户移动/缩放/拖边（转发自 SelectionRectItem::rectChanged），参数为原图像素坐标矩形。
    // 拖动选区边＝移动对应切割线（二者同一图元），故只需这一个稳定信号。
    void selectionEdited(const QRectF& sceneRect);
    // 用户开始拖动任一选区框（单选区或多矩形，转发自 SelectionRectItem::dragStarted）：
    // 供上层在拖动选区边框时清除标注选中态（与空白框选 marqueeStarted 同义）。
    void selectionInteractionStarted();
    // L3 单元点选（转发自 CellPickerItem）：单击切换 / 拖拽框选，交 CanvasCoordinator 写回 Document。
    void cellToggled(int index);
    void cellsMarqueeSelected(const std::vector<int>& indices);
    // L3 Custom 拖拽调序（转发自 CellPickerItem）：已选目标与 fromIndex 互换位次、未选目标原位替换。
    void cellReordered(int fromIndex, int toIndex);
    // L2 多矩形：某个选区框被拖动/缩放（转发自对应 SelectionRectItem），index 为其在列表中的下标。
    void multiRectEdited(int index, const QRectF& sceneRect);
    // L3 网格基准手柄释放：拖拽中只本地预览（不写 Document），释放时把最终参数一次性上交，
    // 由 CanvasCoordinator 走 Document::setGridParams（沿用「改网格几何→清空单元选择集」既有行为）。
    void gridParamsEdited(int originX, int originY, int cellWidth, int cellHeight);
    // 标注：某标注图元被按下（转发自 AnnotationItem），交上层用 Core hitTest 选中（场景坐标）。
    void annotationSelectRequested(const QPointF& scenePos);
    // 标注：某图元被原地点击（按下-释放无移动，转发自 AnnotationItem），交上层推进同点连击循环。
    void annotationClicked(const QPointF& scenePos);
    // 标注：某选中图元被拖动平移（转发自 AnnotationItem），index 为其在标注列表中的下标。
    void annotationMoved(int index, double dx, double dy);
    // 标注：某选中图元的定向包围盒手柄被拖拽（转发自 AnnotationItem），请求缩放(sx,sy)+旋转(deg)。
    void annotationTransformed(int index, double sx, double sy, double rotateDeg);
    // 标注：手柄拖拽**进行中**逐帧预览值（转发自 AnnotationItem），仅供属性面板数值实时回显，不写模型。
    void annotationTransformPreview(int index, double sx, double sy, double rotateDeg);
    // 图片图层：某落位框在拾取模式下被按下（转发自 ImageLayerItem），交上层选中该图层。
    void imageLayerPressed(int id);
    // 图片图层：某落位框被拖拽平移（转发自 ImageLayerItem），请求把该图层落位偏移 (dxPx,dyPx)。
    void imageLayerMoved(int id, int dxPx, int dyPx);
    // 图片拾取态下点击「画布内非图层处」（底图铺满画布）：请求选中底图（预处理目标切 Base）。
    void basePickRequested();
    // 图片拾取态下点击「画布外」：请求取消全部选中（图层/底图/标注），预处理目标回到画布。
    void pickDeselectRequested();

private:
    QGraphicsPixmapItem* baseItem_{nullptr};
    QGraphicsPixmapItem* imageLayerOverlayItem_{nullptr};   // 图片图层像素覆盖（解耦渲染：独立于底图，图层显隐只切此图元）
    MaskLayer maskLayer_;
    GridLayer gridLayer_;
    SelectionRectItem* selItem_{nullptr};
    CellPickerItem* pickerItem_{nullptr};  // L3 单元点选图元（与选区框互斥显隐）
    QVector<SelectionRectItem*> multiRectItems_;  // L2 多矩形可拖拽选区框（下标与 Document::rects() 一致）
    QVector<AnnotationItem*> annoItems_;   // 标注矢量图元（下标与 AnnotationBridge::annotations() 一致）
    AnnotationItem* pendingAnnoItem_{nullptr};  // 绘制拖拽预览图元（橡皮筋矢量，不进 Core 历史）
    bool annotationsVisible_{true};        // 标注图层可见性（图层面板开关）
    qreal annoHandleSize_{nums::canvas::kFallbackHandleScenePx};  // 标注控制点手柄尺寸（场景单位，随缩放换算；视图下发前的兜底）
    qreal multiHandleSize_{nums::canvas::kFallbackHandleScenePx}; // 多矩形选区框手柄尺寸（场景单位，随缩放换算；同上）
    int multiBoundsW_{-1};                 // 上次下发给多矩形图元的图像宽（变化时才重推边界）
    int multiBoundsH_{-1};                 // 上次下发给多矩形图元的图像高
    int activeMultiRect_{-1};              // 当前高亮（选中）的多矩形下标（-1 = 无）

    int imgW_{0};
    int imgH_{0};
    qreal viewMarginXScene_ = 0.0;   // 视图注入的左右滚动余量（场景单位，视口宽口径百分比折算而来）
    qreal viewMarginYScene_ = 0.0;   // 视图注入的上下滚动余量（场景单位，视口高口径百分比折算而来）
    QRectF gridBounds_;              // 全部网格单元区域的并集（场景坐标；无网格 / 非 L3 时为空矩形）
    bool gridActive_{false};         // 是否处于 L3 网格（单元选择交互的 L3 判据；不随网格显隐开关变化）
    qreal gridHandleScene_{nums::canvas::kFallbackHandleScenePx};  // 网格基准手柄边长（场景单位；视图下发前兜底）
    QVector<ImageLayerItem*> imageLayerItems_;   // 图片图层落位框（按 layerId 对齐，增量维护）
    bool imagePickMode_{false};                  // 图层拾取模式（默认关）
    bool imageLayersVisible_{true};              // 图片图层像素/落位框显隐（图层面板开关；纯显示，不驱动重组）
    bool baseVisible_{true};                     // 底图显隐（图层面板开关；纯显示，状态栏取色据此判定）
    DraggableCanvasItem* pickDragItem_{nullptr}; // 拾取模式下正被场景驱动的拖拽图元（图层落位框）
    PickCycle<int> pickCycle_;                   // 同点连击循环选层状态（见 canvas/pick_cycle.h；与标注拾取共用同实现）
    std::optional<int> pickedLayerId_;           // 当前选中的图片图层 id（按下保持选中的判定基准；由选中回灌）
    bool annotationSelectMode_{false};           // 标注选取模式（默认关＝框选态，标注不吃鼠标）
    DraggableCanvasItem* annoDragItem_{nullptr}; // 标注选取模式下正被场景驱动的平移拖拽图元（选中项，非被按项）
    bool masksVisible_{true};
    bool gridVisible_{true};       // 网格线/切割线开关：L3 网格线重建门控（L1/L2 用 cutLinesVisible_，二者同置）
    bool cutLinesVisible_{true};   // 切割线门控：选区延伸段绘制/可抓 + L2 多矩形诱导线（随同一开关同置）
    bool selectionVisible_{true};  // 选取边框图层可见性（图层面板开关）
};

} // namespace idc::gui
