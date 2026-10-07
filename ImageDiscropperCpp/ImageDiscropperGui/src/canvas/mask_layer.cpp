// ============================================================================
// 文件：canvas/mask_layer.cpp
// 作用：实现单一遮罩图元的绘制（保留/删除合成于一层）与遮罩图层的重建、显隐、清理（见同名头文件说明）。
// 分块依据：颜色常量集中定义；rebuild 只做「按 Core 结果生成保留块矩形并交单一图元绘制」，无几何运算。
// ============================================================================
#include "canvas/mask_layer.h"

#include <QBrush>
#include <QColor>
#include <QGraphicsScene>
#include <QPainter>
#include <QPen>
#include <QStyleOptionGraphicsItem>

#include "canvas/z_order.h"

namespace idc::gui {
namespace {

// 视觉规范（本目录 README「画布视觉规范」）：保留绿 rgba(0,200,0,60)、删除红 rgba(200,0,0,60)。
constexpr QColor kKeepColor(0, 200, 0, 60);
constexpr QColor kDeleteColor(200, 0, 0, 60);

} // namespace

// 载入整幅尺寸与保留块矩形（原图像素坐标）；变化时重置几何缓存并重绘。
void MaskLayerItem::setGeometry(const int width, const int height, std::vector<QRectF> keeps) {
    prepareGeometryChange();
    width_ = width;
    height_ = height;
    keeps_ = std::move(keeps);
    update();
}

QRectF MaskLayerItem::boundingRect() const {
    return QRectF(0.0, 0.0, width_, height_);
}

// 单一图元内两层绘制：先铺满全图红色删除底，再叠绿色保留块（透出红处即删除、绿覆盖处即保留）。
void MaskLayerItem::paint(QPainter* painter, const QStyleOptionGraphicsItem*, QWidget*) {
    if (width_ <= 0 || height_ <= 0) return;
    painter->setPen(Qt::NoPen);
    painter->setBrush(QBrush(kDeleteColor));
    painter->drawRect(QRectF(0.0, 0.0, width_, height_));
    painter->setBrush(QBrush(kKeepColor));
    for (const QRectF& r : keeps_) painter->drawRect(r);
}

// 依 Core 结果重建遮罩（单一图元）。
void MaskLayer::rebuild(QGraphicsScene& scene, const engine::EngineResult& result,
                        const int width, const int height, const bool visible) {
    clear(); // 先移除旧遮罩。

    // 不显示、引擎失败或无保留块 → 不画遮罩。
    if (!visible || !result.ok || result.kept.empty()) {
        return;
    }

    // 收集保留块矩形（原图像素坐标）；退化区域跳过。
    std::vector<QRectF> keeps;
    for (const auto& frag : result.kept.fragments()) {
        const auto& r = frag.region;
        if (r.empty()) continue;
        keeps.emplace_back(r.left, r.top, r.width(), r.height());
    }

    // 单一遮罩图元：红色删除底 + 绿色保留块合成于一层（纯显示、不吃鼠标、不可选中）。
    item_ = new MaskLayerItem();
    item_->setGeometry(width, height, std::move(keeps));
    item_->setZValue(zorder::kMask);
    item_->setAcceptedMouseButtons(Qt::NoButton);
    item_->setFlag(QGraphicsItem::ItemIsSelectable, false);
    scene.addItem(item_);
}

// 显隐切换（不重建）。
void MaskLayer::setVisible(const bool visible) const {
    if (item_) item_->setVisible(visible);
}

// 清空遮罩图元。QGraphicsItem 析构会自动从所属场景移除。
void MaskLayer::clear() {
    delete item_;
    item_ = nullptr;
}

} // namespace idc::gui
