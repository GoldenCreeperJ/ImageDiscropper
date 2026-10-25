// ============================================================================
// 文件：canvas/items/mask_layer.cpp
// 作用：实现单一遮罩图元的绘制（保留/删除合成于一层）与遮罩图层的重建、显隐、清理（见同名头文件说明）。
// 分块依据：颜色常量集中定义；rebuild 只做「按 Core 结果生成保留块矩形并交单一图元绘制」，无几何运算。
// ============================================================================
#include "canvas/items/mask_layer.h"

#include <QBrush>
#include <QColor>
#include <QGraphicsScene>
#include <QPainter>
#include <QPen>
#include <QStyleOptionGraphicsItem>

#include "canvas/items/z_order.h"
#include "app/palette.h"

namespace idc::gui {

// 视觉规范（本目录 README「画布视觉规范」）：保留绿 rgba(0,200,0,60)、删除红 rgba(200,0,0,60)
// 为出厂缺省；色相与填充 alpha 均可在「视图→外观/配色…」自定义（Palette 单例，绘制期取色）。

// 载入整幅尺寸与保留块矩形（原图像素坐标）；变化时重置几何缓存并重绘。
void MaskLayerItem::setGeometry(const int width, const int height, std::vector<QRectF> keeps) {
    prepareGeometryChange();
    width_ = width;
    height_ = height;
    keeps_ = std::move(keeps);
    // 包围盒需并上全部保留块：L3 的 Pad 余量策略下 Core 保留块区域可越出图像边界（导出时补白），
    //   仅按原图尺寸算 boundingRect 会把那部分排除在重绘区域之外。
    bounds_ = QRectF(0.0, 0.0, width, height);
    for (const QRectF& r : keeps_) bounds_ = bounds_.united(r);
    update();
}

QRectF MaskLayerItem::boundingRect() const {
    return bounds_;
}

// 单一图元内两层绘制：先铺满全图红色删除底，再叠绿色保留块（透出红处即删除、绿覆盖处即保留）。
void MaskLayerItem::paint(QPainter* painter, const QStyleOptionGraphicsItem*, QWidget*) {
    if (width_ <= 0 || height_ <= 0) return;
    painter->setPen(Qt::NoPen);
    painter->setBrush(QBrush(withAlpha(Palette::instance().deleteColor(), Palette::instance().deleteAlpha())));
    painter->drawRect(QRectF(0.0, 0.0, width_, height_));
    painter->setBrush(QBrush(withAlpha(Palette::instance().keepColor(), Palette::instance().keepAlpha())));
    for (const QRectF& r : keeps_) painter->drawRect(r);
}

// 依 Core 结果重建遮罩（单一图元）。
// 图元恒按最新结果构建，visible 只决定其可见性：显隐开关切换无需重建，也不会因隐藏期重建而丢失图元。
void MaskLayer::rebuild(QGraphicsScene& scene, const engine::EngineResult& result,
                        const int width, const int height, const bool visible) {
    clear(); // 先移除旧遮罩。

    // 无保留块（空结果哨兵：无图 / 引擎失败）→ 无遮罩可画。
    //（成功结果 kept 恒非空——引擎 kept 为空即报 E-7，故无需再判成功位。）
    if (result.kept.empty()) {
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
    item_->setVisible(visible);   // 显隐由「遮罩」图层开关决定（隐藏期重建仍留图元，开回即显）。
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
