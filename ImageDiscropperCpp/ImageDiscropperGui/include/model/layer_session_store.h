// ============================================================================
// 文件：model/layer_session_store.h
// 作用：LayerSessionStore——GUI 侧「每层会话 + 物化缓存」的零 Qt 内聚容器（按 layerId 单点持有），
//       统一持有三类同键数据：
//         · 每层预处理声明态会话（含 base 缓存槽）；
//         · 逐层物化位图；
//         · 上次物化的来源签名。
//       三者按 id 统一生命周期回收（删层 / 清栈 / 换图 / 配置重载一处搞定）。
// 分块依据：
//   - 与 PreprocessSession / AnnotationEditor 同构——纯 C++、零 Qt 依赖，可被无头 unit_tests 直编回归
//     （会话路由 / 签名比对 / 剔除陈旧层的语义可脱离 GUI 直接测试）。
//   - 只存数据与做纯查改，**绝不跑像素**：materializeLayer（EngineBridge/Core）仍由 PreprocessController
//     驱动并写回本 store 的位图槽（分层纪律，与 PreprocessSession 不持引擎同理）。
//   - 物化位图以**并列 unordered_map<int,core::Image>** 存于本 store（而非嵌进 LayerSession）：Core
//     composeCanvas 的 materialized 入参硬性要求 map<int,Image>，并列存放即可零拷贝直传，免逐帧临时建表
//     （拖动图层＝逐帧重组，嵌进 per-id 结构会引入每帧深拷贝，抵消缓存收益）。
// ============================================================================
#pragma once

#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <map>

#include "canvas/layer.h"          // canvas::ImageLayer（makeSig 读来源态）
#include "core/image.h"            // core::Image（物化位图）
#include "generate/pattern.h"      // generate::PatternParams（配方层签名字段）
#include "model/preprocess_session.h" // PreprocessSession（逐层声明态会话）
#include "preprocess/preprocess_pipeline.h"

namespace idc::gui {

// ---------------------------------------------------------------------------
// LayerSig：图层物化签名——覆盖 materializeLayer（装配序见 SPEC §2.2a 唯一口径）读取的全部输入：
//   来源态（图片元素有无由 fileName / 解码像素布局 / 内层落位派生）+ 元素外观（image/backdrop 两段 preprocess/opacity）+
//   图案元素配方 + 目标尺寸 + 内层落位与图层框。缩放与进框发生在 materializeLayer 内，整层位图依赖它们，故均入键；
//   transform.x/y/anchor/layerPivot/opacity/zOrder 恒不入键——改落位或不透明度只重组、不重跑。
//   解码像素以「空态 + 宽高」作内容代理。
// ---------------------------------------------------------------------------
struct LayerSig {
    preprocess::PreprocessPipeline pre;           // 节点外观（层级预处理，物化输入）
    std::string fileName;                         // 文件层相对名
    int decW{0};
    int decH{0};
    bool decEmpty{true};                          // 文件层解码像素布局（空态/尺寸）作内容代理
    int tw{0};
    int th{0};                                    // 目标尺寸（恒入键：materializeLayer 按此缩放）
    // ---- 图片元素外观（物化输入）----
    preprocess::PreprocessPipeline imagePre;      // imagePreprocess
    std::uint8_t imageOpacity{255};               // imageOpacity
    // ---- 图案元素（物化输入）----
    bool hasBackdrop{false};
    std::optional<generate::PatternParams> backdrop;
    preprocess::PreprocessPipeline backdropPre;   // backdropPreprocess
    std::uint8_t backdropOpacity{255};            // backdropOpacity
    // ---- 内层落位 / 图层框（materializeLayer 组装阶段读取，故入键）----
    bool hasPlacement{false};                      // canvas::ImageLayer.hasImagePlacement
    int boxW{0};
    int boxH{0};                                    // 图层框尺寸
    int imageFit{0};                                // canvas::ImageFit 序号
    int imageAnchor{0};
    int imagePivot{0};                              // 内层锚点/枢轴（generate::ImageAnchor 序号）
    int imageX{0};
    int imageY{0};                                  // 内层自由偏移

    bool operator==(const LayerSig& o) const {
        if (fileName != o.fileName) return false;
        if (decEmpty != o.decEmpty || decW != o.decW || decH != o.decH) return false;
        if (pre != o.pre) return false;   // 预处理队列逐项相等（复用 PreprocessPipeline::operator==）
        if (imagePre != o.imagePre || imageOpacity != o.imageOpacity) return false;
        if (backdropPre != o.backdropPre || backdropOpacity != o.backdropOpacity) return false;
        if (hasBackdrop != o.hasBackdrop) return false;
        if (hasBackdrop && !(backdrop == o.backdrop)) return false;
        // 缩放发生在 materializeLayer 内（整层位图）→ 目标尺寸恒入键。
        if (tw != o.tw || th != o.th) return false;
        // 内层落位 / 图层框：仅启用两级落位时参与（决定组装后的框位图）。
        if (hasPlacement != o.hasPlacement) return false;
        if (hasPlacement) {
            if (boxW != o.boxW || boxH != o.boxH) return false;
            if (imageFit != o.imageFit || imageAnchor != o.imageAnchor || imagePivot != o.imagePivot) return false;
            if (imageX != o.imageX || imageY != o.imageY) return false;
        }
        return true;
    }
};

// ---------------------------------------------------------------------------
// LayerSession：某层的 GUI 会话——声明态预处理会话（含 base 槽）+ 上次物化签名（用于命中判定）。
//   物化位图不在此（见文件头「并列 map」说明），由 store 统一持有。
// ---------------------------------------------------------------------------
struct LayerSession {
    PreprocessSession preprocess;    // 声明态会话（steps_/resize_/base_ 缓存槽）
    std::optional<LayerSig> sig;     // 上次物化时的来源签名；nullopt＝尚未物化 / 已失效
};

// ---------------------------------------------------------------------------
// LayerSessionStore：按 layerId 单点持有每层会话（声明态 + 签名）与物化位图，统一生命周期。
//   由 Document 组合持有（跨存盘/撤销跟随图层声明态），PreprocessController 经 Document 驱动其刷新。
// ---------------------------------------------------------------------------
class LayerSessionStore {
public:
    // 取某层会话：无→nullptr（供 activeSession 路由与只读查询；不产生副作用）。
    LayerSession* find(int id);
    const LayerSession* find(int id) const;
    // 取某层会话，缺失则原地创建（供增层 / 配置重载 / 控制器刷新签名）。
    LayerSession& ensure(int id);
    // 删某层：连带回收其会话与物化位图（removeLayer）。
    void remove(int id);
    // 清空全部会话与位图（换图 setImage / 起新图层画布 beginLayerCanvas / 清叠加层 clearLayers / 配置重载）。
    void clear();
    // 剔除不在 alive 集合内的层（会话 + 位图一并回收）：控制器每次同步后调用，防陈旧位图驻留 / id 复用误命中。
    void prune(const std::unordered_set<int>& alive);

    // 物化位图（已组装·缩放·预处理·未落位）：以 layerId 索引，可直接作 EngineBridge::composeCanvas 的 materialized 入参（零拷贝直传）。
    std::unordered_map<int, core::Image>& materialized() { return materialized_; }
    const std::unordered_map<int, core::Image>& materialized() const { return materialized_; }

    // 依图层现声明算其物化签名。
    static LayerSig makeSig(const canvas::ImageLayer& layer);

private:
    std::map<int, LayerSession> sessions_;              // 每层会话（声明态 + 签名），按 layerId
    std::unordered_map<int, core::Image> materialized_; // 每层物化位图（并列存放，直传 composeCanvas）
};

} // namespace idc::gui
