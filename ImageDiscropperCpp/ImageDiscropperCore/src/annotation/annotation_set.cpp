// ============================================================================
// 文件：src/annotation/annotation_set.cpp
// 作用：实现 include/annotation/annotation_set.h——Annotation 拷贝语义与 AnnotationSet
//       的结构操作 / zOrder 不变式 / 命中检测 / 光栅合成。
// 分块依据：自原 annotation_layer.cpp 逐段搬入（纯拆分：算法与口径零改动）——
//       会话态（编辑模式 / 选中 / 属性模板 / 撤销重做）已随拆分离开 Core，归 GUI AnnotationEditor。
// ============================================================================
#include "annotation/annotation_set.h"

#include <utility>

#include "annotation/rasterizer.h"

namespace idc::annotation {

// ===========================================================================
// Annotation 拷贝语义
// ===========================================================================

// 拷贝构造：深拷贝 shape 指针指向的几何对象。
Annotation::Annotation(const Annotation& o)
    : shape(o.shape ? o.shape->clone() : nullptr),
      style(o.style),
      shapeType(o.shapeType),
      zOrder(o.zOrder) {}

// 拷贝赋值：先释放旧 shape，再深拷贝。
Annotation& Annotation::operator=(const Annotation& o) {
    if (this != &o) {
        shape = o.shape ? o.shape->clone() : nullptr;
        style = o.style;
        shapeType = o.shapeType;
        zOrder = o.zOrder;
    }
    return *this;
}

// ===========================================================================
// AnnotationSet
// ===========================================================================

// 规范化叠放次序：把每条标注的 zOrder 重写为其在列表中的下标（0＝最底、大＝顶），
// 维持「zOrder 恒等于下标」不变式——供任何改变列表顺序/长度的操作后统一收尾。
void AnnotationSet::canonicalizeZOrder() {
    for (std::size_t i = 0; i < items_.size(); ++i) items_[i].zOrder = static_cast<int>(i);
}

// 追加一条标注：新标注置于顶层（zOrder ＝ 追加后的下标）；空 shape 拒绝。
bool AnnotationSet::add(Annotation annotation) {
    if (!annotation.shape) return false;
    annotation.zOrder = static_cast<int>(items_.size());
    items_.push_back(std::move(annotation));
    return true;
}

// 删除指定下标；越界不做任何事。被删项之后的标注下标前移 → 重写 zOrder。
bool AnnotationSet::removeAt(const std::size_t index) {
    if (index >= items_.size()) return false;
    items_.erase(items_.begin() + static_cast<std::ptrdiff_t>(index));
    canonicalizeZOrder();
    return true;
}

// 层序调整：把 from 处标注就地移到 to（下标即 z 序，大＝顶）。区间内其余项沿移动方向平移一格、
// 相对顺序不变；末尾重写 zOrder。
bool AnnotationSet::moveItem(const std::size_t from, const std::size_t to) {
    if (from >= items_.size() || to >= items_.size() || from == to) return false;
    Annotation item = std::move(items_[from]);
    if (from < to) {
        for (std::size_t i = from; i < to; ++i) items_[i] = std::move(items_[i + 1]);
    } else {
        for (std::size_t i = from; i > to; --i) items_[i] = std::move(items_[i - 1]);
    }
    items_[to] = std::move(item);
    canonicalizeZOrder();
    return true;
}

// 弹出末尾（顶层）标注：剩余项下标不变，无需规范化。空集返回 nullopt。
std::optional<Annotation> AnnotationSet::popBack() {
    if (items_.empty()) return std::nullopt;
    std::optional<Annotation> last;
    last.emplace(std::move(items_.back()));
    items_.pop_back();
    return last;
}

// 清空全部标注。
void AnnotationSet::clear() { items_.clear(); }

// 整体替换（配置还原专用）：按调用方给定的顺序逐条收编（不在此排序：多来源合并时 zOrder
// 可能重叠，顺序由调用方负责，如配置文件载入前已按 zOrder 稳定排序）。每条收编后其下标即层位，
// 末尾统一规范化 zOrder ＝ 下标；跳过无几何的残缺条目（与原 restoreAnnotations 守卫一致）。
void AnnotationSet::restore(std::vector<Annotation> items) {
    items_.clear();
    for (Annotation& a : items) {
        if (!a.shape) continue;
        a.zOrder = static_cast<int>(items_.size());
        items_.push_back(std::move(a));
    }
    canonicalizeZOrder();
}

// 命中检测：从顶层（最后绘制的）开始遍历，返回第一个命中的索引。
// 判定规则：
//   - 填充形状：使用 Path::contains 判断点是否在内部
//   - 未填充形状：使用 Path::distanceToOutline 判断点是否靠近轮廓
//     （容差 = 5.0 + strokeWidth）
std::optional<std::size_t> AnnotationSet::hitTest(const core::Point2D& p) const {
    for (std::size_t i = items_.size(); i-- > 0;) {
        const Annotation& ps = items_[i];
        if (!ps.shape) continue;
        // 取世界路径（已套用非破坏性变换），故命中判定与缩放/旋转/翻转后的可见几何一致。
        const geometry::Path path = ps.shape->worldPath();

        // 填充且非直线：优先按内部判定
        if (ps.style.fill && ps.shapeType != geometry::ShapeType::LINE) {
            if (path.contains(p.x, p.y)) return i;
        }
        // 其余情况按轮廓距离判定
        if (const double dist = path.distanceToOutline(p.x, p.y); dist < 5.0 + ps.style.strokeWidth) return i;
    }
    return std::nullopt;
}

// 合成（指定底图）：以传入底图为基准逐条光栅化，后入列表者覆盖先入者；
// 底图为空时退化为 1×1 透明图（与原 AnnotationLayer::burnIn(base) 行为一致）。
core::Image AnnotationSet::burnIn(const core::Image& base) const {
    core::Image out = base.empty()
        ? core::Image(1, 1, core::ImageFormat::RGBA)
        : base.toRGBA();

    for (const Annotation& ps : items_) {
        if (!ps.shape) continue;
        PaintStyle style = ps.style;   // 样式快照直接来自内嵌的 PaintStyle
        style.antialias = true;        // 合成路径固定抗锯齿（与逐条光栅化既有口径一致）
        rasterize(out, *ps.shape, style);
    }
    return out;
}

} // namespace idc::annotation
