// ============================================================================
// 文件：canvas/canvas_scene.cpp
// 作用：实现 CanvasScene 的装配与交互分派（见同名头文件说明）。
// 分块依据：按图元族拆五个 TU——本文件（底图/遮罩/切割线装配 + 鼠标事件分派中心）、
//           _annotations（标注图元族）、_rects（多矩形图元族）、_grid（网格线/手柄/单元点选/选区框）、
//           _layers（图层显隐与拾取命中）；场景不含切割或几何逻辑，仅按 Core 结果与 Document
//           状态渲染（CONTRIBUTING.md「分层纪律」）。共享几何助手（nearest/cellSceneRect）
//           见 canvas_scene_internal.h。
// ============================================================================
#include "canvas/canvas_scene.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <vector>

#include <QGraphicsSceneMouseEvent>
#include <QTransform>

#include "canvas/items/grid_handle_item.h"
#include "canvas/items/z_order.h"
#include "model/annotation_bridge.h"
#include "geometry/shapes.h"
#include "util/gui_nums.h"

#include "canvas_scene_internal.h" // nearest（切割线关联最近选区边）

namespace idc::gui {

// 构造：空场景。底图与选区框在 setBaseImage 时惰性创建。
// 【越界行为】不做越界裁剪：图层落位框与标注矢量均可绘制到画布（sceneRect）之外，视口内自由可见
//   （Qt6 已移除 QGraphicsView::setItemClippingEnabled，且产品决定不保留“两者都裁”）。故本构造无需额外开关。
CanvasScene::CanvasScene(QObject* parent) : QGraphicsScene(parent) {}

// 设置底图并同步场景矩形与选区边界。
void CanvasScene::setBaseImage(const QPixmap& preview, const double scaleX, const double scaleY,
                               const int fullW, const int fullH) {
    imgW_ = fullW;
    imgH_ = fullH;
    applySceneExtent();   // 余量与内容边界沿用当前值（网格内容由随后的 updateGrid/clearGrid 回灌）。

    // 底图：预览 pixmap 经放大变换铺满原图尺寸（快速变换，预览足够）。
    if (!baseItem_) {
        baseItem_ = addPixmap(QPixmap());
        baseItem_->setZValue(zorder::kBase);
        baseItem_->setAcceptedMouseButtons(Qt::NoButton);
    }
    baseItem_->setTransformationMode(Qt::FastTransformation);
    baseItem_->setTransform(QTransform::fromScale(scaleX, scaleY));
    baseItem_->setPixmap(preview);

    // 选区框：惰性创建，设定图像边界（供钳制与吸附）。
    if (!selItem_) {
        selItem_ = new SelectionRectItem();
        addItem(selItem_);
        selItem_->setZValue(zorder::kSelection);
        selItem_->setVisible(false);
        // 选区框的编辑事件转发为场景信号，供 CanvasCoordinator 写回 Document（视图/场景不认识 Document）。
        connect(selItem_, &SelectionRectItem::rectChanged, this, &CanvasScene::selectionEdited);
        // 拖动选区边框开始：转发为场景交互信号，供上层清除标注选中（标注 OBB 手柄随选区拖动消失）。
        connect(selItem_, &SelectionRectItem::dragStarted, this, &CanvasScene::selectionInteractionStarted);
        // 同步图层面板的网格线/切割线与选取边框显隐开关（新建图元需继承当前标志）。
        selItem_->setCutLinesVisible(cutLinesVisible_);
        selItem_->setBorderVisible(selectionVisible_);
    }
    selItem_->setImageBounds(fullW, fullH);

    // 单元点选图元：惰性创建（L3 交互）；仅 L3 显示（updateGrid 时切换）。
    // 其点选/框选事件同样转发为场景信号，供 CanvasCoordinator 写回 Document。
    if (!pickerItem_) {
        pickerItem_ = new CellPickerItem();
        addItem(pickerItem_);
        pickerItem_->setVisible(false);
        connect(pickerItem_, &CellPickerItem::cellToggled, this, &CanvasScene::cellToggled);
        connect(pickerItem_, &CellPickerItem::cellsMarqueeSelected,
                this, &CanvasScene::cellsMarqueeSelected);
        connect(pickerItem_, &CellPickerItem::cellReordered, this, &CanvasScene::cellReordered);
    }
}

// 清空画布全部内容。
void CanvasScene::clearAll() {
    maskLayer_.clear();
    gridLayer_.clear();
    clearCutLines();
    clearMultiRects();
    clearAnnotations();
    clearImageLayers();
    if (baseItem_) { removeItem(baseItem_); delete baseItem_; baseItem_ = nullptr; }
    if (imageLayerOverlayItem_) { removeItem(imageLayerOverlayItem_); delete imageLayerOverlayItem_; imageLayerOverlayItem_ = nullptr; }
    if (selItem_) { removeItem(selItem_); delete selItem_; selItem_ = nullptr; }
    if (pickerItem_) { removeItem(pickerItem_); delete pickerItem_; pickerItem_ = nullptr; }
    imgW_ = 0;
    imgH_ = 0;
}

// 刷新保留/删除遮罩（委托 MaskLayer）。
void CanvasScene::updateMasks(const engine::EngineResult& result) {
    maskLayer_.rebuild(*this, result, imgW_, imgH_, masksVisible_);
}

// 遮罩显隐切换。
void CanvasScene::setMasksVisible(const bool visible) {
    masksVisible_ = visible;
    maskLayer_.setVisible(visible);
}

// 依 Core 切割线集合，判定选区四条边各自是否有「内部贯穿切割线」，把结果下发给选区框：
// 选区框据此把这些边延伸绘制为橙色贯穿线，并允许直接抓取拖动（拖动该边＝移动切割线）。
// 单矩形/十字(Cross)四边皆有；横带(Hline)仅上/下；竖带(Vline)仅左/右；落在图像边界(0/W/H)
// 的线不算「内部切割线」（此时选区边即图像边，内部无切割可言），对应边不延伸。
void CanvasScene::updateCutLines(const engine::CutLineSet& lines, const engine::RectRegion& rect) const {
    if (!selItem_ || imgW_ <= 0 || imgH_ <= 0) { clearCutLines(); return; }

    // 每条边取 Core 线集中最接近该边者，落在图像内部才视为有效贯穿切割线。
    const int lx = nearest(lines.xs, rect.left);
    const int rx = nearest(lines.xs, rect.right);
    const int ty = nearest(lines.ys, rect.top);
    const int by = nearest(lines.ys, rect.bottom);
    const bool left   = lx > 0 && lx < imgW_;
    const bool right  = rx > 0 && rx < imgW_;
    const bool top    = ty > 0 && ty < imgH_;
    const bool bottom = by > 0 && by < imgH_;
    selItem_->setCutEdges(left, right, top, bottom);
}

// 清空切割线（换图/关闭图像/无选区时）：取消选区框的边延伸标记。
void CanvasScene::clearCutLines() const {
    if (selItem_) selItem_->setCutEdges(false, false, false, false);
}

// 内容边界 = 原图 ∪ 全部网格单元区域（无网格时 gridBounds_ 为空矩形，united 直接返回原图）。
QRectF CanvasScene::contentBounds() const {
    return QRectF(0.0, 0.0, imgW_, imgH_).united(gridBounds_);
}

// 场景矩形 = 内容边界 + 外圈余量（余量由视图按「视口各边百分比 / 当前缩放」折算为场景单位注入，视口口径、与图幅
//   无关，见 CanvasView::updateScrollMargins）：横向按视口宽、纵向按视口高，故左右与上下可不等量。内容边界由
//   updateGrid 按 Core 单元并入：补白(Pad) 的残缺单元伸出原图时必须**完整**进滚动范围（夹住它 → 放大后可滚进来的
//   部分越来越小、边界块永远看不全），余量再包在这层内容外面。
// 无图（尺寸 0）时不改场景矩形：空矩形会让 QGraphicsScene 退回 itemsBoundingRect 自动口径。
void CanvasScene::applySceneExtent() {
    if (imgW_ <= 0 || imgH_ <= 0) return;
    setSceneRect(contentBounds().adjusted(-viewMarginXScene_, -viewMarginYScene_,
                                         viewMarginXScene_, viewMarginYScene_));
}

// 视图滚动余量（场景单位，由 CanvasView 按「视口各边百分比 / 当前缩放」折算）：任何缩放档位下都能把内容往外推
// 同一视口比例幅度（横＝视口宽×百分比、纵＝视口高×百分比）。本方法是场景矩形唯一的「外部余量」入口，与
// updateGrid 记下的内容边界互不干涉。
void CanvasScene::setScrollMargins(const qreal marginXScene, const qreal marginYScene) {
    const qreal mx = std::max(0.0, marginXScene);
    const qreal my = std::max(0.0, marginYScene);
    if (qFuzzyCompare(mx, viewMarginXScene_) && qFuzzyCompare(my, viewMarginYScene_)) return;
    viewMarginXScene_ = mx;
    viewMarginYScene_ = my;
    applySceneExtent();
}

// 场景级按下：拾取模式下重叠落位框靠循环选层（layerStackAt），而非 z 序自动抓取。
//   命中候选 → PickCycle::resolveOnPress 定本次目标层（当前选中仍在栈内则保持、否则取最顶），
//   起拖拽手势并发 imageLayerPressed（→ 上层 selectLayer 回灌 pickedLayerId_ 与选中高亮）；
//   循环推进不在此发生（纯查询），延到确认无拖动的释放（见 mouseReleaseEvent 的 advanceOnClick）。
//   未命中任何层 → 按落点路由：画布内（底图铺满）发 basePickRequested 选中底图、画布外发 pickDeselectRequested 取消全部；
//   图片拾取态恒 accept、不落基类（杜绝残余框选/默认抓取，四态互斥）；非拾取模式 / 非左键 → 交基类。
void CanvasScene::mousePressEvent(QGraphicsSceneMouseEvent* event) {
    if (imagePickMode_ && event->button() == Qt::LeftButton) {
        if (const std::vector<int> stack = layerStackAt(event->scenePos()); !stack.empty()) {
            // 按下保持当前选中（若仍在命中栈内 → 拖动跟随选中项），否则取栈顶；纯查询不推进循环。
            const std::optional<int> target = PickCycle<int>::resolveOnPress(stack, pickedLayerId_);
            if (ImageLayerItem* it = imageLayerItemById(*target)) {
                pickDragItem_ = it;
                it->beginTranslationDrag(event->scenePos());
                emit imageLayerPressed(*target);   // 按下即选中：交上层 selectLayer → 回灌 pickedLayerId_
                event->accept();
                return;
            }
        }
        // 未命中可拖图层：清循环态，再按落点分派底图选中 / 取消全部选中（坐实四态互斥）。
        pickCycle_.reset();
        if (const QPointF p = event->scenePos(); p.x() >= 0 && p.y() >= 0 && p.x() < imgW_ && p.y() < imgH_)
            emit basePickRequested();
        else
            emit pickDeselectRequested();
        event->accept();
        return;
    }
    // 标注选取态按下：选中标注的 OBB 手柄优先穿透命中——上层标注盖住手柄点位时，按下仍应落到
    // 选中标注的手柄拖拽（否则选中框形同虚设）；未命中（无选中/不在手柄半径内）交基类走正常分发。
    if (annotationSelectMode_ && event->button() == Qt::LeftButton) {
        for (AnnotationItem* it : annoItems_) {
            if (it->isSelectedState() && it->tryHandlePress(event->scenePos())) {
                event->accept();
                return;
            }
        }
    }
    // L2 多矩形重叠路由：各选区框同 z，交基类自动抓取时永远是最后追加者吃到按下，被压住的旧矩形
    //   在画布上再也拖不到。改为场景按命中栈路由：resolveOnPress——当前选中（面板列表行回灌的
    //   activeMultiRect_）仍在命中栈内则保持它（拖动跟随选中项，列表选中后可直接拖），否则取栈顶。
    //   手势在被路由的图元上显式起（beginPressAt）并由其 grabMouse 接管后续 move/release：事件只交付
    //   一个图元，而「被按到的顶图元」与「应被拖动的图元」可以不同。未命中任何框 → 落基类（不抢抓取，
    //   视图据此起橡皮筋框选追加新矩形）。
    if (!multiRectItems_.isEmpty() && event->button() == Qt::LeftButton) {
        const std::vector<int> stack = multiRectStackAt(event->scenePos());
        const std::optional<int> current =
            activeMultiRect_ >= 0 ? std::optional(activeMultiRect_) : std::nullopt;
        if (const std::optional<int> target = PickCycle<int>::resolveOnPress(stack, current);
            target && *target < multiRectItems_.size()) {
            // beginPressAt 内部的 dragStarted 只经 selectionInteractionStarted 清标注选中（只重建标注图元、
            // 不动多矩形图元），故本行之后 it 仍有效；随后才抢抓取与 emit（回灌链若重建图元也不影响已抢好的抓取）。
            if (SelectionRectItem* it = multiRectItems_[*target]; it->beginPressAt(event->scenePos())) {
                multiRectPressIndex_ = *target;
                it->grabMouse();   // 基类把后续移动/释放照常送达本图元（它未必是光标下的顶图元）
                // 按下即选中：不握 it 指针再往下走（回灌链有可能重建图元），故 emit 放末句。
                emit multiRectSelectionRequested(*target);
                event->accept();
                return;
            }
        }
    }
    QGraphicsScene::mousePressEvent(event);
}

// 场景级移动：驱动当前被拖图元（dragMoveTo 仅 setPos 视觉位移；点击 vs 拖拽的判定内聚在图元基类
//   手势——以「本手势是否收到过非零位移」的显式标志判定，与设备像素阈值、视图缩放均无关）。
void CanvasScene::mouseMoveEvent(QGraphicsSceneMouseEvent* event) {
    if (DraggableCanvasItem* d = pickDragItem_ ? pickDragItem_ : annoDragItem_) {
        d->dragMoveTo(event->scenePos());
        event->accept();
        return;
    }
    QGraphicsScene::mouseMoveEvent(event);
}

// 场景级释放：结束当前手势（endTranslationDrag 先复位视觉位移，再交图元送达钩子一次性上送
//   moved / clicked / moveRequested，并返回本次是否为原地点击）；清空拖拽句柄与按下残留的抓取
//   （隐式抓取只在基类释放处理里解除，本分支自行消费事件后必须补清，见 releasePressGrab）。
//   拾取原地点击 → PickCycle::advanceOnClick 沿候选栈推进一位（首次定点击复位到栈顶＝当前），选中变化才
//     emit imageLayerPressed（交 CanvasCoordinator selectLayer → setImageLayerSelection 回同步高亮）；
//   拾取拖拽 → 清循环态。否则交基类。
//   多矩形同构：原地点击（图元自报手势未位移）→ 沿栈下探一位并 emit multiRectSelectionRequested；
//     拖拽 → 清循环态（其后同点单击从无历史重新起算）。用 advanceFromTarget 而非 advanceOnClick：
//     按下阶段已把目标定好（可能是从列表选中的下层框），游标未指向它时本次只定位不推进，
//     否则单击会把刚选中的下层框弹回栈顶（等于又选到别的框）。结算（吸附/量化/写回模型）仍由图元自己的
//     释放处理完成，故先取点击判据再交基类送达，emit 排在基类之后（不与 rectChanged 的写回链重入）。
//     基类送达后必须补 releasePressGrab()：本次抓取是 grabMouse() 的**显式**抓取，而基类释放只自动解除
//     隐式抓取（显式抓取恒保留至 ungrabMouse），不归还则该图元永久持抓取——后续 grabMouse 命中「已是
//     抓取者」直接早退，移动/释放全送到旧图元，新目标拖不动且卡在拖拽态（与标注穿透路径同一教训）。
void CanvasScene::mouseReleaseEvent(QGraphicsSceneMouseEvent* event) {
    if (pickDragItem_) {
        DraggableCanvasItem* it = pickDragItem_;
        pickDragItem_ = nullptr;
        if (const bool wasClick = it->endTranslationDrag(event->scenePos())) {
            const std::optional<int> next = pickCycle_.advanceOnClick(
                event->scenePos().x(), event->scenePos().y(),
                layerStackAt(event->scenePos()), pickTolScene_);
            if (next && next != pickedLayerId_) emit imageLayerPressed(*next);
        } else {
            pickCycle_.reset();   // 拖拽不推进循环：清循环态，其后同点单击从无历史重新起算（同标注 moveSelectedBy）。
        }
        releasePressGrab();
        event->accept();
        return;
    }
    if (annoDragItem_) {
        DraggableCanvasItem* it = annoDragItem_;
        annoDragItem_ = nullptr;
        it->endTranslationDrag(event->scenePos());   // 原地点击发 clicked、平移发 moveRequested（此后不触碰 it）
        releasePressGrab();
        event->accept();
        return;
    }
    if (multiRectPressIndex_ >= 0) {
        const int index = multiRectPressIndex_;
        multiRectPressIndex_ = -1;
        // 原地点击判据必须在交基类**之前**取：基类送达后图元已归位拖拽态（手势内位移标志仍留存，
        // 但下标可能因本次写回触发的刷新而失效，此处提前取图元指针只为读该标志，不做其他事）。
        const SelectionRectItem* it = index < multiRectItems_.size() ? multiRectItems_[index] : nullptr;
        const bool wasClick = it && !it->gestureMoved();
        const QPointF p = event->scenePos();
        QGraphicsScene::mouseReleaseEvent(event);   // 送达抓取者：本图元自行吸附/量化并写回模型
        releasePressGrab();   // 显式抓取不随基类释放自动解除，点击/拖拽两条路径都要归还
        if (!wasClick) {
            multiRectCycle_.reset();
            return;
        }
        const std::optional<int> next = multiRectCycle_.advanceFromTarget(
            index, p.x(), p.y(), multiRectStackAt(p), pickTolScene_);
        if (next && *next != activeMultiRect_) emit multiRectSelectionRequested(*next);
        return;
    }
    QGraphicsScene::mouseReleaseEvent(event);
}

// 清除按下时建立的鼠标抓取。两条遗留路径都需归还：
//   · 隐式抓取（基类自动抓取）只在基类释放处理里随「最后按键抬起」解除——场景自持的释放分支不交基类时须补清；
//   · 显式抓取（图元自调 grabMouse，如 L2 多矩形场景路由）**即使交了基类也不解除**（基类只回收隐式抓取）。
// 被按项与拖动项可以不同（重叠时按下落在最顶图元、拖动跟随选中项），残留抓取挂在被按项上。
void CanvasScene::releasePressGrab() const {
    if (QGraphicsItem* g = mouseGrabberItem()) g->ungrabMouse();
}

// 外部打断（捏合/滚轮缩放抢占）：各图元尚未结算的手势就地归位，最后解除抓取（必要性见头文件）。
// 归位取向按「本图元拖拽中是否已写模型」分两类：只动本地预览/视觉位移的→作废（手柄预览、标注与
// 落位框平移、网格基准手柄）；拖拽中已逐帧写模型的→不回滚，按释放同口径就地结算（选区框）。
void CanvasScene::cancelActiveItemDrag() {
    if (pickDragItem_) {
        pickDragItem_->cancelTranslationDrag();
        pickDragItem_ = nullptr;
        pickCycle_.reset();   // 本次拖拽永不结算：循环态无从推进，与释放路径的拖拽分支同口径。
    }
    if (annoDragItem_) {
        annoDragItem_->cancelTranslationDrag();
        annoDragItem_ = nullptr;
    }
    // 手柄预览不碰模型（提交只在释放的 transformRequested），逐个归位无重入风险。
    for (AnnotationItem* it : annoItems_) it->cancelHandleDrag();
    // 以下两条结算会写回模型并触发刷新（图元有可能在其中被重建），而同时刻只可能一个图元在手势中，
    // 故命中即短路：不在循环里握可能失效的旧指针。
    if (selItem_ && selItem_->isDragging()) {
        selItem_->cancelDrag();
    } else {
        for (SelectionRectItem* it : multiRectItems_)
            if (it->isDragging()) { it->cancelDrag(); break; }
    }
    // 被打断的本笔永不结算：按下归属与循环态一并归零，重入从新目标重新起算。
    multiRectPressIndex_ = -1;
    multiRectCycle_.reset();
    if (pickerItem_) pickerItem_->cancelDrag();   // 纯本地按下态、不发信号
    if (gridLayer_.handlesReady()) {
        if (GridHandleItem* h = gridLayer_.originHandle(); h && h->isDragging()) h->cancelDrag();
        else if (GridHandleItem* h = gridLayer_.cellHandle(); h && h->isDragging()) h->cancelDrag();
    }
    releasePressGrab();   // 解除抓取放最后：上面各归位路径都不依赖抓取状态
}

// 标注选取模式：切换全部已提交标注图元的左键可交互性（预览图元恒不可交互，不受影响）。
// 开→标注可点选 / 拖拽 / 手柄；关→标注不吃鼠标，框选 / 拾取手势不被抢走。
void CanvasScene::setAnnotationSelectMode(const bool on) {
    annotationSelectMode_ = on;
    const auto annoButtons = on ? Qt::LeftButton : Qt::NoButton;
    for (AnnotationItem* it : annoItems_) it->setAcceptedMouseButtons(annoButtons);
    // 标注选取态：z 位于标注之上（选区 60 / 单元 / 多矩形）的覆盖图元改吃无鼠标，使被这些框盖住的标注
    //   也能点中（否则顶图元抢占按下事件、下方标注收不到）；退态恢复其可交互（仅左键，与既有语义一致）。
    const auto overlayButtons = on ? Qt::NoButton : Qt::LeftButton;
    if (selItem_) selItem_->setAcceptedMouseButtons(overlayButtons);
    for (SelectionRectItem* it : multiRectItems_) it->setAcceptedMouseButtons(overlayButtons);
    if (pickerItem_) pickerItem_->setAcceptedMouseButtons(overlayButtons);
    refreshGridHandleInteraction();   // 标注选取态下网格基准手柄同样让位。
}

} // namespace idc::gui
