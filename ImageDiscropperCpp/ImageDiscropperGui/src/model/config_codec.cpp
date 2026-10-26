// ============================================================================
// 文件：model/config_codec.cpp
// 作用：实现 ConfigCodec 的 Document ↔ EngineConfig 字段映射（见同名头文件说明）；
//       实现为「friend 直读私有状态」的静态函数（Document ↔ EngineConfig 映射）。
// ============================================================================
#include "model/config_codec.h"

#include <algorithm>
#include <utility>

#include "canvas/layer.h"
#include "model/document.h"
#include "model/preprocess_session.h"
#include "preprocess/preprocess_pipeline.h"

namespace idc::gui {

engine::EngineConfig ConfigCodec::buildEngineConfig(const Document& doc) {
    engine::EngineConfig cfg;

    // 源尺寸：以工作图实际尺寸为准（预处理可能改变尺寸，不能用原图尺寸）。
    cfg.source.width = doc.width();
    cfg.source.height = doc.height();

    // 切割配置（含模式/极性/几何）。
    cfg.cut = doc.buildCutConfig();

    // 显式选择集：仅 L3 使用用户点选/全选的单元序号；L1/L2 必须恒空 → 由 Core 依生成器
    // 自动推导选择区（engine.cpp：「显式 selectedCells 优先，否则按生成器几何自动推导」）。
    // 若不加模式门控，从 L3 切回 L1/L2 时残留的 L3 单元序号会被套用到形状不同的诱导网格上，
    // 使保留集算错甚至为空、遮罩消失。故此处按模式门控，维持「L1/L2 selectedCells 恒空」不变式。
    cfg.selectedCells = doc.mode_ == engine::Tier::Grid ? doc.selectedCells_ : std::vector<int>{};

    // 排序策略（FR-L3.5）：row-major / column-major / reverse / snake / custom。
    cfg.order = doc.order_;

    // 导出参数：把 GUI 侧字段搬运进 CompositionParams。
    cfg.exportParams.mode = doc.exportMode_;
    cfg.exportParams.layout = doc.layout_;
    cfg.exportParams.format = doc.format_;
    cfg.exportParams.quality = doc.quality_;
    cfg.exportParams.naming = doc.naming_.toStdString();
    cfg.exportParams.padColor = doc.padColor_;
    // 重排填充顺序（MergeOrder）：与选择排序 doc.order_ 正交，仅 Rearrange 时被 Core compose 使用。
    cfg.exportParams.mergeOrder = doc.mergeOrder_;
    // 重排画布参数（FR-L3.7）：0 视为未指定 → 保持 nullopt 交 Core compose 自动推导；
    // 仅正值写入 optional，避免用 0 覆盖 Core 的默认布局（否则画布尺寸会被算成 0）。
    if (doc.mergeCols_ > 0) cfg.exportParams.cols = doc.mergeCols_;
    if (doc.mergeRows_ > 0) cfg.exportParams.rows = doc.mergeRows_;
    if (doc.mergeCellW_ > 0) cfg.exportParams.cellWidth = doc.mergeCellW_;
    if (doc.mergeCellH_ > 0) cfg.exportParams.cellHeight = doc.mergeCellH_;

    // preprocess 保持默认空流水线：预处理已即时作用于工作图（见 setWorkingImage），不在此重复。
    return cfg;
}

// 构建可序列化配置（保存 / 载入）：复用 buildEngineConfig 的切割/选择/排序/导出字段，
// 唯 preprocess 段填入「[resize?] + 轻操作队列」的完整有效管线（resize 恒置首位）、source 取原图尺寸
//——还原端从 original 重放整链复现 working_（配置载入时在控制器侧归一化抽出 resize）。
engine::EngineConfig ConfigCodec::buildSerializableConfig(const Document& doc) {
    engine::EngineConfig cfg = buildEngineConfig(doc);
    // 序列化携带画布级完整有效管线（[resize?] + 轻操作队列，resize 恒首位）——由画布会话统一推演。
    cfg.preprocess = doc.preprocess_.effectivePipeline();
    // 画布级整体透明度随 §7 顶层 opacity 键往返（缺省 255）；与底图/图层透明度正交。
    cfg.canvasOpacity = doc.canvasOpacity_;
    // 底图声明（同构 ImageLayer）随 §7 generate 段往返（可空则段省略）：还原端据 source 尺寸 + 本段重生画布底图。
    //   位图不入配置：剥离 decodedPixels（File 底凭 fileName 相对名引用 assets）。
    if (doc.baseLayer_.has_value()) {
        canvas::ImageLayer base = *doc.baseLayer_;
        base.decodedPixels = core::Image{};
        base.transform.opacity = doc.baseOpacity_;   // 底图整体透明度随 §7 generate 段往返（内存声明恒存 255，此处回填真值到序列化副本）
        base.preprocess = doc.baseSession_.steps();  // 底图色彩预处理随 generate 段 preprocess 键往返（仅色彩步，baseToJson 再次过滤）
        cfg.base = std::move(base);
    }
    if (doc.layersActive_) {
        // 叠加图层声明态入配置（位图不入）：逐层用其独立会话的有效管线覆写 layer.preprocess，剥离 decodedPixels。
        // 底图不入 layers（已在 generate 段 / 或为外部文件）；source 取画布尺寸——还原端先以底图作 base composeCanvas 得画布图、再重放 cfg.preprocess 复现工作图。
        std::vector<canvas::ImageLayer> out = doc.layers_;
        for (auto& layer : out) {
            if (const auto* sess = doc.layerSessions_.find(layer.id))
                layer.preprocess = sess->preprocess.effectivePipeline();
            layer.decodedPixels = core::Image{};   // 位图不入快照 / 配置（File 层仅留 fileName 相对名）
        }
        cfg.layers = std::move(out);
        cfg.source.width = doc.canvasWidth_;
        cfg.source.height = doc.canvasHeight_;
    } else {
        cfg.source.width = doc.original().width();
        cfg.source.height = doc.original().height();
    }
    return cfg;
}

// 反向映射（配置加载与撤销重做共用）：把 EngineConfig 搬回 Document 状态，与 buildEngineConfig 互逆。
// 直接改私有字段（不走各 setter）后只发一次 changed()，避免逐字段多次触发刷新；图像/source 尺寸不还原。
void ConfigCodec::applyEngineConfig(Document& doc, const engine::EngineConfig& cfg, const bool preserveBase) {
    // ---- 模式与极性 ----
    doc.mode_ = cfg.cut.tier;
    doc.polarity_ = cfg.cut.polarity;

    // ---- 生成器 + tier → L1 形状 / L2 子功能（逆向 buildCutConfig 的映射）----
    // L3 恒为 Grid、无子选项；未知生成器回退到各模式默认值，保证枚举有效。
    switch (cfg.cut.tier) {
        case engine::Tier::Standard:
            switch (cfg.cut.generator) {
                case engine::CutGenerator::HorizontalLine: doc.l1Shape_ = L1Shape::Hband; break;
                case engine::CutGenerator::VerticalLine:   doc.l1Shape_ = L1Shape::Vband; break;
                default:                                        doc.l1Shape_ = L1Shape::Rect;  break;
            }
            break;
        case engine::Tier::Erase:
            switch (cfg.cut.generator) {
                case engine::CutGenerator::HorizontalLine: doc.l2Sub_ = L2Sub::Hline;     break;
                case engine::CutGenerator::VerticalLine:   doc.l2Sub_ = L2Sub::Vline;     break;
                case engine::CutGenerator::MultiRect:      doc.l2Sub_ = L2Sub::MultiRect; break;
                default:                                        doc.l2Sub_ = L2Sub::Cross;     break;
            }
            break;
        case engine::Tier::Grid:
            break; // 网格模式无 L1/L2 子选项。
    }

    // ---- 切割几何 ----
    doc.rect_ = cfg.cut.rect;
    doc.hasRect_ = doc.rect_.width() > 0 && doc.rect_.height() > 0; // 退化矩形视为无选区（与 setRect 校验一致）。
    doc.rects_ = cfg.cut.rects;
    doc.grid_ = cfg.cut.grid;
    // 网格单元尺寸必须为正（Core Grid::build 要求，E-5）；非法配置回退默认 100×100，避免产不出单元。
    if (doc.grid_.cellWidth <= 0) doc.grid_.cellWidth = 100;
    if (doc.grid_.cellHeight <= 0) doc.grid_.cellHeight = 100;

    // ---- 选择集与排序 ----
    doc.selectedCells_ = cfg.selectedCells;
    doc.order_ = cfg.order;

    // ---- 导出参数 ----
    doc.exportMode_ = cfg.exportParams.mode;
    doc.layout_ = cfg.exportParams.layout;
    doc.format_ = cfg.exportParams.format;
    doc.quality_ = cfg.exportParams.quality;
    doc.naming_ = QString::fromStdString(cfg.exportParams.naming);
    doc.padColor_ = cfg.exportParams.padColor;
    doc.mergeOrder_ = cfg.exportParams.mergeOrder;
    // optional 画布/单元尺寸：nullopt（未指定）→ 0（交 Core 自动推导），与 buildEngineConfig 的「0 视为未指定」互逆。
    doc.mergeCols_ = cfg.exportParams.cols.value_or(0);
    doc.mergeRows_ = cfg.exportParams.rows.value_or(0);
    doc.mergeCellW_ = cfg.exportParams.cellWidth.value_or(0);
    doc.mergeCellH_ = cfg.exportParams.cellHeight.value_or(0);

    // ---- 维持模型不变式：合并重排为 L3 专属（Core runEngine 硬约束）----
    // 载入/还原出「非 L3 + 重排」的非法组合时复位为坍缩，与 setMode 的守卫一致，避免引擎拿到非法配置。
    if (doc.mode_ != engine::Tier::Grid && doc.layout_ == engine::MergeLayout::Rearrange)
        doc.layout_ = engine::MergeLayout::Collapse;

    // ---- 多层画布声明还原（与 buildSerializableConfig 互逆；layers 入快照使图层可撤销）----
    // 图层声明（transform/z/逐层 preprocess/尺寸）在此还原：位图不入快照，按 id 合并回现有内存像素供合成。
    // 画布级 preprocess 仍交由 PreprocessController.applyLoadedQueue 在配置载入时单独重放（不入文档快照）。
    // preserveBase=true（当前已有打开图）：底图像素与声明一律不动（不回填 baseLayer_/baseOpacity_/baseSession_、不作废 base_ 缓存），
    //   使导入配置只改切割/选择/排序/导出/图层参数而绝不覆盖底图；画布级 opacity 属独立顶层设置、仍照常还原。
    if (!preserveBase) {
        // 底图配方（§7 generate 段）回装为声明态（像素重生由调用方 onLoadConfig 专步回灌 setBaseImage，不走快照）。
        doc.baseLayer_ = cfg.base;
        // 底图整体透明度：§7 generate 段 transform.opacity 载入即回填为运行态 baseOpacity_，并把内存声明归一 255——
        //   透明度恒在 fadedCanvasBase 边界施加，避免与 composeBase 双淡（撤销/重做快照回退亦经此对称还原）。
        doc.baseOpacity_ = doc.baseLayer_.has_value() ? static_cast<std::uint8_t>(std::clamp(doc.baseLayer_->transform.opacity, 0, 255)) : 255;
        if (doc.baseLayer_.has_value()) doc.baseLayer_->transform.opacity = 255;
        // 底图色彩预处理：从 generate 段 base->preprocess 取色彩步入 baseSession_（内存 doc.baseLayer_->preprocess 置空，
        //   色彩预处理恒由 fadedCanvasBase 边界施加、不烘焙 baseImage_，杜绝与 composeBase 双应用）。底图选中态复位（快照不含运行态选中）。
        doc.baseSelected_ = false;
        doc.baseSession_.clear();
        if (doc.baseLayer_.has_value()) {
            preprocess::PreprocessPipeline colorSteps;
            for (const auto& step : doc.baseLayer_->preprocess.items())
                if (preprocess::isColorStep(step)) colorSteps.add(step);
            doc.baseSession_.setSteps(std::move(colorSteps));
            doc.baseLayer_->preprocess.clear();   // 内存声明剥离 preprocess：重生底图时 composeBase 不施加（由 fadedCanvasBase 承担）
        }
        // base_（resize 前缀的物化重放缓存）依赖底图源（baseSession_ 色彩步 + baseOpacity_）：
        //   本函数直接改写二者却不经 setImage，不会自动作废 base_。若遗留旧 base_，随后任一带 resize 前缀的
        //   commitPipeline 会因 baseDirtyFor 只比 resize 前缀而 baseDirty=false → 复用旧基准 → 像素错（撤销/重做/回放场景）。
        doc.preprocess_.clearBase();
    }
    // 画布级整体透明度：§7 顶层 opacity 键还原（缺省已由 engineConfigFromJson 夹为 255）。与底图独立，恒还原。
    doc.canvasOpacity_ = static_cast<std::uint8_t>(std::clamp(cfg.canvasOpacity, 0, 255));
    bool layersRestored = false;
    if (cfg.layers.has_value()) {
        doc.layersActive_ = true;
        // 画布宽高与底图同属 preserveBase 保护范围：当前已有打开图时导入配置不改画布尺寸，
        //   沿用既有 canvasWidth_/canvasHeight_（＝打开图尺寸），令叠加层落在当前底图/画布上；
        //   仅空文档（!preserveBase）才据配置 source 尺寸设定画布框（与底图重生同口径）。
        if (!preserveBase) {
            doc.canvasWidth_ = cfg.source.width;
            doc.canvasHeight_ = cfg.source.height;
        }
        doc.imageLayersVisible_ = true;   // 显隐不入配置（纯运行态）：重载/快照回退默认可见。
        std::vector<canvas::ImageLayer> restored = *cfg.layers;
        // 配置/快照重载：旧会话与物化缓存整体作废，按新层集重建每层声明态会话（位图随后由控制器按需重烤）。
        doc.layerSessions_.clear();
        for (auto& layer : restored) {
            // 合并回旧内存位图（File 层）：新配置/快照位图为空，但既有同名层可能已解码——保其像素以免重组降级。
            if (layer.decodedPixels.empty()) {
                if (const auto* old = doc.layerById(layer.id); old && !old->decodedPixels.empty())
                    layer.decodedPixels = old->decodedPixels;
            }
            doc.layerSessions_.ensure(layer.id).preprocess.setSteps(layer.preprocess);   // 逐层 preprocess 映到会话 steps
            if (layer.id >= doc.nextLayerId_) doc.nextLayerId_ = layer.id + 1;
        }
        doc.layers_ = std::move(restored);   // 图片图层数组整体替换（显隐不随配置还原，已在上方置回默认可见）。
        // 载入 zOrder 视作权威（含乱序/带间隙的旧配置）→ 稳定重排 + 重编号，重建「向量序≡zOrder 下标」不变式。
        std::ranges::stable_sort(doc.layers_, [](const canvas::ImageLayer& a, const canvas::ImageLayer& b) {
            return a.zOrder < b.zOrder;
        });
        for (std::size_t k = 0; k < doc.layers_.size(); ++k) doc.layers_[k].zOrder = static_cast<int>(k);
        if (doc.selectedLayerId_ && doc.layerById(*doc.selectedLayerId_) == nullptr) doc.selectedLayerId_.reset();
        layersRestored = true;
    } else {
        // 有底图即图层画布——配置无 layers 仅表示零叠加层：clearLayers 仅清叠加层、保留 layersActive_/baseImage_；
        //   结构变→下方 layersChanged→recompose 以「仅底图」重出 original_/working_（免撤销到无叠加层态时残留旧叠层）。
        doc.clearLayers();
        layersRestored = doc.layersActive_;   // 图层画布态需重组；空文档（base 缺省）交 changed 刷新即可。
    }

    emit doc.changed();
    // 图层声明变了→需重组展平画布：发 layersChanged 交 PreprocessController 走 composeCanvas 重算 doc.original_/working_。
    if (layersRestored) emit doc.layersChanged();
}


} // namespace idc::gui
