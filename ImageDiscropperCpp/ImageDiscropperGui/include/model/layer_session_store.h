// ============================================================================
// 文件：model/layer_session_store.h
// 作用：LayerSessionStore——GUI 侧「每层会话 + 物化缓存」的零 Qt 内聚容器（按 layerId 单点持有），
//       把原先散落两处的缓存收敛到一处：
//         · Document::layerSessions_[id]（每层预处理声明态会话，含 base 缓存槽）；
//         · PreprocessController::layerSig_[id] / layerMat_[id]（逐层物化位图 + 来源签名）。
//       三类同键数据现由本 store 统一持有、统一按 id 生命周期回收（删层 / 清栈 / 换图 / 配置重载一处搞定）。
// 分块依据：
//   - 与 PreprocessSession / AnnotationEditor 同构——纯 C++、零 Qt 依赖，可被无头 unit_tests 直编回归
//     （会话路由 / 签名比对 / 剔除陈旧层的语义第一次可脱离 GUI 直接测试）。
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

#include "canvas/layer.h"          // canvas::ImageLayer / LayerSourceKind（makeSig 读来源态）
#include "core/image.h"            // core::Image（物化位图）
#include "generate/pattern.h"      // generate::PatternParams（配方层签名字段）
#include "model/preprocess_session.h" // PreprocessSession（逐层声明态会话）
#include "preprocess/preprocess_pipeline.h"

namespace idc::gui {

// ---------------------------------------------------------------------------
// LayerSig：图层物化签名——只覆盖 materializeLayer 读取的来源态（kind / 逐层 preprocess / 文件名 /
//   解码像素布局 / 配方参数 + 目标尺寸），恒不含 transform.x/y/anchor/opacity/zOrder——改落位或
//   不透明度只重组、不重跑预处理（缺口 1）。解码像素以「空态 + 宽高」作内容代理（同一 ImageLayer 的
//   decodedPixels 一经注入即随层生命周期不变，改名/换图会经加载路径重建，宽高变化足以触发重算）。
//   （自 PreprocessController::LayerSig 逐字迁入，纯内聚重构，零语义改动。）
// ---------------------------------------------------------------------------
struct LayerSig {
    int kind{0};                                  // canvas::LayerSourceKind 序号
    preprocess::PreprocessPipeline pre;           // 逐层预处理（物化输入）
    std::string fileName;                         // 文件层相对名
    int decW{0};
    int decH{0};
    bool decEmpty{true};                          // 文件层解码像素布局（空态/尺寸）作内容代理
    int tw{0};
    int th{0};                                    // 目标尺寸（仅配方层参与签名：makePattern 按尺寸现算）
    bool hasRecipe{false};
    std::optional<generate::PatternParams> recipe;

    bool operator==(const LayerSig& o) const {
        if (kind != o.kind || fileName != o.fileName) return false;
        if (decEmpty != o.decEmpty || decW != o.decW || decH != o.decH) return false;
        if (!(pre.items() == o.pre.items())) return false;   // 预处理队列逐项相等
        if (hasRecipe != o.hasRecipe) return false;
        if (hasRecipe && !(recipe == o.recipe)) return false;
        // 文件层缩放发生在 composeCanvas 的 scaleToTarget（物化与目标尺寸无关）→ 仅配方层比较 targetW/H。
        if (kind == static_cast<int>(canvas::LayerSourceKind::Recipe) && (tw != o.tw || th != o.th)) return false;
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
    // 清空全部会话与位图（换图 setImage / 起新图层画布 beginLayerCanvas / 回退单图 clearLayers / 配置重载）。
    void clear();
    // 剔除不在 alive 集合内的层（会话 + 位图一并回收）：控制器每次同步后调用，防陈旧位图驻留 / id 复用误命中。
    void prune(const std::unordered_set<int>& alive);

    // 物化位图（缩放前）：以 layerId 索引，可直接作 EngineBridge::composeCanvas 的 materialized 入参（零拷贝透传）。
    std::unordered_map<int, core::Image>& materialized() { return materialized_; }
    const std::unordered_map<int, core::Image>& materialized() const { return materialized_; }

    // 依图层现声明算其物化签名（自 PreprocessController::makeLayerSig 迁入）。
    static LayerSig makeSig(const canvas::ImageLayer& layer);

private:
    std::map<int, LayerSession> sessions_;              // 每层会话（声明态 + 签名），按 layerId
    std::unordered_map<int, core::Image> materialized_; // 每层物化位图（并列存放，直传 composeCanvas）
};

} // namespace idc::gui
