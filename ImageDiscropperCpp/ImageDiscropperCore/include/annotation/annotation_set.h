// ============================================================================
// 文件：include/annotation/annotation_set.h
// 作用：标注集合 AnnotationSet——**纯数据**标注列表 + zOrder 恒等下标不变式 +
//       命中检测（hitTest）+ 光栅合成（burnIn），并作为 SPEC §7 config `annotations`
//       段的读写容器（engine save/load、CLI 消费）。
// 分块依据：自原 AnnotationLayer 会话状态机拆分而来（annotation_layer.h 已删除）——
//       本头只承载「无头侧也成立」的数据与合成能力；编辑模式 / 选中 / 属性模板 /
//       撤销重做等**交互会话态**归 GUI 层 AnnotationEditor（app 层），不进 Core。
//   - Annotation : 一次提交的标注记录（几何 + 样式），拷贝语义为深拷贝 shape。
//   - AnnotationSet : 拥有标注列表；结构操作（add/removeAt/moveItem/restore/clear）
//       后统一规范化 zOrder ＝ 下标（0＝最底、大＝顶，列表下标是命中/绘制/层序唯一真相源）。
// ============================================================================
#pragma once

#include <cstddef>
#include <memory>
#include <optional>
#include <vector>

#include "annotation/rasterizer.h" // PaintStyle（内嵌为样式单一真相源）
#include "core/color.h"
#include "core/image.h"
#include "core/point.h"
#include "geometry/shape_type.h"
#include "geometry/shapes.h"

namespace idc::annotation {

// ---------------------------------------------------------------------------
// Annotation：一个已提交的形状记录（几何 + 样式属性）
// ---------------------------------------------------------------------------
struct Annotation {
    std::unique_ptr<geometry::Shape> shape;  // 几何数据（多态）
    // 渲染样式：内嵌光栅化参数包 PaintStyle 作单一真相源（color / fillColor / strokeWidth /
    // fill / antialias），消除与 PaintStyle 的字段重复；burnIn 经此快照直接喂给 rasterize。
    PaintStyle style;
    geometry::ShapeType shapeType{geometry::ShapeType::LINE}; // 形状类型
    // 叠放次序（z 序）属性：0＝最底、越大越靠上。**恒等于其在标注列表中的下标**
    // ——列表下标仍是内存中命中/绘制/选中的唯一真相源（见 hitTest/burnIn），此字段为其显式副本，
    // 随 SPEC §7 config 的 annotations 段逐条导入/导出，使叠放次序成为可持久化、可手改的一等属性。
    // 任何增删移/恢复都会重新规范化（zOrder ＝ 下标），二者永不背离。
    int zOrder{0};                            // 叠放次序（恒等于列表下标，0＝最底、大＝顶）

    // 拷贝构造：深拷贝 shape。
    Annotation(const Annotation& o);
    Annotation& operator=(const Annotation& o);
    Annotation(Annotation&&) noexcept = default;
    Annotation& operator=(Annotation&&) noexcept = default;
    Annotation() = default;
};

// ---------------------------------------------------------------------------
// AnnotationSet：标注集合（纯数据 + zOrder 不变式 + 命中 / 合成）
// 不含底图、不含历史栈、不含交互态：烧录一律经 burnIn(base) 显式传入底图。
// ---------------------------------------------------------------------------
class AnnotationSet {
public:
    // ------ 结构操作（每次成功后自动规范化 zOrder ＝ 下标）------
    // 追加一条标注（zOrder ＝ 尾下标）；空 shape 的残缺条目拒绝，返回 false。
    bool add(Annotation annotation);
    // 删除指定下标；越界返回 false（不做任何事）。
    bool removeAt(std::size_t index);
    // 把 from 处标注移动到 to（下标即 z 序，大＝顶）；区间内其余项依次平移、相对顺序不变。
    // 越界或 from==to 返回 false（不做任何事）。
    bool moveItem(std::size_t from, std::size_t to);
    // 弹出末尾（顶层）标注；空集返回 nullopt——供上层撤销栈把「撤销一步」映射为「弹尾入重做栈」。
    std::optional<Annotation> popBack();
    // 清空全部标注。
    void clear();
    // 整体替换（配置还原专用，SPEC §7 annotations 段）：按调用方给定顺序逐条收编
    // （不在此排序：多来源合并时 zOrder 可能重叠，顺序由调用方负责，如载入前已按 zOrder 稳定排序），
    // 跳过无几何的残缺条目，末尾规范化 zOrder ＝ 下标。
    void restore(std::vector<Annotation> items);

    // ------ 访问 ------
    const std::vector<Annotation>& annotations() const { return items_; }
    std::size_t size() const { return items_.size(); }
    bool empty() const { return items_.empty(); }
    // 可写引用：供上层编辑会话**就地**改样式 / 几何（EDIT 模式改选中项不推快照的语义）；
    // 只改内容不改顺序/长度时无需规范化（zOrder 不因内容编辑而变），改完列表形态须调用 canonicalizeZOrder。
    std::vector<Annotation>& mutableAnnotations() { return items_; }
    // 重写每条标注的 zOrder ＝ 当前下标（不变式收口，结构操作内部已自动调用）。
    void canonicalizeZOrder();

    // ------ 查询与合成（口径与原 AnnotationLayer 完全一致）------
    // 命中检测：从顶层开始返回第一个命中的索引（填充形状优先判内部，其余按轮廓邻近判定），未命中 nullopt。
    std::optional<std::size_t> hitTest(const core::Point2D& p) const;
    // 以传入底图为基准逐条光栅化全部标注（后入列表者覆盖先入者），返回新 RGBA 图像；
    // 底图为空时退化为 1×1 透明图。不修改集合自身。
    core::Image burnIn(const core::Image& base) const;

private:
    std::vector<Annotation> items_;
};

} // namespace idc::annotation
