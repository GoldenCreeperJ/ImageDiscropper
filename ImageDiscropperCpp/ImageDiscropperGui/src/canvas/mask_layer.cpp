// ============================================================================
// 文件：canvas/mask_layer.cpp
// 作用：实现保留/删除遮罩的重建、显隐与清理（见同名头文件说明）。
// 分块依据：颜色常量集中定义；rebuild 只做「按 Core 结果创建矩形图元」，无几何运算。
// ============================================================================
#include "canvas/mask_layer.h"

#include <QColor>
#include <QGraphicsScene>
#include <QPen>

#include "canvas/z_order.h"

namespace idc::gui {
namespace {

// 视觉规范（本目录 README「画布视觉规范」）：保留绿 rgba(0,200,0,60)、删除红 rgba(200,0,0,60)。
constexpr QColor kKeepColor(0, 200, 0, 60);
constexpr QColor kDeleteColor(200, 0, 0, 60);

// 把一个矩形图元设为「纯显示」：不接收鼠标、不可选中，避免遮挡选区交互。
void makePassive(QGraphicsRectItem* item) {
    item->setAcceptedMouseButtons(Qt::NoButton);
    item->setFlag(QGraphicsItem::ItemIsSelectable, false);
    item->setPen(Qt::NoPen);
}

} // namespace

// 依 Core 结果重建遮罩。
void MaskLayer::rebuild(QGraphicsScene& scene, const engine::EngineResult& result,
                        const int width, const int height, const bool visible) {
    clear(); // 先移除旧遮罩。

    // 不显示、引擎失败或无保留块 → 不画遮罩。
    if (!visible || !result.ok || result.kept.empty()) {
        return;
    }

    // 红色删除底：铺满整幅原图（透出红处即被删除区）。
    deleteBase_ = scene.addRect(0.0, 0.0, width, height,
                                Qt::NoPen, QBrush(kDeleteColor));
    makePassive(deleteBase_);
    deleteBase_->setZValue(zorder::kDelete);

    // 绿色保留块：逐个叠加 kept 片段（原图像素坐标）。
    for (const auto& frag : result.kept.fragments()) {
        const auto& r = frag.region;
        if (r.empty()) continue; // 跳过退化区域。
        QGraphicsRectItem* item = scene.addRect(
            QRectF(r.left, r.top,
                   r.width(), r.height()),
            Qt::NoPen, QBrush(kKeepColor));
        makePassive(item);
        item->setZValue(zorder::kKeep);
        keepItems_.push_back(item);
    }
}

// 显隐切换（不重建）。
void MaskLayer::setVisible(const bool visible) const {
    if (deleteBase_) deleteBase_->setVisible(visible);
    for (QGraphicsRectItem* item : keepItems_) {
        if (item) item->setVisible(visible);
    }
}

// 清空全部遮罩图元。QGraphicsItem 析构会自动从所属场景移除。
void MaskLayer::clear() {
    delete deleteBase_;
    deleteBase_ = nullptr;
    for (const QGraphicsRectItem* item : keepItems_) {
        delete item;
    }
    keepItems_.clear();
}

} // namespace idc::gui
