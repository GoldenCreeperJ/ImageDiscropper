// ============================================================================
// 文件：canvas/canvas_scene_annotations.cpp
// 作用：CanvasScene 的「标注」图元域成员实现（同类分 TU 编译）——标注矢量图元增量维护/拖拽预览/
//       显隐/清空/手柄尺寸。CanvasScene 类不拆分，仅按职责分 TU 编译。
// ============================================================================
#include "canvas/canvas_scene.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <ranges>
#include <vector>

#include <QGraphicsSceneMouseEvent>
#include <QTransform>

#include "canvas/items/z_order.h"
#include "geometry/shapes.h"
#include "model/annotation_bridge.h"

namespace idc::gui {


// 依 AnnotationBridge 增量维护标注矢量图元与拖拽预览图元。
// 【拖拽安全】按数量增删末位图元、逐个刷新几何；绝不 clear+重建（否则会在拖拽中删除
// 正在处理鼠标事件的图元→崩溃）；且跳过对正在拖拽图元的几何回设（其位置由手势维护）。
void CanvasScene::updateAnnotations(const AnnotationBridge& model) {
    const std::vector<annotation::Annotation>& anns = model.annotations();
    const int n = static_cast<int>(anns.size());

    // 收缩：删除多余末位图元（防御：绝不删除正在拖拽者）。
    while (annoItems_.size() > n) {
        AnnotationItem* it = annoItems_.last();
        if (it->isDragging()) break;
        annoItems_.removeLast();
        removeItem(it);
        delete it;
    }
    // 扩张：为新增标注创建图元并连接信号（发射时按指针查当前下标，避免下标漂移）。
    while (annoItems_.size() < n) {
        auto* it = new AnnotationItem();
        it->setHandleSize(annoHandleSize_);
        // 依当前标注选取模式决定图元是否吃左键（默认框选态＝不吃，手势落到视图框选）。
        it->setAcceptedMouseButtons(annotationSelectMode_ ? Qt::LeftButton : Qt::NoButton);
        addItem(it);
        connect(it, &AnnotationItem::pressed, this, &CanvasScene::annotationSelectRequested);
        connect(it, &AnnotationItem::moveRequested, this, [this, it](const double dx, const double dy) {
            if (const int idx = static_cast<int>(annoItems_.indexOf(it)); idx >= 0) emit annotationMoved(idx, dx, dy);
        });
        connect(it, &AnnotationItem::transformRequested,
                this, [this, it](const double sx, const double sy, const double deg) {
                    if (const int idx = static_cast<int>(annoItems_.indexOf(it)); idx >= 0) emit annotationTransformed(idx, sx, sy, deg);
        });
        connect(it, &AnnotationItem::transformPreview,
                this, [this, it](const double sx, const double sy, const double deg) {
                    if (const int idx = static_cast<int>(annoItems_.indexOf(it)); idx >= 0) emit annotationTransformPreview(idx, sx, sy, deg);
        });
        annoItems_.append(it);
    }
    // 刷新几何与选中态：跳过正在拖拽者（其位置/几何由手势维护，释放时才回灌）。
    const std::optional<std::size_t> sel = model.selectedIndex();
    for (int i = 0; i < annoItems_.size(); ++i) {
        AnnotationItem* it = annoItems_[i];
        it->setVisible(annotationsVisible_);
        if (i < n && !it->isDragging()) it->setAnnotation(anns[static_cast<std::size_t>(i)]);
        it->setSelectedState(sel.has_value() && *sel == static_cast<std::size_t>(i));
    }

    // 拖拽预览（橡皮筋矢量）与已提交图元解耦，走同一刷新入口。
    updatePendingAnnotation(model);
}

// 仅刷新拖拽预览图元：以当前属性临时封装一个 Annotation 渲染，不进 Core 历史。
// 绘制手势逐帧只调本方法，避免逐帧重建/克隆已提交标注（性能）。
void CanvasScene::updatePendingAnnotation(const AnnotationBridge& model) {
    if (const geometry::Shape* ps = model.pendingShape(); ps && annotationsVisible_) {
        if (!pendingAnnoItem_) {
            pendingAnnoItem_ = new AnnotationItem();
            pendingAnnoItem_->setHandleSize(annoHandleSize_);
            pendingAnnoItem_->setAcceptedMouseButtons(Qt::NoButton);  // 预览不可交互
            // 橡皮筋预览抬到已提交标注之上（构造默认落在 kAnnotation，与后提交的标注同 z 会被盖住）。
            pendingAnnoItem_->setZValue(zorder::kAnnotationPreview);
            addItem(pendingAnnoItem_);
        }
        annotation::Annotation tmp;
        tmp.shape = ps->clone();
        tmp.style.color = model.currentColor();
        tmp.style.fillColor = model.currentFillColor();
        tmp.style.strokeWidth = model.currentStrokeWidth();
        tmp.style.fill = model.currentFill();
        pendingAnnoItem_->setAnnotation(tmp);
        pendingAnnoItem_->setVisible(true);
    } else if (pendingAnnoItem_) {
        pendingAnnoItem_->setVisible(false);
    }
}

// 标注图层显隐：只切图元可见性，不删除（保留 Core 标注数据）。
void CanvasScene::setAnnotationsVisible(const bool on) {
    annotationsVisible_ = on;
    for (AnnotationItem* it : annoItems_) it->setVisible(on);
    if (pendingAnnoItem_ && !on) pendingAnnoItem_->setVisible(false);
}

// 清空全部标注图元（含预览）。QGraphicsItem 析构会自动从场景移除。
void CanvasScene::clearAnnotations() {
    for (AnnotationItem* it : annoItems_) { removeItem(it); delete it; }
    annoItems_.clear();
    if (pendingAnnoItem_) { removeItem(pendingAnnoItem_); delete pendingAnnoItem_; pendingAnnoItem_ = nullptr; }
}

// 命中场景坐标处的标注显示色：标注层隐藏→nullopt。橡皮筋预览图元最顶，先判它；
// 再对已提交图元逆序（annoItems_ 下标＝z 序、大＝顶，同 z 后提交者盖上面）取首个可见命中。
std::optional<core::Color> CanvasScene::annotationColorAt(const QPointF& scenePos) const {
    if (!annotationsVisible_) return std::nullopt;
    if (pendingAnnoItem_ && pendingAnnoItem_->isVisible())
        if (const auto c = pendingAnnoItem_->colorAtScene(scenePos)) return c;
    for (const auto item : std::ranges::reverse_view(annoItems_)) {
        if (!item || !item->isVisible()) continue;
        if (const auto c = item->colorAtScene(scenePos)) return c;
    }
    return std::nullopt;
}

// 设置标注控制点手柄尺寸（同时缓存供后续新建图元使用）。
void CanvasScene::setAnnotationHandleSize(const qreal sceneUnits) {
    annoHandleSize_ = sceneUnits;
    for (AnnotationItem* it : annoItems_) it->setHandleSize(sceneUnits);
    if (pendingAnnoItem_) pendingAnnoItem_->setHandleSize(sceneUnits);
}

// 底图图层显隐：仅切换 base 图元可见性（不影响遮罩 / 标注 / 导出）。

} // namespace idc::gui
