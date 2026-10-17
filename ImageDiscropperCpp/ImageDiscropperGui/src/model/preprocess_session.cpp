// ============================================================================
// 文件：model/preprocess_session.cpp
// 作用：实现 model/preprocess_session.h——预处理会话态的域不变式 / 归一化 / 签名推演 / base_ 缓存维护。
// 分块依据：静态纯策略（作用于给定管线）与实例态维护分列。
// ============================================================================
#include "model/preprocess_session.h"

#include <utility>
#include <variant>

#include "preprocess/preprocess_config.h"

namespace idc::gui {

namespace {
// 几何步判定统一委托 Core preprocess::isGeometryStep（与面板 / 折叠同源，口径不漂移）。
bool isGeo(const preprocess::PreprocessConfig& cfg) { return preprocess::isGeometryStep(cfg); }
} // namespace

// 几何块长度 = 首色彩步下标（几何恒在前、色彩恒在后）。
std::size_t PreprocessSession::geoBlockLen(const preprocess::PreprocessPipeline& steps) {
    std::size_t n = 0;
    for (const auto& cfg : steps.items()) {
        if (!isGeo(cfg)) break;
        ++n;
    }
    return n;
}

// 按类型不变式追加：几何步插到几何块末尾（首色之前）、色彩步追加队尾——steps 恒 [全部几何][全部色彩]。
void PreprocessSession::appendKeepingInvariant(preprocess::PreprocessPipeline& steps,
                                               const preprocess::PreprocessConfig& cfg) {
    if (isGeo(cfg)) steps.insert(geoBlockLen(steps), cfg);   // 落在几何块末尾、色彩块之前
    else steps.add(cfg);                                     // 色彩步恒在队尾
}

// 块内换序限幅：仅相邻同类才移一格；越界/跨块不动并返回 false。
bool PreprocessSession::moveWithinBlock(preprocess::PreprocessPipeline& steps,
                                        const std::size_t index, const bool down) {
    const auto& items = steps.items();
    if (index >= items.size()) return false;
    // 块内移动限幅：仅当相邻项与本项同类（同属几何块或同属色彩块）才换序，避免跨块破坏 geo→color 不变式。
    const std::size_t j = down ? index + 1 : index - 1;
    if (down && j >= items.size()) return false;
    if (!down && j >= items.size()) return false;   // index==0 时 index-1 下溢落入此分支
    if (isGeo(items[index]) != isGeo(items[j])) return false;
    if (down) steps.moveDown(index);
    else steps.moveUp(index);
    return true;
}

// 有效管线 = [resize?] + 轻操作队列——几何签名与 base_ 前缀、序列化的唯一真相源。
preprocess::PreprocessPipeline PreprocessSession::makeEffective(
    const std::optional<ResizeTarget>& resize, const preprocess::PreprocessPipeline& steps) {
    preprocess::PreprocessPipeline out;
    if (resize) out.add(preprocess::ResizeOp{resize->width, resize->height});
    for (const auto& cfg : steps.items()) out.add(cfg);
    return out;
}

// 载入归一化：散落 ResizeOp 抽出为独立 resize（取最后一个正尺寸、非正丢弃），其余按序归位 geo 前 color 后。
// 「多个 resize 取最后一个」是刻意归一（resize 恒从原图重烤、不叠加），并非等价重放。
PreprocessSession::Loaded PreprocessSession::normalizeLoaded(
    const preprocess::PreprocessPipeline& pipeline) {
    Loaded out;
    std::optional<ResizeTarget> resize;
    preprocess::PreprocessPipeline geo, color;
    for (const auto& cfg : pipeline.items()) {
        if (std::holds_alternative<preprocess::ResizeOp>(cfg)) {
            if (const auto&[width, height] = std::get<preprocess::ResizeOp>(cfg); width > 0 && height > 0)
                resize = ResizeTarget{width, height};
            continue;   // 任何 ResizeOp 都不入轻队列（含非正尺寸的退化 resize）。
        }
        if (isGeo(cfg)) geo.add(cfg);
        else color.add(cfg);
    }
    for (const auto& cfg : geo.items()) out.steps.add(cfg);
    for (const auto& cfg : color.items()) out.steps.add(cfg);
    out.resize = resize;
    return out;
}

// 队列灰度推演：任一 GrayOp 使输出恒为灰度（下游 invert/rotate/flip/resize/split 均不恢复彩色）。
bool PreprocessSession::producesGray() const {
    for (const auto& cfg : steps_.items())
        if (std::holds_alternative<preprocess::GrayOp>(cfg)) return true;
    return false;
}

// 记录一段前缀的重放产物为 base_（图像由 Controller 经 EngineBridge 物化后灌入）。
void PreprocessSession::setBase(core::Image img, preprocess::PreprocessPipeline prefix) {
    base_ = std::move(img);
    basePath_ = std::move(prefix);
}

// 会话失效：清空轻队列、独立 resize 与 base_ 缓存（换图 / 重置预处理复用；不含 working_ 还原，那归 Document）。
void PreprocessSession::clear() {
    steps_.clear();
    resize_.reset();
    base_.reset();
    basePath_.clear();
}

} // namespace idc::gui
