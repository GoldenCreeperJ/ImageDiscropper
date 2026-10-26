// ============================================================================
// 文件：model/preprocess_session.h
// 作用：PreprocessSession——预处理「会话态 + 域不变式」的零 Qt 内聚对象（GUI 侧），
//       集中 Document 侧（steps_/resize_/base_/basePath_ 四件套 + geomSig）与
//       Controller 侧（geo→color 不变式、移动限幅、载入归一化、baseDirty 前缀比对、
//       effectivePipeline）的预处理策略，供 Document 组合持有、Controller 驱动。
// 分块依据：
//   - 与 AnnotationEditor 同构——都是零 Qt 依赖的 GUI 会话对象，可被无头 unit_tests 直编
//     回归（块不变式/归一化/缓存失效等策略可直编测试）。
//   - 只持声明态与派生查询，**绝不跑像素**：base_ 重放物化（EngineBridge/Core applyExec）、
//     忙碌框、信号、选区/标注清理均留在 PreprocessController（分层纪律，与 AnnotationEditor 不持引擎同理）。
//   - 纯策略（作用于给定管线、与实例态无关）以 **静态成员** 暴露，供 Controller 在「候选态提交前」
//     计算 before/after 签名而不改动当前态；实例成员维护「已提交态」与 base_ 缓存。
// 说明：steps_ 恒守「[全部几何块][全部色彩块]」不变式、独立 resize 恒在队列之外（不进 steps_）。
// ============================================================================
#pragma once

#include <cstddef>
#include <optional>
#include <vector>

#include "core/image.h"
#include "model/derived_cache.h"
#include "preprocess/preprocess_config.h"
#include "preprocess/preprocess_pipeline.h"

namespace idc::gui {

// 独立画布尺寸目标（可选，恒最先重放、从原图重采样）；非 steps_ 成员，独立于轻操作队列。
// 定义于此：使 PreprocessSession 零 Qt 可独立编译（document.h 是 QObject 头，不宜作依赖）。
struct ResizeTarget {
    int width = 0;
    int height = 0;
};

// ---------------------------------------------------------------------------
// PreprocessSession：预处理声明态（steps_ + resize_ + base_ 缓存）与其域不变式/归一化/签名推演。
// ---------------------------------------------------------------------------
class PreprocessSession {
public:
    // 载入归一化的产物：抽出独立 resize（多个取最后一个正尺寸）+ 轻队列（geo 前 color 后，各保相对序、绝不含 ResizeOp）。
    struct Loaded {
        preprocess::PreprocessPipeline steps;
        std::optional<ResizeTarget> resize;
    };

    //====== 静态纯策略（作用于给定管线，与实例态无关；供提交前算候选态，不改当前态）======

    // 几何块长度 = 首色彩步下标（几何恒在前、色彩恒在后，故 [0,geoLen) 为几何块、[geoLen,n) 为色彩块）。
    static std::size_t geoBlockLen(const preprocess::PreprocessPipeline& steps);

    // 按类型不变式追加一步：几何步插到几何块末尾（首色之前）、色彩步追加队尾——由此 steps 恒 [全部几何][全部色彩]。
    static void appendKeepingInvariant(preprocess::PreprocessPipeline& steps,
                                       const preprocess::PreprocessConfig& cfg);

    // 块内换序限幅：仅当相邻项与本项同类（同几何块或同色彩块）才上下移一格；越界/跨块则不动并返回 false。
    //   口径含 i==0 上移、末位下移的边界守卫。
    static bool moveWithinBlock(preprocess::PreprocessPipeline& steps, std::size_t index, bool down);

    // 组装「有效管线」= [resize?] + 轻操作队列——几何签名推演与 base_ 前缀、序列化的唯一真相源。
    static preprocess::PreprocessPipeline makeEffective(
        const std::optional<ResizeTarget>& resize, const preprocess::PreprocessPipeline& steps);

    // 载入队列归一化：把散落的 ResizeOp 抽出为独立 resize（取最后一个正尺寸，非正丢弃）、其余按类型
    //   重排为 geo 前 color 后（各保相对顺序，任何 ResizeOp 绝不入轻队列）。
    static Loaded normalizeLoaded(const preprocess::PreprocessPipeline& pipeline);

    //====== 实例：已提交声明态访问 ======

    const preprocess::PreprocessPipeline& steps() const { return steps_; }
    const std::optional<ResizeTarget>& resizeTarget() const { return resize_; }
    bool hasResize() const { return resize_.has_value(); }
    // 是否已被预处理（有 resize 或队列非空）——供面板/菜单启用「重置预处理」。
    bool hasPreprocess() const { return !steps_.empty() || resize_.has_value(); }

    // 当前态的有效管线（[resize?]+steps_）——几何签名与序列化的真相源。
    preprocess::PreprocessPipeline effectivePipeline() const { return makeEffective(resize_, steps_); }
    // 几何签名：对 (origW,origH) 跑有效管线的纯元数据推演（不碰像素），得 {输出宽高, 净旋转象限}。
    preprocess::PreprocessPipeline::GeometryOut geomSig(const int origW, const int origH) const {
        return effectivePipeline().geometryOutput(origW, origH);
    }
    // 队列灰度推演：任一 GrayOp 使输出恒灰度（下游不恢复彩色）——供灰度后停用「黑白」「色道分离」。
    bool producesGray() const;

    //====== 实例：base_ 重放缓存（图像由 Controller 物化后灌入；session 只存槽、比对前缀、失效）======

    bool hasBase() const { return base_.valid(); }
    const core::Image& base() const { return base_.value(); }
    // 缓存对应的 [resize?] 前缀（无缓存时返回空静态；调用方须 hasBase 守卫）。
    const preprocess::PreprocessPipeline& basePrefix() const;
    // 记录一段前缀的重放产物为 base_（prefix 为其对应的 [resize?] 前缀队列）。
    void setBase(core::Image img, const preprocess::PreprocessPipeline &prefix);
    // 释放 base_（由「有 resize」切到「无 resize」时回收内存）。
    void clearBase() { base_.clear(); }
    // 前缀是否需要重烤：无缓存，或缓存前缀与之逐字段不等 → true。
    bool baseDirtyFor(const preprocess::PreprocessPipeline& prefix) const {
        return !base_.hit(prefix);
    }

    //====== 实例：已提交态改写（供 Document 回灌 / 控制器提交成功后落定）======

    void setSteps(preprocess::PreprocessPipeline pipeline) { steps_ = std::move(pipeline); }
    void setResize(const std::optional<ResizeTarget> target) { resize_ = target; }

    // 会话失效：清空轻队列、独立 resize 与 base_ 缓存（换图 setImage / 重置预处理 resetPreprocess 复用）。
    void clear();

private:
    preprocess::PreprocessPipeline steps_;             // 声明式轻操作队列（绝不含 ResizeOp），几何签名与序列化真相源
    std::optional<ResizeTarget> resize_;               // 独立画布尺寸（可选，恒最先重放）
    // resize(original) 的物化缓存（统一 DerivedCache：Key＝[resize?] 前缀比对自愈；无 resize 不维护，省一份拷贝）。
    DerivedCache<preprocess::PreprocessPipeline, core::Image> base_;
};

} // namespace idc::gui
