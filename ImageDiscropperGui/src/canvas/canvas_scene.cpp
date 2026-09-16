// ============================================================================
// 文件：canvas/canvas_scene.cpp
// 作用：实现 CanvasScene 的图层装配与刷新（见同名头文件说明）。
// 分块依据：底图/遮罩/切割线/选区四类图元各由专门方法管理；场景不含切割或几何逻辑，
//           仅按 Core 结果与 Document 状态渲染（A-0.1）。
// ============================================================================
#include "canvas/canvas_scene.h"

#include <cstdlib>
#include <vector>

#include <QTransform>

#include "canvas/z_order.h"

namespace idc::gui {
namespace {

// 在候选坐标集 vals 中取最接近 target 者；vals 为空返回 -1。
// 用于把 Core 的切割线（xs/ys 含 0/W 边界）关联到最近的选区边。
int nearest(const std::vector<int>& vals, const int target) {
    int best = -1;
    int bestD = -1;
    for (const int v : vals) {
        const int d = std::abs(v - target);
        if (bestD < 0 || d < bestD) { bestD = d; best = v; }
    }
    return best;
}

} // namespace

// 构造：空场景。底图与选区框在 setBaseImage 时惰性创建。
CanvasScene::CanvasScene(QObject* parent) : QGraphicsScene(parent) {}

// 设置底图并同步场景矩形与选区边界。
void CanvasScene::setBaseImage(const QPixmap& preview, const double scaleX, const double scaleY,
                               const int fullW, const int fullH) {
    imgW_ = fullW;
    imgH_ = fullH;
    setSceneRect(0.0, 0.0, static_cast<qreal>(fullW), static_cast<qreal>(fullH));

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
        // 选区框的编辑事件转发为场景信号，供 MainWindow 写回 Document（视图/场景不认识 Document）。
        connect(selItem_, &SelectionRectItem::rectChanged, this, &CanvasScene::selectionEdited);
    }
    selItem_->setImageBounds(fullW, fullH);
}

// 清空画布全部内容。
void CanvasScene::clearAll() {
    maskLayer_.clear();
    clearCutLines();
    if (baseItem_) { removeItem(baseItem_); delete baseItem_; baseItem_ = nullptr; }
    if (selItem_) { removeItem(selItem_); delete selItem_; selItem_ = nullptr; }
    imgW_ = 0;
    imgH_ = 0;
}

// 刷新保留/删除遮罩（委托 MaskLayer）。
void CanvasScene::updateMasks(const idc::engine::EngineResult& result) {
    maskLayer_.rebuild(*this, result, imgW_, imgH_, masksVisible_);
}

// 遮罩显隐切换。
void CanvasScene::setMasksVisible(const bool visible) {
    masksVisible_ = visible;
    maskLayer_.setVisible(visible);
}

// 依 Core 切割线集合，判定选区四条边各自是否有「内部贯穿切割线」，把结果下发给选区框：
// 选区框据此把这些边延伸绘制为橙色贯穿线，并允许直接抓取拖动（拖动该边＝移动切割线）。
// 单矩形/十字(RECT)四边皆有；横带(HLINE)仅上/下；竖带(VLINE)仅左/右；落在图像边界(0/W/H)
// 的线不算「内部切割线」（此时选区边即图像边，内部无切割可言），对应边不延伸。
void CanvasScene::updateCutLines(const idc::engine::CutLineSet& lines, const idc::engine::RectRegion& rect) {
    if (!selItem_ || imgW_ <= 0 || imgH_ <= 0) { clearCutLines(); return; }

    // 每条边取 Core 线集中最接近该边者，落在图像内部才视为有效贯穿切割线。
    const int lx = nearest(lines.xs, rect.left);
    const int rx = nearest(lines.xs, rect.right);
    const int ty = nearest(lines.ys, rect.top);
    const int by = nearest(lines.ys, rect.bottom);
    const bool left   = (lx > 0 && lx < imgW_);
    const bool right  = (rx > 0 && rx < imgW_);
    const bool top    = (ty > 0 && ty < imgH_);
    const bool bottom = (by > 0 && by < imgH_);
    selItem_->setCutEdges(left, right, top, bottom);
}

// 清空切割线（换图/关闭图像/无选区时）：取消选区框的边延伸标记。
void CanvasScene::clearCutLines() {
    if (selItem_) selItem_->setCutEdges(false, false, false, false);
}

// 依 Document 的选区同步选区框显示。
void CanvasScene::syncSelection(const idc::engine::RectRegion& rect, const bool hasRect) {
    if (!selItem_) return; // 尚无图像。
    if (!hasRect) {
        selItem_->setVisible(false);
        return;
    }
    selItem_->setRect(QRectF(static_cast<qreal>(rect.left), static_cast<qreal>(rect.top),
                             static_cast<qreal>(rect.width()), static_cast<qreal>(rect.height())));
    selItem_->setVisible(true);
}

} // namespace idc::gui
