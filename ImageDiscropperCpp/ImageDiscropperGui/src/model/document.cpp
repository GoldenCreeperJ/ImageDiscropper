// ============================================================================
// 文件：model/document.cpp
// 作用：实现 Document 的状态存取与 EngineConfig 组装（见同名头文件说明）。
// 分块依据：setter 一律「改值 → 发对应信号」，把刷新时机交给 MainWindow；
//           buildCutConfig / buildEngineConfig 只做字段搬运与枚举映射（无引擎逻辑）。
// ============================================================================
#include "model/document.h"

#include <algorithm>
#include <utility>
#include <variant>

namespace idc::gui {

// 构造：无图像、默认 L1 + KEEP。
Document::Document(QObject* parent) : QObject(parent) {}

// 载入新图像：置原图与工作图，清空队列与选区。
void Document::setImage(core::Image img, QString path) {
    original_ = img;             // 保留一份原始副本（供队列重放与「重置预处理」）。
    working_ = std::move(img);   // 新图初始无预处理，工作图即原图。
    preprocess_.clear();         // 换图：轻队列 / 独立 resize / base_ 缓存随原图一并失效。
    // 载入单一图像＝非图层画布态：清图层数组 / 每层会话 / 选中 / 尺寸，回退到 original_→预处理→working_ 既有链路。
    layers_.clear();
    imageLayersVisible_ = true;
    layersActive_ = false;
    canvasWidth_ = 0;
    canvasHeight_ = 0;
    layerSessions_.clear();
    selectedLayerId_.reset();
    nextLayerId_ = 1;
    baseImage_ = core::Image{};   // 载入文件图＝文件底（canvasBase() 走 original_），清残留底图像素与声明。
    baseLayer_.reset();
    baseOpacity_ = 255;           // 单图态无底图淡出概念，复位不透明（与 canvasBase() 走 original_ 同口径）。
    baseSelected_ = false;        // 换图回单图态：底图选中态与色彩预处理会话、画布级透明度一并复位。
    baseSession_.clear();
    canvasOpacity_ = 255;
    imagePath_ = std::move(path);
    hasRect_ = false;
    rect_ = engine::RectRegion{};
    emit imageChanged();
}

// 新建语义（**非破坏性**）：以新扁平底图替换画布基座、保留图层栈与色彩预处理（详见头文件）。
//   与 setImage 的关键差异：不清图层/不取消标注会话、仅剥除会改变尺寸的 resize+几何预处理，
//   从而“新建同尺寸画布”不会清空图层/标注/预处理。标注捕获回填与单图态色彩预处理重放均由调用方（MainWindow）掌控。
void Document::resetBaseWith(core::Image flat, std::optional<canvas::ImageLayer> baseLayer) {
    const int newW = flat.width();
    const int newH = flat.height();
    const int curW = layersActive_ ? canvasWidth_ : original_.width();
    const int curH = layersActive_ ? canvasHeight_ : original_.height();
    const bool sizeChanged = newW != curW || newH != curH;

    imagePath_.clear();               // 新建画布无文件来源。
    baseLayer_ = std::move(baseLayer);  // 底图声明（同构 ImageLayer：程序图案或附图片+落位），供重组/序列化重生。
    // 新底图声明携带的 transform.opacity 回填为运行态 baseOpacity_、内存声明归一 255（透明度只走 fadedCanvasBase 边界）。
    //   新建对话框产的 base transform.opacity 恒为 255 → baseOpacity_ 随之复位为不透明。
    baseOpacity_ = baseLayer_.has_value() ? static_cast<std::uint8_t>(std::clamp(baseLayer_->transform.opacity, 0, 255)) : 255;
    if (baseLayer_.has_value()) baseLayer_->transform.opacity = 255;
    baseSelected_ = false;   // 新建底图：清底图选中态与色彩预处理会话（画布透明度属保留态，此处不改）。
    baseSession_.clear();

    // 预处理：仅保留色彩步（Gray/Split/Invert），丢弃几何步（Rotate/Flip）与独立 resize，失效 base_ 缓存——
    //   防重放改变工作图尺寸（保留的几何/resize 会把新底图重新缩放/旋转、令图层/标注坐标失去意义）。
    preprocess::PreprocessPipeline kept;
    for (const auto& step : preprocess_.steps())
        if (preprocess::isColorStep(step)) kept.add(step);
    preprocess_.setSteps(std::move(kept));
    preprocess_.setResize(std::nullopt);
    preprocess_.clearBase();

    if (layersActive_) {
        baseImage_ = std::move(flat);   // 新底图入基座；layers_ / 每层会话原样保留，交下方 layersChanged 重组。
        canvasWidth_ = newW;
        canvasHeight_ = newH;
    } else {
        original_ = flat.clone();
        working_ = std::move(flat);      // 先置为无预处理工作图；调用方随后经 replayCanvasPreprocess 补算色彩预处理。
    }

    if (sizeChanged) {                  // 尺寸变→旧框选坐标失效；同尺寸→保留框选（决策：仅尺寸不变时留框选）。
        hasRect_ = false;
        rect_ = engine::RectRegion{};
        rects_.clear();
        selectedCells_.clear();
    }

    // 图层态：发 layersChanged → recomposeLayerCanvas 以新 baseImage_ 展平 + 重放色彩预处理 → setWorkingImage（内含 imageChanged）。
    // 单图态：发 imageChanged 刷新预览（工作图补算交调用方重放）。
    if (layersActive_) emit layersChanged();
    else emit imageChanged();
}

// 写回一次预处理物化结果：替换工作图（original_ / steps_ 不变）。
// 空图拒绝，避免画布/引擎拿到无效工作图。触发 imageChanged()（需重建预览底图）。
void Document::setWorkingImage(core::Image img) {
    if (img.empty()) return;
    working_ = std::move(img);
    emit imageChanged();
}

// 重组后回灌展平画布底图为 original_（多层画布专用）：保留画布级预处理声明、失效其 base_ 缓存；
// 空图拒绝。不碰 working_、不发信号（随后由控制器走 commitPipeline 重放产 working_）。
void Document::setOriginalImage(core::Image composedCanvas) {
    if (composedCanvas.empty()) return;
    original_ = std::move(composedCanvas);
    preprocess_.clearBase();   // 基准已变：旧 base_=resize(旧 original) 失效，下次提交重烤。
}

// 重置预处理：清空独立 resize 与轻操作队列、工作图恢复为原图（original_ 始终保留）；未预处理时无需还原。
void Document::resetPreprocess() {
    if (!preprocess_.hasPreprocess()) return;
    preprocess_.clear();         // 清空轻队列 / 独立 resize / base_ 缓存（会话失效）。
    working_ = original_;
    emit imageChanged();
}

// 覆盖 / 清除独立 resize（仅存声明状态；物化与刷新由调用方经 setWorkingImage 掌控）。
void Document::setResize(const std::optional<ResizeTarget> target) {
    preprocess_.setResize(target);
}

// 覆盖整条声明式预处理队列（仅存状态；物化与刷新由调用方掌控）。
void Document::setSteps(preprocess::PreprocessPipeline pipeline) {
    preprocess_.setSteps(std::move(pipeline));
}

// 记录一段前缀的重放产物为 base_（供 commitPipeline 免重复重采样复用）。
void Document::setBase(core::Image img, preprocess::PreprocessPipeline prefix) {
    preprocess_.setBase(std::move(img), std::move(prefix));
}

// 几何签名：对原图尺寸跑「有效管线」（[resize?] + 轻操作队列）的纯元数据推演（不碰像素），得 {输出宽高, 净旋转象限}。
preprocess::PreprocessPipeline::GeometryOut Document::geomSig() const {
    return preprocess_.geomSig(original_.width(), original_.height());
}

// 队列灰度推演：任一 GrayOp 使输出恒为灰度（下游 invert/rotate/flip/resize/split 均不恢复彩色），
// 供启停智能在灰度后停用「黑白」「色道分离」（纯元数据扫描，不碰像素）。
bool Document::preprocessProducesGray() const {
    return preprocess_.producesGray();
}

// ============================ 多层画布（Phase 3-4）============================
// 说明：Document 只维护图层「声明态」（栈/尺寸/选中/每层会话），绝不跑像素、不读盘——
// composeCanvas（物化）与 File 层解码均由 PreprocessController / MainWindow 经 EngineBridge 承担
//（CONTRIBUTING.md「分层纪律」）。CRUD 仅改声明态后发 layersChanged()，控制器接此信号重组。

// 设当前拾取的图片图层：切换预处理当前目标（图层↔底图/画布）；发 changed 驱动面板/状态栏刷新。
//   选中某层时与底图选中互斥（清 baseSelected_）；传 nullopt（取消层选中）不强改底图选中态（由 selectBase 独立决定）。
void Document::selectLayer(const std::optional<int> id) {
    const std::optional<int> next = id.has_value() && layerById(*id) != nullptr ? id : std::nullopt;
    if (const bool needClearBase = next.has_value() && baseSelected_; selectedLayerId_ == next && !needClearBase) return;
    selectedLayerId_ = next;
    if (next.has_value()) baseSelected_ = false;   // 选层与选底图互斥
    emit changed();
}

// 设底图（画布基座）选中态：与 selectLayer 互斥（选底图清选中层）；发 changed 切换预处理当前目标。
void Document::selectBase(const bool on) {
    if (baseSelected_ == on) return;
    baseSelected_ = on;
    if (on) selectedLayerId_.reset();   // 选底图与选层互斥
    emit changed();
}

const canvas::ImageLayer* Document::layerById(const int id) const {
    for (const auto& l : layers_)
        if (l.id == id) return &l;
    return nullptr;
}

// 当前目标会话三元路由：选中图层→其 layerSessions_ 条目（缺失则按底图/画布回落）；选中底图→baseSession_；未选中→画布 preprocess_。
PreprocessSession& Document::activeSession() {
    if (selectedLayerId_.has_value()) {
        if (auto* sess = layerSessions_.find(*selectedLayerId_))
            return sess->preprocess;
    }
    if (baseSelected_) return baseSession_;
    return preprocess_;
}
const PreprocessSession& Document::activeSession() const {
    return const_cast<Document*>(this)->activeSession();
}

// 起图层画布：置 layersActive_、设尺寸、留存底图 base、清空旧叠加层与会话、复位选中与 id 发号器。
void Document::beginLayerCanvas(const int width, const int height, core::Image base) {
    layersActive_ = true;
    canvasWidth_ = width;
    canvasHeight_ = height;
    layers_.clear();      // 只留叠加层；底图不入 layers（base 存于 baseImage_）。
    layerSessions_.clear();
    selectedLayerId_.reset();
    imageLayersVisible_ = true;   // 新画布默认图片图层整体可见。
    nextLayerId_ = 1;
    baseImage_ = std::move(base);        // 画布底图（input 生态位）；空＝仅透明底。
}

// 追加图层：分配递增稳定 id、zOrder 置顶（高于现有最大）、建其独立会话；发 layersChanged。
int Document::addImageLayer(canvas::ImageLayer layer) {
    layer.id = nextLayerId_++;
    int maxZ = 0;
    for (const auto& l : layers_) maxZ = std::max(maxZ, l.zOrder);
    layer.zOrder = layers_.empty() ? 0 : maxZ + 1;
    const int id = layer.id;
    layerSessions_.ensure(id);   // 该层独立声明态会话（含 base 槽 + 物化签名槽）
    layers_.push_back(std::move(layer));
    emit layersChanged();
    return id;
}

// 删除某层（连带回收其会话；若正被选中则取消选中）。
bool Document::removeLayer(const int id) {
    const auto it = std::find_if(layers_.begin(), layers_.end(),
                                 [id](const canvas::ImageLayer& l) { return l.id == id; });
    if (it == layers_.end()) return false;
    layers_.erase(it);
    layerSessions_.remove(id);   // 连带回收其会话与物化位图
    if (selectedLayerId_ == id) selectedLayerId_.reset();
    emit layersChanged();
    return true;
}

// 上/下移一层：与相邻项交换 zOrder（存序不敏感、合成恒按 zOrder 升序）。up＝向顶层（更大 zOrder）。
bool Document::moveLayerZOrder(const int id, const bool up) {
    std::sort(layers_.begin(), layers_.end(),
              [](const canvas::ImageLayer& a, const canvas::ImageLayer& b) { return a.zOrder < b.zOrder; });
    const auto it = std::find_if(layers_.begin(), layers_.end(),
                                 [id](const canvas::ImageLayer& l) { return l.id == id; });
    if (it == layers_.end()) return false;
    const auto i = static_cast<std::size_t>(it - layers_.begin());
    const auto n = layers_.size();
    const std::size_t j = up ? i + 1 : i > 0 ? i - 1 : i;   // up→邻 i+1（更高）；down→邻 i−1（更低）
    if (j == i || j >= n) return false;                        // 已在顶/底，不动
    std::swap(layers_[i].zOrder, layers_[j].zOrder);
    emit layersChanged();
    return true;
}

// 移至顶层 / 底层：把选中层 zOrder 设为当前最大 +1（顶）或最小 −1（底）。合成恒按 zOrder 升序稳定，
// 故单值推离即整体置顶/置底（无需重排其余层）。已在边缘（zOrder 已等极值）则短路不动。
bool Document::moveLayerToEdge(const int id, const bool toTop) {
    const auto it = std::find_if(layers_.begin(), layers_.end(),
                                 [id](const canvas::ImageLayer& l) { return l.id == id; });
    if (it == layers_.end() || layers_.size() < 2) return false;
    int lo = it->zOrder;
    int hi = it->zOrder;
    for (const canvas::ImageLayer& l : layers_) { lo = std::min(lo, l.zOrder); hi = std::max(hi, l.zOrder); }
    if (toTop) {
        if (it->zOrder >= hi) return false;   // 已在顶层（zOrder 已为最大）
        it->zOrder = hi + 1;
    } else {
        if (it->zOrder <= lo) return false;   // 已在底层（zOrder 已为最小）
        it->zOrder = lo - 1;
    }
    emit layersChanged();
    return true;
}

// 就地替换某层（transform / z / preprocess 编辑）：强制保持原 id 稳定（id 为会话/拾取键）。
bool Document::updateLayer(const int id, const canvas::ImageLayer& layer) {
    const auto it = std::find_if(layers_.begin(), layers_.end(),
                                 [id](const canvas::ImageLayer& l) { return l.id == id; });
    if (it == layers_.end()) return false;
    canvas::ImageLayer nl = layer;
    nl.id = id;
    *it = std::move(nl);
    emit layersChanged();
    return true;
}

// 图片图层整体显隐：一个开关直接落到运行态标志 imageLayersVisible_（不入配置；无逐层 visible）。
// 值未变则短路；否则发 layersChanged 交控制器重组（隐藏时传空 layers，仅留底图）。
void Document::setImageLayersVisible(const bool visible) {
    if (imageLayersVisible_ == visible) return;
    imageLayersVisible_ = visible;
    emit layersChanged();
}

// 回退到无图层（单一载入图态）：清栈、清会话、取消选中、清底图像素（不动 original_/working_）。
void Document::clearLayers() {
    if (!layersActive_) return;
    layersActive_ = false;
    layers_.clear();
    layerSessions_.clear();
    selectedLayerId_.reset();
    baseSelected_ = false;   // 回单图态：底图选中态与色彩预处理会话失效（单图态无 Base 目标）。
    baseSession_.clear();
    baseImage_ = core::Image{};   // 回单图态：canvasBase() 转走 original_，清掉独立底图像素。
}

// 清空全部图片图层并回退为单图底图态：先把 original_/working_ 还原为纯底图 baseImage_，再走 clearLayers 清栈。
//   保留画布级预处理声明与标注（调用方随后重放色彩预处理）；发 imageChanged 刷新预览。仅图层态有效。
bool Document::clearAllLayersToBase() {
    if (!layersActive_) return false;
    const bool hadBase = !baseImage_.empty();
    if (hadBase) {
        original_ = baseImage_.clone();
        working_ = baseImage_.clone();
    }
    clearLayers();   // layersActive_=false / layers_={} / 清会话 / baseImage_={}；此后 canvasBase() 走 original_（已还原为底图）。
    emit imageChanged();
    return hadBase;
}

// 声明画布尺寸：更新尺寸 + 仅清框选（矩形/多矩形/单元），保留图层与标注（决策 3）；发 layersChanged 交控制器重组。
void Document::setCanvasSize(const int width, const int height) {
    if (width <= 0 || height <= 0) return;
    if (canvasWidth_ == width && canvasHeight_ == height) return;
    canvasWidth_ = width;
    canvasHeight_ = height;
    hasRect_ = false;
    rect_ = engine::RectRegion{};
    rects_.clear();
    selectedCells_.clear();
    emit layersChanged();
}

// 确保图层画布态：无图层但有单张载入图时，把当前原图纳为**画布底图**（input 生态位，不入 layers、无 transform）。
// 底图声明（若原图为程序图案 / 附图片底）保留在 baseLayer_，序列化时进 §7 generate 段。不动画布级预处理会话（preprocess_）——
// 重组后 composed==original_（空叠加层），工作图经 commitPipeline 重放保持不变。
bool Document::ensureLayerCanvas() {
    if (layersActive_) return true;
    if (original_.empty()) return false;
    const int w = original_.width();
    const int h = original_.height();
    if (w <= 0 || h <= 0) return false;
    // 以原图为底图（base）进入图层编辑态（画布无级背景）；不往 layers 塞底图层（故底图不可拖 / 无 transform / 不失配方）。
    beginLayerCanvas(w, h, original_.clone());
    return true;
}

// 设底图整体不透明度（0..255）：与图层自身 transform.opacity 正交——它是「画布基座」的整体淡出。
//   值未变早退；否则置值后发 layersChanged（交控制器以 fadedCanvasBase 重组预览，仅乘 alpha、不重算图案）
//   + changed()（刷新面板 / 状态栏）。透明度恒在 fadedCanvasBase 边界施加，不烘焙进 baseImage_。
void Document::setBaseOpacity(const int opacity) {
    const int v = std::clamp(opacity, 0, 255);
    if (static_cast<int>(baseOpacity_) == v) return;
    baseOpacity_ = static_cast<std::uint8_t>(v);
    emit layersChanged();
    emit changed();
}

// 按 baseOpacity_ 淡出的画布底图副本：clone canvasBase() 后 canvas::applyAlpha。空底图（无 base）返回空图由
//   composeCanvas 自然处理。仅图层态 recompose / 导出重算取用（canvasBase() 本身保持原样供几何等用途）。
core::Image Document::fadedCanvasBase() const {
    core::Image img = canvasBase().clone();
    // 底图色彩预处理（仅色彩步、resize 恒空）在合成边界施加——baseImage_ 保持未处理原始像素，杜绝与 composeBase 双应用。
    if (!baseSession_.steps().empty())
        img = baseSession_.effectivePipeline().applyExec(img);
    canvas::applyAlpha(img, baseOpacity_);   // 底图整体淡出，同一边界仅施加一次
    return img;
}

// 设画布级整体不透明度（0..255）：三元目标 Canvas 的透明度，与图层 transform.opacity、底图 baseOpacity 正交。
//   值未变早退；否则置值后发 changed()（刷新面板 / 历史采集）。重算 working_ 交调用方（MainWindow）随后走
//   replayCanvasPreprocess——透明度恒在产出边界（commitPipeline）逐像素施加一次、不烘焙缓存位图。
void Document::setCanvasOpacity(const int opacity) {
    const int v = std::clamp(opacity, 0, 255);
    if (static_cast<int>(canvasOpacity_) == v) return;
    canvasOpacity_ = static_cast<std::uint8_t>(v);
    emit changed();
}

// 更新底图色彩预处理声明态（Base 目标）：仅保留色彩步（几何/resize 会改画布尺寸/坐标基准，一律丢弃），
//   写入 baseSession_（resize_ 恒 nullopt）；内容未变早退返回 false，否则发 layersChanged 交控制器以
//   fadedCanvasBase 重组预览（色彩预处理边界施加、不烘焙进 baseImage_）。返回是否发生变更。
bool Document::updateBasePreprocess(const preprocess::PreprocessPipeline& colorSteps) {
    preprocess::PreprocessPipeline filtered;
    for (const auto& step : colorSteps.items())
        if (preprocess::isColorStep(step)) filtered.add(step);
    if (baseSession_.steps() == filtered) return false;
    baseSession_.setSteps(std::move(filtered));
    baseSession_.setResize(std::nullopt);   // 底图预处理恒不含 resize
    emit layersChanged();
    return true;
}


// 切换模式：L1/L2 由极性定义（L1≡keep、L2≡remove），切到二者时套用对应极性；
// L3（网格）极性独立于模式，切入 L3 时**保留当前极性不变**（从 L2 进 L3 应沿用 remove）。发变更信号。
void Document::setMode(const engine::Tier t) {
    if (mode_ == t) return;
    mode_ = t;
    // L1≡保留框内(keep)、L2≡删除框内(remove)：切到 L1/L2 时套用其定义极性；
    // 切到 L3 不动极性——L3 极性独立，用户从 L2(remove) 进 L3 时期望极性保持 remove。
    if (t == engine::Tier::L1)
        polarity_ = engine::Polarity::KEEP;
    else if (t == engine::Tier::L2)
        polarity_ = engine::Polarity::REMOVE;
    // 合并重排为 L3 专属（Core runEngine 硬约束）：离开 L3 时若仍是重排，复位为坍缩，
    // 在模型层维持不变式，避免导出面板/引擎拿到非法的「非 L3 + 重排」组合。
    if (t != engine::Tier::L3 && layout_ == engine::MergeLayout::REARRANGE)
        layout_ = engine::MergeLayout::COLLAPSE;
    emit changed();
}

// 设置极性（keep/remove），实时驱动遮罩刷新（NFR-6）。
// 语义耦合：L1（标准提取）＝保留框内(keep)、L2（反向剔除）＝删除框内(remove)，二者本质是
// 同一交互的两种极性表述，故在 L1/L2 下切换极性会同步切换模式（keep→L1、remove→L2）；
// L3（网格）的极性独立于模式，切换极性不改变 L3。
void Document::setPolarity(const engine::Polarity p) {
    if (polarity_ == p) return;
    polarity_ = p;
    // L1/L2 与极性一一对应：切换极性即切换模式；L3 极性独立，模式保持不变。
    if (mode_ == engine::Tier::L1 || mode_ == engine::Tier::L2) {
        mode_ = p == engine::Polarity::REMOVE ? engine::Tier::L2
                                                     : engine::Tier::L1;
    }
    emit changed();
}

// 设置 L1 形状。
void Document::setL1Shape(const L1Shape s) {
    if (l1Shape_ == s) return;
    l1Shape_ = s;
    emit changed();
}

// 设置 L2 子功能。
void Document::setL2Sub(const L2Sub s) {
    if (l2Sub_ == s) return;
    l2Sub_ = s;
    emit changed();
}

// 设置选区矩形（原图像素坐标），标记已有选区。
// 校验：拒绝退化矩形（宽或高 ≤ 0）——非法几何会让 Core 的切割/网格计算异常甚至崩溃，
// 故在此统一兜底（所有写回路径共用），退化输入时保持上一次有效选区不变（CONTRIBUTING.md「分层纪律」 输入校验）。
void Document::setRect(const engine::RectRegion& r) {
    if (r.width() <= 0 || r.height() <= 0) return;
    rect_ = r;
    hasRect_ = true;
    emit changed();
}

// 清除选区。
void Document::clearRect() {
    if (!hasRect_) return;
    hasRect_ = false;
    rect_ = engine::RectRegion{};
    emit changed();
}

// 追加一个多矩形（L2 MULTI_RECT）。校验同 setRect：拒绝退化矩形，避免 Core 切割/诱导网格异常。
void Document::addRect(const engine::RectRegion& r) {
    if (r.width() <= 0 || r.height() <= 0) return;
    rects_.push_back(r);
    emit changed();
}

// 修改指定下标的多矩形；越界或退化输入均忽略（保持原列表不变）。
void Document::updateRect(const std::size_t index, const engine::RectRegion& r) {
    if (index >= rects_.size()) return;
    if (r.width() <= 0 || r.height() <= 0) return;
    if (rects_[index] == r) return;
    rects_[index] = r;
    emit changed();
}

// 删除指定下标的多矩形；越界忽略。
void Document::removeRect(const std::size_t index) {
    if (index >= rects_.size()) return;
    rects_.erase(rects_.begin() + static_cast<std::ptrdiff_t>(index));
    emit changed();
}

// 清空多矩形列表。
void Document::clearRects() {
    if (rects_.empty()) return;
    rects_.clear();
    emit changed();
}

// 设置 L3 网格基准点（相位锚；切割线恒过该点）。
// 网格几何一变，按序号的选择集即失效（同一序号指向不同单元），故一并清空。
void Document::setGridOrigin(const int x, const int y) {
    if (grid_.originX == x && grid_.originY == y) return;
    grid_.originX = x;
    grid_.originY = y;
    selectedCells_.clear();
    emit changed();
}

// 设置 L3 单元尺寸；拒绝非正值（会让 Core Grid::build 产不出单元，E-5）。
void Document::setCellSize(const int w, const int h) {
    if (w <= 0 || h <= 0) return;
    if (grid_.cellWidth == w && grid_.cellHeight == h) return;
    grid_.cellWidth = w;
    grid_.cellHeight = h;
    selectedCells_.clear(); // 单元尺寸变→网格重铺，选择集序号失效。
    emit changed();
}

// 设置 L3 余量策略（discard / keep-partial / pad）。
void Document::setRemainder(const engine::RemainderPolicy p) {
    if (grid_.remainder == p) return;
    grid_.remainder = p;
    selectedCells_.clear(); // 余量策略变→边缘单元增删，选择集序号失效。
    emit changed();
}

// 回灌 Core 计算出的派生行列数（仅供面板只读显示，不触发 changed 以免回环）。
void Document::setDerivedGridSize(const int rows, const int cols) {
    gridRows_ = rows;
    gridCols_ = cols;
}

// 「转为网格模式编辑」：把当前矩形选区一键送入 L3 继续精修。
// 语义：以选区左上为基准点、选区宽高为单元尺寸生成周期性网格——于是用户刚画的那个矩形
// 恰成为网格中的一个单元（网格线精确穿过矩形四边），可在 L3 里逐单元点选/排序精修。
// 说明：L3 网格是「基准点 + 单元尺寸」的周期铺满（SPEC §3.4.3），无法逐条复刻单矩形诱导的非均匀
// 3×3 线；此映射为纯字段搬运（不含几何计算，CONTRIBUTING.md「分层纪律」），保留矩形边界作为网格相位锚，视觉连续。
void Document::convertRectToGrid() {
    // 源矩形：优先用单选区 rect_；若无有效单选区但多矩形列表非空（MULTI_RECT），
    // 则取全部矩形的包围盒（min 左上 / max 右下）作为网格单元基准（纯字段派生，非切割几何）。
    engine::RectRegion src = rect_;
    if ((!hasRect_ || src.width() <= 0 || src.height() <= 0) && !rects_.empty()) {
        int l = rects_.front().left, t = rects_.front().top;
        int r = rects_.front().right, b = rects_.front().bottom;
        for (const auto& rr : rects_) {
            l = std::min(l, rr.left);  t = std::min(t, rr.top);
            r = std::max(r, rr.right); b = std::max(b, rr.bottom);
        }
        src = engine::RectRegion(l, t, r, b);
    }
    // 无有效矩形（或退化）时不转换，避免产生非法单元尺寸（Core Grid::build 要求正尺寸）。
    if (src.width() <= 0 || src.height() <= 0) return;
    grid_.originX = src.left;
    grid_.originY = src.top;
    grid_.cellWidth = src.width();
    grid_.cellHeight = src.height();
    selectedCells_.clear();                        // 进入 L3 后由用户重新点选。
    mode_ = engine::Tier::L3;
    // L3 默认保留选中；空选择集在 Core 会推导为全选，故 keep 下保留全图（避免 remove+全选→剔除全部报 E-7）。
    polarity_ = engine::Polarity::KEEP;
    emit changed();
}

// 覆盖 L3 选择集（CUSTOM 排序下其顺序即自定义序列）。
void Document::setSelectedCells(std::vector<int> cells) {
    if (selectedCells_ == cells) return;
    selectedCells_ = std::move(cells);
    emit changed();
}

// 全选：依派生行列数生成 0..N-1 全序号（row-major）。
void Document::selectAllCells() {
    const int n = gridRows_ * gridCols_;
    if (n <= 0) return;
    std::vector<int> all;
    all.reserve(static_cast<std::size_t>(n));
    for (int i = 0; i < n; ++i) all.push_back(i);
    setSelectedCells(std::move(all));
}

// 反选：依派生行列数取当前选择集的补集（保持升序）。
void Document::invertCells() {
    const int n = gridRows_ * gridCols_;
    if (n <= 0) return;
    std::vector<char> on(static_cast<std::size_t>(n), 0);
    for (const int i : selectedCells_) {
        if (i >= 0 && i < n) on[static_cast<std::size_t>(i)] = 1;
    }
    std::vector<int> inv;
    inv.reserve(static_cast<std::size_t>(n));
    for (int i = 0; i < n; ++i) {
        if (!on[static_cast<std::size_t>(i)]) inv.push_back(i);
    }
    setSelectedCells(std::move(inv));
}

// 清空 L3 选择集。
void Document::clearCells() {
    if (selectedCells_.empty()) return;
    selectedCells_.clear();
    emit changed();
}

// 单击切换某单元：已选则移除、未选则追加到末尾（CUSTOM 下追加顺序即自定义序，FR-L3.5）。
void Document::toggleCell(const int index) {
    if (index < 0) return;
    if (const auto it = std::find(selectedCells_.begin(), selectedCells_.end(), index); it != selectedCells_.end()) {
        selectedCells_.erase(it);
    } else {
        selectedCells_.push_back(index);
    }
    emit changed();
}

// 并入框选命中的单元（去重，保持首次纳入顺序；CUSTOM 下即点选先后）。
void Document::addCells(const std::vector<int>& indices) {
    // 局部标志改名 modified：避免遮蔽 Document::changed() 信号（否则 emit changed() 会被当成调用该 bool → C2064）。
    bool modified = false;
    for (const int index : indices) {
        if (index < 0) continue;
        if (std::find(selectedCells_.begin(), selectedCells_.end(), index) == selectedCells_.end()) {
            selectedCells_.push_back(index);
            modified = true;
        }
    }
    if (modified) emit changed();
}

// CUSTOM 拖拽调序（FR-L3.5）：把 from 单元移到 to 单元当前所在的序位。
// selectedCells_ 的顺序即 Core 自定义输出序，故调序只是把 from 元素搬到 to 元素的原位次：
// 先记录 to 的位次（须在移除 from 之前，否则位次会漂移），移除 from 后在该位次插入 from。
void Document::moveCellOrder(const int from, const int to) {
    if (from == to || from < 0 || to < 0) return;
    int toPos = -1; // to 单元当前位次（0-based）。
    for (std::size_t i = 0; i < selectedCells_.size(); ++i) {
        if (selectedCells_[i] == to) { toPos = static_cast<int>(i); break; }
    }
    if (toPos < 0) return; // to 不在选择集中，无法定位调序目标。
    const auto itFrom = std::find(selectedCells_.begin(), selectedCells_.end(), from);
    if (itFrom == selectedCells_.end()) return; // from 未选中，无需调序。
    selectedCells_.erase(itFrom);
    // 移除 from 后 to 的位次可能前移一位；钳制到有效范围，使 from 落在 to 原来的位次。
    if (toPos > static_cast<int>(selectedCells_.size())) toPos = static_cast<int>(selectedCells_.size());
    selectedCells_.insert(selectedCells_.begin() + toPos, from);
    emit changed();
}

// 设置排序策略（row-major / column-major / custom）。
void Document::setSortStrategy(const engine::SortStrategy s) {
    if (order_.strategy == s) return;
    order_.strategy = s;
    emit changed();
}

// 设置整体逆序。
void Document::setSortReverse(const bool on) {
    if (order_.reverse == on) return;
    order_.reverse = on;
    emit changed();
}

// 设置蛇形排序（隔行/隔列反向）。
void Document::setSortSnake(const bool on) {
    if (order_.snake == on) return;
    order_.snake = on;
    emit changed();
}

// 设置导出模式与布局（分离 / 坍缩 / 重排）。
void Document::setEmit(const engine::EmitMode mode, const engine::MergeLayout layout) {
    if (emitMode_ == mode && layout_ == layout) return;
    emitMode_ = mode;
    layout_ = layout;
    emit changed();
}

// 设置导出格式。
void Document::setFormat(const engine::ExportFormat f) {
    if (format_ == f) return;
    format_ = f;
    emit changed();
}

// 设置有损格式质量。
void Document::setQuality(const int q) {
    if (quality_ == q) return;
    quality_ = q;
    emit changed();
}

// 设置分离导出命名模板。
void Document::setNaming(const QString& n) {
    if (naming_ == n) return;
    naming_ = n;
    emit changed();
}

// 设置分离导出目标目录。
void Document::setOutputDir(const QString& d) {
    outputDir_ = d; // 路径变更不影响预览，不发 changed()。
}

// 设置合并导出目标文件。
void Document::setOutputFile(const QString& f) {
    outputFile_ = f; // 同上，不发 changed()。
}

// 设置重排画布列/行数（FR-L3.7）：0=交 Core 依保留块数自动推导；负值钳为 0。
void Document::setMergeGrid(const int cols, const int rows) {
    const int c = cols < 0 ? 0 : cols;
    const int r = rows < 0 ? 0 : rows;
    if (mergeCols_ == c && mergeRows_ == r) return;
    mergeCols_ = c;
    mergeRows_ = r;
    emit changed();
}

// 设置重排单元尺寸：0=用保留块原尺寸；负值钳为 0。
void Document::setMergeCellSize(const int w, const int h) {
    const int ww = w < 0 ? 0 : w;
    const int hh = h < 0 ? 0 : h;
    if (mergeCellW_ == ww && mergeCellH_ == hh) return;
    mergeCellW_ = ww;
    mergeCellH_ = hh;
    emit changed();
}

// 设置重排空位/余量填充色（默认透明）。
void Document::setPadColor(const core::Color c) {
    if (padColor_ == c) return;
    padColor_ = c;
    emit changed();
}

// 设置重排填充策略（仅行/列优先；CUSTOM 无意义，Core compose 按行优先处理）。
void Document::setMergeOrderStrategy(const engine::SortStrategy s) {
    if (mergeOrder_.strategy == s) return;
    mergeOrder_.strategy = s;
    emit changed();
}

// 设置重排填充路径整体倒序。
void Document::setMergeOrderReverse(const bool on) {
    if (mergeOrder_.reverse == on) return;
    mergeOrder_.reverse = on;
    emit changed();
}

// 设置重排填充路径蛇形（隔行/隔列反向）。
void Document::setMergeOrderSnake(const bool on) {
    if (mergeOrder_.snake == on) return;
    mergeOrder_.snake = on;
    emit changed();
}

// 依当前模式/子选项构建切割配置（枚举映射，无几何计算）。
engine::CutConfig Document::buildCutConfig() const {
    engine::CutConfig c;
    c.tier = mode_;
    c.polarity = polarity_;
    c.rect = rect_;

    // 生成器映射：L1 形状 / L2 子功能 → Core CutGenerator；L3 → GRID。
    switch (mode_) {
        case engine::Tier::L1:
            switch (l1Shape_) {
                case L1Shape::RECT:  c.generator = engine::CutGenerator::RECT; break;
                case L1Shape::HBAND: c.generator = engine::CutGenerator::HORIZONTAL_LINE; break;
                case L1Shape::VBAND: c.generator = engine::CutGenerator::VERTICAL_LINE; break;
            }
            break;
        case engine::Tier::L2:
            switch (l2Sub_) {
                case L2Sub::CROSS: c.generator = engine::CutGenerator::RECT; break;
                case L2Sub::HLINE: c.generator = engine::CutGenerator::HORIZONTAL_LINE; break;
                case L2Sub::VLINE: c.generator = engine::CutGenerator::VERTICAL_LINE; break;
                case L2Sub::MULTI_RECT:
                    // 多矩形并集：搬运矩形列表，Core 对每个矩形诱导十字带后取并集（方案 A）。
                    c.generator = engine::CutGenerator::MULTI_RECT;
                    c.rects = rects_;
                    break;
            }
            break;
        case engine::Tier::L3:
            c.generator = engine::CutGenerator::GRID;
            c.grid = grid_;   // 网格参数（基准点 + 单元尺寸 + 余量策略）。
            break;
    }
    return c;
}

// 构建一次完整作业的配置（供 runEngine / exportImage 使用）。
engine::EngineConfig Document::buildEngineConfig() const {
    engine::EngineConfig cfg;

    // 源尺寸：以工作图实际尺寸为准（预处理可能改变尺寸，不能用原图尺寸）。
    cfg.source.width = width();
    cfg.source.height = height();

    // 切割配置（含模式/极性/几何）。
    cfg.cut = buildCutConfig();

    // 显式选择集：仅 L3 使用用户点选/全选的单元序号；L1/L2 必须恒空 → 由 Core 依生成器
    // 自动推导选择区（engine.cpp：「显式 selectedCells 优先，否则按生成器几何自动推导」）。
    // 若不加模式门控，从 L3 切回 L1/L2 时残留的 L3 单元序号会被套用到形状不同的诱导网格上，
    // 使保留集算错甚至为空、遮罩消失。故此处按模式门控，维持「L1/L2 selectedCells 恒空」不变式。
    cfg.selectedCells = mode_ == engine::Tier::L3 ? selectedCells_ : std::vector<int>{};

    // 排序策略（FR-L3.5）：row-major / column-major / reverse / snake / custom。
    cfg.order = order_;

    // 导出参数：把 GUI 侧字段搬运进 CompositionParams。
    cfg.emitParams.mode = emitMode_;
    cfg.emitParams.layout = layout_;
    cfg.emitParams.format = format_;
    cfg.emitParams.quality = quality_;
    cfg.emitParams.naming = naming_.toStdString();
    cfg.emitParams.padColor = padColor_;
    // 重排填充顺序（MergeOrder）：与选择排序 order_ 正交，仅 REARRANGE 时被 Core compose 使用。
    cfg.emitParams.mergeOrder = mergeOrder_;
    // 重排画布参数（FR-L3.7）：0 视为未指定 → 保持 nullopt 交 Core compose 自动推导；
    // 仅正值写入 optional，避免用 0 覆盖 Core 的默认布局（否则画布尺寸会被算成 0）。
    if (mergeCols_ > 0) cfg.emitParams.cols = mergeCols_;
    if (mergeRows_ > 0) cfg.emitParams.rows = mergeRows_;
    if (mergeCellW_ > 0) cfg.emitParams.cellWidth = mergeCellW_;
    if (mergeCellH_ > 0) cfg.emitParams.cellHeight = mergeCellH_;

    // preprocess 保持默认空流水线：预处理已即时作用于工作图（见 setWorkingImage），不在此重复。
    return cfg;
}

// 构建可序列化配置（保存 / 载入）：复用 buildEngineConfig 的切割/选择/排序/导出字段，
// 唯 preprocess 段填入「[resize?] + 轻操作队列」的完整有效管线（resize 恒置首位）、source 取原图尺寸
//——还原端从 original 重放整链复现 working_（旧配置载入时在控制器侧归一化抽出 resize）。
engine::EngineConfig Document::buildSerializableConfig() const {
    engine::EngineConfig cfg = buildEngineConfig();
    // 序列化携带画布级完整有效管线（[resize?] + 轻操作队列，resize 恒首位）——由画布会话统一推演。
    cfg.preprocess = preprocess_.effectivePipeline();
    // 画布级整体透明度随 §7 顶层 opacity 键往返（缺省 255）；与底图/图层透明度正交。
    cfg.canvasOpacity = canvasOpacity_;
    // 底图声明（同构 ImageLayer）随 §7 generate 段往返（可空则段省略）：还原端据 source 尺寸 + 本段重生画布底图。
    //   位图不入配置：剥离 decodedPixels（File 底凭 fileName 相对名引用 assets）。
    if (baseLayer_.has_value()) {
        canvas::ImageLayer base = *baseLayer_;
        base.decodedPixels = core::Image{};
        base.transform.opacity = baseOpacity_;   // 底图整体透明度随 §7 generate 段往返（内存声明恒存 255，此处回填真值到序列化副本）
        base.preprocess = baseSession_.steps();  // 底图色彩预处理随 generate 段 preprocess 键往返（仅色彩步，baseToJson 再次过滤）
        cfg.base = std::move(base);
    }
    if (layersActive_) {
        // 叠加图层声明态入配置（位图不入）：逐层用其独立会话的有效管线覆写 layer.preprocess，剥离 decodedPixels。
        // 底图不入 layers（已在 generate 段 / 或为外部文件）；source 取画布尺寸——还原端先以底图作 base composeCanvas 得画布图、再重放 cfg.preprocess 复现工作图。
        std::vector<canvas::ImageLayer> out = layers_;
        for (auto& layer : out) {
            if (const auto* sess = layerSessions_.find(layer.id))
                layer.preprocess = sess->preprocess.effectivePipeline();
            layer.decodedPixels = core::Image{};   // 位图不入快照 / 配置（File 层仅留 fileName 相对名）
        }
        cfg.layers = std::move(out);
        cfg.source.width = canvasWidth_;
        cfg.source.height = canvasHeight_;
    } else {
        cfg.source.width = original_.width();
        cfg.source.height = original_.height();
    }
    return cfg;
}

// 反向映射（配置加载与撤销重做共用）：把 EngineConfig 搬回 Document 状态，与 buildEngineConfig 互逆。
// 直接改私有字段（不走各 setter）后只发一次 changed()，避免逐字段多次触发刷新；图像/source 尺寸不还原。
void Document::applyEngineConfig(const engine::EngineConfig& cfg) {
    // ---- 模式与极性 ----
    mode_ = cfg.cut.tier;
    polarity_ = cfg.cut.polarity;

    // ---- 生成器 + tier → L1 形状 / L2 子功能（逆向 buildCutConfig 的映射）----
    // L3 恒为 GRID、无子选项；未知生成器回退到各模式默认值，保证枚举有效。
    switch (cfg.cut.tier) {
        case engine::Tier::L1:
            switch (cfg.cut.generator) {
                case engine::CutGenerator::HORIZONTAL_LINE: l1Shape_ = L1Shape::HBAND; break;
                case engine::CutGenerator::VERTICAL_LINE:   l1Shape_ = L1Shape::VBAND; break;
                default:                                         l1Shape_ = L1Shape::RECT;  break;
            }
            break;
        case engine::Tier::L2:
            switch (cfg.cut.generator) {
                case engine::CutGenerator::HORIZONTAL_LINE: l2Sub_ = L2Sub::HLINE;      break;
                case engine::CutGenerator::VERTICAL_LINE:   l2Sub_ = L2Sub::VLINE;      break;
                case engine::CutGenerator::MULTI_RECT:      l2Sub_ = L2Sub::MULTI_RECT; break;
                default:                                         l2Sub_ = L2Sub::CROSS;      break;
            }
            break;
        case engine::Tier::L3:
            break; // 网格模式无 L1/L2 子选项。
    }

    // ---- 切割几何 ----
    rect_ = cfg.cut.rect;
    hasRect_ = rect_.width() > 0 && rect_.height() > 0; // 退化矩形视为无选区（与 setRect 校验一致）。
    rects_ = cfg.cut.rects;
    grid_ = cfg.cut.grid;
    // 网格单元尺寸必须为正（Core Grid::build 要求，E-5）；非法配置回退默认 100×100，避免产不出单元。
    if (grid_.cellWidth <= 0) grid_.cellWidth = 100;
    if (grid_.cellHeight <= 0) grid_.cellHeight = 100;

    // ---- 选择集与排序 ----
    selectedCells_ = cfg.selectedCells;
    order_ = cfg.order;

    // ---- 导出参数 ----
    emitMode_ = cfg.emitParams.mode;
    layout_ = cfg.emitParams.layout;
    format_ = cfg.emitParams.format;
    quality_ = cfg.emitParams.quality;
    naming_ = QString::fromStdString(cfg.emitParams.naming);
    padColor_ = cfg.emitParams.padColor;
    mergeOrder_ = cfg.emitParams.mergeOrder;
    // optional 画布/单元尺寸：nullopt（未指定）→ 0（交 Core 自动推导），与 buildEngineConfig 的「0 视为未指定」互逆。
    mergeCols_ = cfg.emitParams.cols.value_or(0);
    mergeRows_ = cfg.emitParams.rows.value_or(0);
    mergeCellW_ = cfg.emitParams.cellWidth.value_or(0);
    mergeCellH_ = cfg.emitParams.cellHeight.value_or(0);

    // ---- 维持模型不变式：合并重排为 L3 专属（Core runEngine 硬约束）----
    // 载入/还原出「非 L3 + 重排」的非法组合时复位为坍缩，与 setMode 的守卫一致，避免引擎拿到非法配置。
    if (mode_ != engine::Tier::L3 && layout_ == engine::MergeLayout::REARRANGE)
        layout_ = engine::MergeLayout::COLLAPSE;

    // ---- 多层画布声明还原（Phase 3：与 buildSerializableConfig 互逆；layers 入快照使图层可撤销）----
    // 底图配方（§7 generate 段）回装为声明态（像素重生由调用方 onLoadConfig 专步回灌 setBaseImage，不走快照）；
    // 画布级 preprocess 仍交由 PreprocessController.applyLoadedQueue 在配置载入时单独重放（不入文档快照）；
    // 图层声明（transform/z/逐层 preprocess/尺寸）在此还原：位图不入快照，按 id 合并回现有内存像素供合成。
    baseLayer_ = cfg.base;
    // 底图整体透明度：§7 generate 段 transform.opacity 载入即回填为运行态 baseOpacity_，并把内存声明归一 255——
    //   透明度恒在 fadedCanvasBase 边界施加，避免与 composeBase 双淡（撤销/重做快照回退亦经此对称还原）。
    baseOpacity_ = baseLayer_.has_value() ? static_cast<std::uint8_t>(std::clamp(baseLayer_->transform.opacity, 0, 255)) : 255;
    if (baseLayer_.has_value()) baseLayer_->transform.opacity = 255;
    // 画布级整体透明度：§7 顶层 opacity 键还原（缺省已由 engineConfigFromJson 夹为 255）。
    canvasOpacity_ = static_cast<std::uint8_t>(std::clamp(cfg.canvasOpacity, 0, 255));
    // 底图色彩预处理：从 generate 段 base->preprocess 取色彩步入 baseSession_（内存 baseLayer_->preprocess 置空，
    //   色彩预处理恒由 fadedCanvasBase 边界施加、不烘焙 baseImage_，杜绝与 composeBase 双应用）。底图选中态复位（快照不含运行态选中）。
    baseSelected_ = false;
    baseSession_.clear();
    if (baseLayer_.has_value()) {
        preprocess::PreprocessPipeline colorSteps;
        for (const auto& step : baseLayer_->preprocess.items())
            if (preprocess::isColorStep(step)) colorSteps.add(step);
        baseSession_.setSteps(std::move(colorSteps));
        baseLayer_->preprocess.clear();   // 内存声明剥离 preprocess：重生底图时 composeBase 不再施加（改由 fadedCanvasBase）
    }
    bool layersRestored = false;
    if (cfg.layers.has_value()) {
        layersActive_ = true;
        canvasWidth_ = cfg.source.width;
        canvasHeight_ = cfg.source.height;
        imageLayersVisible_ = true;   // 显隐不入配置（纯运行态）：重载/快照回退默认可见。
        std::vector<canvas::ImageLayer> restored = *cfg.layers;
        // 配置/快照重载：旧会话与物化缓存整体作废，按新层集重建每层声明态会话（位图随后由控制器按需重烤）。
        layerSessions_.clear();
        for (auto& layer : restored) {
            // 合并回旧内存位图（File 层）：新配置/快照位图为空，但既有同名层可能已解码——保其像素以免重组降级。
            if (layer.decodedPixels.empty()) {
                if (const auto* old = layerById(layer.id); old && !old->decodedPixels.empty())
                    layer.decodedPixels = old->decodedPixels;
            }
            layerSessions_.ensure(layer.id).preprocess.setSteps(layer.preprocess);   // 逐层 preprocess 映到会话 steps
            if (layer.id >= nextLayerId_) nextLayerId_ = layer.id + 1;
        }
        layers_ = std::move(restored);   // 图片图层数组整体替换（显隐不随配置还原，已在上方置回默认可见）。
        if (selectedLayerId_ && layerById(*selectedLayerId_) == nullptr) selectedLayerId_.reset();
        layersRestored = true;
    } else {
        clearLayers();   // 配置无 layers：回退到非图层态（单一载入图）。
    }

    emit changed();
    // 图层声明变了→需重组展平画布：发 layersChanged 交 PreprocessController 走 composeCanvas 重算 original_/working_。
    if (layersRestored) emit layersChanged();
}

} // namespace idc::gui
