// ============================================================================
// 文件：canvas/canvas_scene_layers.cpp
// 作用：实现 CanvasScene 的图层显隐与命中簇——底图/图片图层/网格线/选区图元显隐开关
//       （setBaseVisible/setImageLayersVisible/setGridLinesVisible/setSelectionVisible，选区显示含 OBB）、
//       图片图层落位框生命周期（clearImageLayers/setImageLayerSelection/setImagePickMode）
//       与拾取命中（layerStackAt/imageLayerItemById）。
// 分块依据：见 canvas_scene.cpp 头注（按图元族五 TU 拆分）；本 TU 只管理显隐开关与命中查询，
//       不参与鼠标事件分派（留在主件）。
// ============================================================================
#include "canvas/canvas_scene.h"

#include <optional>
#include <vector>

#include <QGraphicsSceneMouseEvent>
#include <QPixmap>
#include <QRectF>

#include "canvas/items/image_layer_item.h"
#include "canvas/items/z_order.h"

namespace idc::gui {

// ===========================================================================
// 标注图层
// ===========================================================================
// 底图显隐（图层面板开关）：只切 base 图元可见性；状态留存供状态栏取色判定「所见」是否含底图。
void CanvasScene::setBaseVisible(const bool on) {
    baseVisible_ = on;
    if (baseItem_) baseItem_->setVisible(on);
}

// 设置图片图层像素覆盖图元（解耦渲染）：惰性创建、与底图同坐标系（同 fullW/H、同放大变换）。
// preview 为空则清空覆盖（无叠加图层 / 非图层画布）；可见性按当前 imageLayersVisible_ 落地。
void CanvasScene::setImageLayerOverlay(const QPixmap& preview, const double scaleX, const double scaleY,
                                       const int /*fullW*/, const int /*fullH*/) {
    if (!imageLayerOverlayItem_) {
        imageLayerOverlayItem_ = addPixmap(QPixmap());
        imageLayerOverlayItem_->setZValue(zorder::kImageLayerPixels);
        imageLayerOverlayItem_->setAcceptedMouseButtons(Qt::NoButton);
        imageLayerOverlayItem_->setTransformationMode(Qt::FastTransformation);
    }
    imageLayerOverlayItem_->setTransform(QTransform::fromScale(scaleX, scaleY));
    imageLayerOverlayItem_->setPixmap(preview);
    imageLayerOverlayItem_->setVisible(imageLayersVisible_ && !preview.isNull());
}

// 图片图层显隐（图层面板开关）：纯显示切换——同步图层像素覆盖图元与各落位框可见性；不触发重组、不影响导出。
void CanvasScene::setImageLayersVisible(const bool on) {
    imageLayersVisible_ = on;
    if (imageLayerOverlayItem_) imageLayerOverlayItem_->setVisible(on && !imageLayerOverlayItem_->pixmap().isNull());
    for (ImageLayerItem* it : imageLayerItems_) it->setVisible(on);
}

// 网格线 / 切割线图层显隐（图层面板单一开关）：同一开关按模式各司其职——
//   L3 → 灰色网格线（不直接切 gridLayer_ 可见性，只置标志，由 refreshPreview 依 gridVisible_ 重建落地）；
//   L1/L2 → 选区标记边向全图贯穿的延伸段（绘制 + 可抓）与 L2 多矩形诱导切割线。
// 两个内部标志同置：GridLayer 被 L3 灰色网格与 L2 橙色诱导线共用，直接 setVisible 会相互覆盖；
// 非 L3 时 gridLayer_ 只承载 L2 诱导线，故一并直接切其可见性使开关不依赖重建即时生效。
void CanvasScene::setGridLinesVisible(const bool on) {
    gridVisible_ = on;
    cutLinesVisible_ = on;
    if (selItem_) selItem_->setCutLinesVisible(on);   // 仅影响贯穿延伸段绘制与可抓性，选区边自身仍可拖。
    for (SelectionRectItem* it : multiRectItems_) it->setCutLinesVisible(on);
    if (!gridActive_) gridLayer_.setVisible(on);
    refreshGridHandleInteraction();   // L3 基准手柄同属该图层：隐藏即不可拖。
}

// 选取边框图层显隐：下发到单选区框与全部多矩形框（隐藏边框同时禁用其拖拽交互，遵循「隐藏图层=不可交互」）。
// L3 单元选择高亮（CellPickerItem）由上层 refreshPreview 依 selectionVisible_ 重建显隐。
void CanvasScene::setSelectionVisible(const bool on) {
    selectionVisible_ = on;
    if (selItem_) selItem_->setBorderVisible(on);
    for (SelectionRectItem* it : multiRectItems_) it->setBorderVisible(on);
}

// ===========================================================================
// 图片图层落位框（多层画布：底图之上、标注之下的可拾取框）
// ===========================================================================

// 依 Document 图层栈增量维护每个图片图层的落位框图元。
// 【拖拽安全】仿 updateMultiRects：按 layerId 对齐增删，绝不 clear+重建（否则会在拖拽中删除
// 正在处理鼠标事件的图元→崩溃）；且跳过对正在拖拽者的几何回设（其位置由手势维护，释放才回灌）。
// 落位矩形来自 Core canvas::layerPlacement（GUI 不自算锚点/枢轴/缩放，CONTRIBUTING.md「分层纪律」）。
// 底图是经 canvasGeom（画布级有效管线）作用后的工作图，场景坐标即工作图坐标系；layerPlacement 产出的
//   是画布坐标系矩形，需经 mapRectGeometry 正向映射后再落位（与逐像素几何同口径；空管线/仅色彩步→恒等），
//   否则画布级 rotate/flip/resize 后选框大小方向与原样不跟随、拖动命中错位。
// 无可呈现位图（layerPlacement 返回 nullopt）的层：已有图元仅隐藏（不删，规避拖拽期删除风险），否则跳过。
// 【层序与 Qt z 无关】图层叠放序是两个正交维度的模型事实：ImageLayer::zOrder（越大越靠顶）与
//   该层在 layers_ 数组中的位序（同 zOrder 时靠后者在上），与 Core composeCanvas 的
//   「按 zOrder 稳定排序、同值才维持数组先后」逐字同构。二者作为堆叠键随图元留存
//   （setStackKeys），**只在 layerStackAt 的排序里消费**，不折算进 Qt z：落位框 z 恒取带基
//   kImageLayer（它的职责只是把整族图元放在底图之上、标注之下）。
//   因此同 z 下落位框之间的绘制序 = 场景插入序（＝图元创建序），仅影响青色虚线框互相压盖的观感，
//   **不表达叠放序**；需要叠放序的地方（同点连击的下探顺序）一律走命中栈。按下归属也由场景
//   路由（layerStackAt + resolveOnPress），不靠 z 抢最顶。
void CanvasScene::updateImageLayers(const std::vector<canvas::ImageLayer>& layers,
                                    const int canvasW, const int canvasH,
                                    const preprocess::PreprocessPipeline& canvasGeom,
                                    const std::optional<int> selectedId) {
    pickedLayerId_ = selectedId;   // 回灌当前选中层：作为拾取按下时「保持选中」的判定基准。
    // 收缩：删除已不在 layers 中的图元（按 id 匹配；防御：绝不删除正在拖拽者）。
    for (int i = imageLayerItems_.size() - 1; i >= 0; --i) {
        ImageLayerItem* it = imageLayerItems_[i];
        const bool stillPresent = std::ranges::any_of(layers,
                                                      [it](const canvas::ImageLayer& l) { return l.id == it->layerId(); });
        if (stillPresent) continue;
        if (it->isDragging()) continue;
        imageLayerItems_.remove(i);
        removeItem(it);
        delete it;
    }
    // 扩张 + 刷新：逐层确保有对应图元，再按 Core 落位刷新几何、层序键与选中态。
    for (std::size_t i = 0; i < layers.size(); ++i) {
        const canvas::ImageLayer& l = layers[i];
        ImageLayerItem* it = nullptr;
        for (ImageLayerItem* cand : imageLayerItems_) {
            if (cand->layerId() == l.id) { it = cand; break; }
        }
        const auto p = canvas::layerPlacement(l, canvasW, canvasH);
        if (!p) {
            if (it) it->setVisible(false);   // 空位图：隐藏既有图元，不删除。
            continue;
        }
        if (!it) {
            it = new ImageLayerItem();
            it->setLayerId(l.id);
            // 按下选中与拖拽平移由场景按下链路驱动（imageLayerPressed / 手势基类），
            // 本图元只上送拖拽提交 moved，交 CanvasCoordinator 写回 Document（场景不认识 Document）。
            connect(it, &ImageLayerItem::moved, this, &CanvasScene::imageLayerMoved);
            // 新图元交互态：拾取模式开即为循环选层候选；z 恒取落位带基（层序不落进 z，见函数头注）。
            it->setInteractive(imagePickMode_);
            it->setZValue(zorder::kImageLayer);
            addItem(it);
            imageLayerItems_.append(it);
        }
        // 层序堆叠键逐帧刷新（zOrder 属性 + 数组位序，两个维度都不进 Qt z）。
        it->setStackKeys(l.zOrder, static_cast<int>(i));
        it->setVisible(imageLayersVisible_);
        // 画布坐标 → 工作图坐标：随画布级几何步同步变换落位框（坐标空间与底图对齐）。
        const auto [x, y, width, height] = canvasGeom.mapRectGeometry(p->x, p->y, p->width, p->height, canvasW, canvasH);
        if (!it->isDragging()) it->setPlacement(QRectF(x, y, width, height));
        it->setSelectedFrame(selectedId.has_value() && selectedId == l.id);
    }
}

// 清空全部图片图层落位框图元。QGraphicsItem 析构会自动从场景移除。
void CanvasScene::clearImageLayers() {
    for (ImageLayerItem* it : imageLayerItems_) { removeItem(it); delete it; }
    imageLayerItems_.clear();
}

// 仅切换已有落位框的选中高亮（不重算 layerPlacement）：选中层变化时廉价刷新。
// 拖拽中的图元也须同步高亮：按下即选中，若跳过被拖者，点中时刻的描边切换会被吞掉、直到释放
// 才由 moved 补上；选中描边与位置无关，拖拽护栏只挡几何回设（见 updateImageLayers）。
void CanvasScene::setImageLayerSelection(const std::optional<int> selectedId) {
    pickedLayerId_ = selectedId;
    for (ImageLayerItem* it : imageLayerItems_)
        it->setSelectedFrame(selectedId.has_value() && selectedId == it->layerId());
}

// 图层拾取模式：切换全部落位框的可拾取标志（不动 z 序——拾取纯由四态模式 + 场景级循环选层，与叠放先后无关）。
// 开→落位框登记为拾取候选（点选由本场景 mousePressEvent 循环路由，非 z 序自动抓取）；
// 关→全部退出候选（仅示意），并放弃进行中的场景级拖拽、复位循环态（重入从最顶层重新开始）。
void CanvasScene::setImagePickMode(const bool on) {
    imagePickMode_ = on;
    if (!on) { pickDragItem_ = nullptr; pickCycle_.reset(); }
    for (ImageLayerItem* it : imageLayerItems_) it->setInteractive(on);
    refreshGridHandleInteraction();   // 拾取态下网格基准手柄让位（只降可交互，不动显隐）。
}

// 收集 scenePos 处命中的图片图层落位框 id，按叠放序「顶在前」返回。去 z 序自动抓取依赖：本函数
//   自行遍历候选图元做几何命中，不依赖 Qt 把按下事件递给最顶图元；仅拾取模式下可见且登记的候选入栈。
//   排序不取 Qt z（落位框同 z），而是直接用图元上随帧刷新的层序堆叠键，与 Core composeCanvas 同构：
//   ① zOrder 降序（越大越靠顶、越先命中）；② 同 zOrder 时数组位序降序（靠后者在上，SPEC §7）。
//   本 TU 的遍历序（imageLayerItems_ 位序＝图元创建序）不参与排序：它与上述两个维度都无关
//   （图元按 id 复用，不随重排变动）。
std::vector<int> CanvasScene::layerStackAt(const QPointF& scenePos) const {
    struct Hit { int id; int zOrder; int arrayIndex; };
    std::vector<Hit> hits;
    for (const ImageLayerItem* it : imageLayerItems_) {
        if (!it->isVisible() || !it->interactive()) continue;
        if (!it->placement().contains(scenePos)) continue;
        hits.push_back({it->layerId(), it->stackZOrder(), it->stackArrayIndex()});
    }
    // 顶在前：先按 zOrder 降序，同 zOrder 再按数组位序降序（靠后者在上）。
    std::ranges::sort(hits, [](const Hit& a, const Hit& b) {
        if (a.zOrder != b.zOrder) return a.zOrder > b.zOrder;
        return a.arrayIndex > b.arrayIndex;
    });
    std::vector<int> stack;
    stack.reserve(hits.size());
    for (const Hit& h : hits) stack.push_back(h.id);
    return stack;
}

// 按 id 取回对应的落位框图元（无则 nullptr）。
ImageLayerItem* CanvasScene::imageLayerItemById(const int id) const {
    for (ImageLayerItem* it : imageLayerItems_) if (it->layerId() == id) return it;
    return nullptr;
}

} // namespace idc::gui