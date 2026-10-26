// ============================================================================
// 文件：model/document_canvas.cpp
// 作用：Document 的「画布/图层/预处理会话」状态域成员实现（TU 拆分）——图像载入/换底图/
//       工作图与预处理会话声明/预处理目标路由/图层栈 CRUD/画布尺寸/底图与画布不透明度。
//       本 TU 与 document.cpp 同属 Document 类（类不拆、仅分 TU 编译）。
// 分块依据：本域与「切割/选择/排序/导出参数 + 配置组装」域互不调用，各自独立成 TU
//       （CONTRIBUTING.md「分层纪律」；状态聚合器不拆类）。
// ============================================================================
#include "model/document.h"

#include <QFileInfo>

#include <algorithm>
#include <utility>
#include <variant>

namespace idc::gui {

namespace {

// fadedCanvasBase 缓存键的轻量内容指纹：只抽样首/尾各至多 4 字节（O(1)，与图像尺寸无关），
//   用于捕获「尺寸/格式不变但内容被整体换掉」（如 setBaseImage 换一张同尺寸新图）的漏检场景。
//   底图像素从不被逐像素就地修改（baseImage_ 仅经 move/copy 整体替换），故首尾指纹相同即可判定内容未变。
std::uint64_t baseFingerprintOf(const core::Image& img) {
    if (img.empty()) return 0;
    const std::size_t bpp = core::bytesPerPixel(img.format());
    const std::uint8_t* d = img.data();
    const std::size_t total = img.sizeInBytes();
    const std::size_t head = std::min<std::size_t>(bpp, 4);
    std::uint64_t fp = 0;
    for (std::size_t i = 0; i < head; ++i) fp = fp << 8 | d[i];
    const std::size_t tailStart = total > bpp ? total - bpp : 0;
    for (std::size_t i = 0; i < head && tailStart + i < total; ++i) fp = fp << 8 | d[tailStart + i];
    return fp;
}

} // namespace


// 恒等 compose 判定（内存短路 2，见头文件）：纯派生、无失效簿记——任一条件翻转即回落物化路径。
bool Document::composeIsIdentity() const {
    return layersActive_ && layers_.empty() && baseSession_.steps().empty() && baseOpacity_ == 255 &&
           !baseImage_.empty() && baseImage_.width() == canvasWidth_ && baseImage_.height() == canvasHeight_;
}

// 原图基准：恒等 compose 时别名底图（零物化），否则为展平物化 original_。
const core::Image& Document::original() const {
    return composeIsIdentity() ? baseImage_ : original_;
}

// 工作图（引擎输入，内存短路 1，见头文件）：恒等管线派生别名重放基准自身，否则物化 working_。
const core::Image& Document::working() const {
    if (preprocess_.steps().empty() && canvasOpacity_ == 255) {
        if (!preprocess_.hasResize()) return original();
        if (preprocess_.hasBase()) return preprocess_.base();   // resize 恒最先重放、队列空 → 工作图即 base_
    }
    return working_;
}

// 载入新图像＝建立「含底图的图层画布」（打开图片等价 新建画布[白底+原生附图]）：
//   底图入 baseImage_（input 生态位，**绝不入 layers_**）、叠加层栈清空；有图即图层画布。
void Document::setImage(core::Image img, QString path) {
    const int w = img.width();
    const int h = img.height();
    // 恒等态起步（内存短路）：底图入 baseImage_（唯一像素实体）；无叠加层/底图色/淡出 → compose 恒等，
    //   original_/working_ 不物化、经 accessor 派生别名 baseImage_（打开大图零克隆）。
    baseImage_ = std::move(img);
    original_ = core::Image{};
    working_ = core::Image{};
    preprocess_.clear();         // 换图：轻队列 / 独立 resize / base_ 缓存随原图一并失效。
    // 打开图片＝图层画布基座——baseImage_ 取原图像素（identity），叠加层栈 / 会话 / 选中清空（底图不入 layers_）。
    layersActive_ = true;
    canvasWidth_ = w;
    canvasHeight_ = h;
    baseSelected_ = false;
    baseSession_.clear();
    // 恒等底图声明（§7 generate 段：placement-only + solid white backdrop）：无「recipe 键」→ 载入端 baseFromJson 视作运行时位图底；
    //   white backdrop 令「无图重载」生成白底、且导 JPEG/BMP 透明区显白非黑（composeIntoBox 缺 backdrop 才透明）。
    canvas::ImageLayer base;
    base.fileName = QFileInfo(path).fileName().toStdString();
    base.hasImagePlacement = true;
    base.boxWidth = w;                                  // 落位框＝画布（＝原图尺寸，恒等）。
    base.boxHeight = h;
    base.imageFit = canvas::ImageFit::Native;           // 原生不缩放（图==框→逐像素恒等，无缺口）。
    base.imageAnchor = generate::ImageAnchor::Center;
    base.imagePivot = generate::ImageAnchor::Center;
    base.imageX = 0;
    base.imageY = 0;
    base.boxBackdrop = generate::PatternParams{};       // 缺省即 solid white（background=kWhite）：白底种子。
    baseLayer_ = std::move(base);
    layers_.clear();
    imageLayersVisible_ = true;
    layerSessions_.clear();
    selectedLayerId_.reset();
    nextLayerId_ = 1;
    baseOpacity_ = 255;
    canvasOpacity_ = 255;
    imagePath_ = std::move(path);
    hasRect_ = false;
    rect_ = engine::RectRegion{};
    rects_.clear();   // 多矩形列表同样基于原图坐标：换图即失效，随单选区一并清空。
    emit imageChanged();
}

// 删除底图并复位为空白文档（setImage 的逆操作）：把与「有图」相关的全部状态清回构造初态——
//   基准/工作图/底图像素与声明/预处理会话/图层栈与每层会话/选区全部置空，layersActive_=false。
//   发 imageChanged()：MainWindow::onImageChanged → PreviewController::rebuildPreviewPixmap 命中 !hasImage() 分支 scene_->clearAll() 清场。
//   不动 mode/polarity/导出/网格参数（它们仅是参数，空白文档下引擎/预览入口自被 hasImage 早退）。
void Document::resetToEmpty() {
    original_ = core::Image{};
    working_ = core::Image{};
    preprocess_.clear();
    imagePath_.clear();
    baseImage_ = core::Image{};
    baseLayer_.reset();
    baseOpacity_ = 255;
    baseSelected_ = false;
    baseSession_.clear();
    fadedBaseCache_.clear();   // 底图已空：作废淡出缓存。
    canvasOpacity_ = 255;
    layers_.clear();
    imageLayersVisible_ = true;
    layersActive_ = false;                // 回空白文档：非图层画布。
    canvasWidth_ = 0;
    canvasHeight_ = 0;
    selectedLayerId_.reset();
    nextLayerId_ = 1;
    layerSessions_.clear();
    hasRect_ = false;
    rect_ = engine::RectRegion{};
    rects_.clear();
    selectedCells_.clear();
    emit imageChanged();
}

// 新建语义（**非破坏性**）：以新扁平底图替换画布基座、保留图层栈与色彩预处理（详见头文件）。
//   与 setImage 的关键差异：不清图层/不取消标注会话、仅剥除会改变尺寸的 resize+几何预处理，
//   从而“新建同尺寸画布”不会清空图层/标注/预处理。标注捕获回填与色彩预处理重放均由调用方（SessionController）掌控。
void Document::resetBaseWith(core::Image flat, std::optional<canvas::ImageLayer> baseLayer) {
    const int newW = flat.width();
    const int newH = flat.height();
    const int curW = layersActive_ ? canvasWidth_ : original().width();
    const int curH = layersActive_ ? canvasHeight_ : original().height();
    const bool sizeChanged = newW != curW || newH != curH;

    imagePath_.clear();               // 新建画布无文件来源。
    baseLayer_ = std::move(baseLayer);  // 底图声明（同构 ImageLayer：程序图案或附图片+落位），供重组/序列化重生。
    // 新底图声明携带的 transform.opacity 回填为运行态 baseOpacity_、内存声明归一 255（透明度只走 fadedCanvasBase 边界单点施加，避免与声明字段双淡）。
    //   新建对话框可直接采集底图不透明度（0..255）落此 transform.opacity，故回填值随对话框选择而变。
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
        // 无图首次新建 → 起图层画布，flat 作 baseImage_（底图入基座、**绝不入 layers_**）；
        //   original_/working_ 交下方 layersChanged→recompose 展平产出（勿在此手设，免双路径）。
        beginLayerCanvas(newW, newH, std::move(flat));
    }

    if (sizeChanged) {                  // 尺寸变→旧框选坐标失效；同尺寸→保留框选（仅尺寸不变时留框选）。
        hasRect_ = false;
        rect_ = engine::RectRegion{};
        rects_.clear();
        selectedCells_.clear();
    }

    // 恒图层画布——发 layersChanged → recomposeLayerCanvas 以 fadedCanvasBase 展平 + 重放色彩预处理 → working_（内含 imageChanged）。
    emit layersChanged();
}

// 写回一次预处理物化结果：替换工作图（original_ / steps_ 不变）。
// 空图拒绝，避免画布/引擎拿到无效工作图。触发 imageChanged()（需重建预览底图）。
void Document::setWorkingImage(core::Image img) {
    if (img.empty()) return;
    working_ = std::move(img);
    emit imageChanged();
}

// 恒等产出回灌（内存短路 1，见头文件）：置声明态 + 清陈旧 working_ 位图 + 发 imageChanged 驱动预览重建。
// 与 setWorkingImage 互斥使用——提交期按「轻队列空 && 画布级不透明度 255」判据二选一。
void Document::commitIdentityPipeline(const preprocess::PreprocessPipeline& steps,
                                      const std::optional<ResizeTarget> resize) {
    preprocess_.setSteps(steps);
    preprocess_.setResize(resize);
    working_ = core::Image{};   // 释放陈旧物化位图；working() 自此派生别名（见 accessor）。
    emit imageChanged();
}

// 重组后回灌展平画布底图为 original_（多层画布专用）：保留画布级预处理声明、失效其 base_ 缓存；
// 空图拒绝。不碰 working_、不发信号（随后由控制器走 commitPipeline 重放产 working_）。
void Document::setOriginalImage(core::Image composedCanvas) {
    if (composedCanvas.empty()) return;
    original_ = std::move(composedCanvas);
    preprocess_.clearBase();   // 基准已变：旧 base_=resize(旧 original) 失效，下次提交重烤。
}

// 恒等 compose 时释放物化展平副本（内存短路 2，见头文件）：只清不填——original_ 清空后
//   original() 转别名 baseImage_；恒失效 base_（基准即底图）。不发信号、不碰 working_。
void Document::clearOriginalImage() {
    original_ = core::Image{};
    preprocess_.clearBase();
}

// 重置预处理：清空独立 resize 与轻操作队列、工作图恢复为原图。恒等态（画布级不透明度 255）零物化：
//   working() 派生别名 original()；否则按产出边界口径对 original() 施一次画布级淡出（防重置后淡出丢失）。
void Document::resetPreprocess() {
    if (!preprocess_.hasPreprocess()) return;
    preprocess_.clear();         // 清空轻队列 / 独立 resize / base_ 缓存（会话失效）。
    if (canvasOpacity_ == 255) {
        working_ = core::Image{};   // 恒等：working() 派生别名，释放陈旧物化位图
    } else {
        working_ = original();
        canvas::applyAlpha(working_, canvasOpacity_);
    }
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
    return preprocess_.geomSig(original().width(), original().height());
}

// 队列灰度推演：任一 GrayOp 使输出恒为灰度（下游 invert/rotate/flip/resize/split 均不恢复彩色），
// 供启停智能在灰度后停用「黑白」「色道分离」（纯元数据扫描，不碰像素）。
bool Document::preprocessProducesGray() const {
    return preprocess_.producesGray();
}

// ============================ 多层画布 ============================
// 说明：Document 只维护图层「声明态」（栈/尺寸/选中/每层会话），绝不跑像素、不读盘——
// composeCanvas（物化）与 File 层解码均由 PreprocessController / SessionController 经 EngineBridge 承担
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
    // 空文档（非图层画布）无底图交互目标：底图色彩/透明度仅在图层画布态可拾取。拦住 on，杜绝“能选不能作用”的死控制。
    if (on && !layersActive_) return;
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
    if (baseSelected_ && layersActive_) return baseSession_;   // 底图目标仅图层态可达（与 selectBase 门控同口径）
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

// 追加图层：分配递增稳定 id、置顶（zOrder = 栈长，恒与向量位序一致）、建其独立会话；发 layersChanged。
// 不变式（zOrder 不变式，SPEC §8.2 口径）：layers_ 向量恒按 zOrder 升序且 zOrder ≡ 向量下标（0..n-1）——
//   面板/场景按向量序天然有序，合成/序列化口径不漂移；任何增删移改后本不变式立即重建。
int Document::addImageLayer(canvas::ImageLayer layer) {
    layer.id = nextLayerId_++;
    layer.zOrder = static_cast<int>(layers_.size());   // 置顶：向量尾 + 下标即 zOrder
    const int id = layer.id;
    layerSessions_.ensure(id);   // 该层独立声明态会话（含 base 槽 + 物化签名槽）
    layers_.push_back(std::move(layer));
    emit layersChanged();
    return id;
}

// 删除某层（连带回收其会话；若正被选中则取消选中）。
bool Document::removeLayer(const int id) {
    const auto it = std::ranges::find_if(layers_,
                                         [id](const canvas::ImageLayer& l) { return l.id == id; });
    if (it == layers_.end()) return false;
    layers_.erase(it);
    layerSessions_.remove(id);   // 连带回收其会话与物化位图
    if (selectedLayerId_ == id) selectedLayerId_.reset();
    emit layersChanged();
    return true;
}

// 上/下移一层：向量邻位交换并同步 zOrder（恒等下标 0..n-1 不变式，免全量排序）。up＝向顶层（更高 zOrder）。
bool Document::moveLayerZOrder(const int id, const bool up) {
    const auto it = std::ranges::find_if(layers_,
                                         [id](const canvas::ImageLayer& l) { return l.id == id; });
    if (it == layers_.end()) return false;
    const auto i = static_cast<std::size_t>(it - layers_.begin());
    const auto n = layers_.size();
    const std::size_t j = up ? i + 1 : i > 0 ? i - 1 : i;   // up→邻 i+1（更高）；down→邻 i−1（更低）
    if (j == i || j >= n) return false;                        // 已在顶/底，不动
    std::swap(layers_[i], layers_[j]);
    layers_[i].zOrder = static_cast<int>(i);
    layers_[j].zOrder = static_cast<int>(j);
    emit layersChanged();
    return true;
}

// 移至顶层 / 底层：把元素搬到向量尾/首并重编号 zOrder（恒等下标不变式，免极值扫描与值漂移）。
bool Document::moveLayerToEdge(const int id, const bool toTop) {
    const auto it = std::ranges::find_if(layers_,
                                         [id](const canvas::ImageLayer& l) { return l.id == id; });
    if (it == layers_.end() || layers_.size() < 2) return false;
    const auto i = static_cast<std::size_t>(it - layers_.begin());
    if ((toTop && i + 1 == layers_.size()) || (!toTop && i == 0)) return false;   // 已在边缘
    canvas::ImageLayer moved = std::move(layers_[i]);
    layers_.erase(it);
    layers_.insert(toTop ? layers_.end() : layers_.begin(), std::move(moved));
    for (std::size_t k = 0; k < layers_.size(); ++k) layers_[k].zOrder = static_cast<int>(k);
    emit layersChanged();
    return true;
}

// 就地替换某层（transform / z / preprocess 编辑）：强制保持原 id 稳定（id 为会话/拾取键）。
// 外部传入的 zOrder 视作权威 → 替换后按 zOrder 稳定重排并重编号 0..n-1，重建「向量序≡zOrder 下标」不变式
//（transform-only 的逐帧更新已改走 updateLayerTransform，本函数仅在预处理编辑/整层替换等事件级路径调用）。
bool Document::updateLayer(const int id, const canvas::ImageLayer& layer) {
    const auto it = std::ranges::find_if(layers_,
                                         [id](const canvas::ImageLayer& l) { return l.id == id; });
    if (it == layers_.end()) return false;
    canvas::ImageLayer nl = layer;
    nl.id = id;
    *it = std::move(nl);
    std::ranges::stable_sort(layers_, [](const canvas::ImageLayer& a, const canvas::ImageLayer& b) {
        return a.zOrder < b.zOrder;
    });
    for (std::size_t k = 0; k < layers_.size(); ++k) layers_[k].zOrder = static_cast<int>(k);
    emit layersChanged();
    return true;
}

// 仅改落位变换（见头文件）：transform 就地写回、不拷贝声明其余字段——拖拽/落位编辑逐帧调用，
//   免整层声明（含 decodedPixels 源位图）的逐帧深拷贝；像素零触碰。
bool Document::updateLayerTransform(const int id, const canvas::ImageTransform& transform) {
    const auto it = std::ranges::find_if(layers_,
                                         [id](const canvas::ImageLayer& l) { return l.id == id; });
    if (it == layers_.end()) return false;
    it->transform = transform;
    emit layersChanged();
    return true;
}

// 图片图层整体显隐（图层面板「显示」开关）：**纯显示切换**——一个开关直接落到运行态标志 imageLayersVisible_（不入配置；无逐层 visible）。
// 值未变则短路；否则发 imageLayersVisibleChanged 交上层（MainWindow）切场景图层像素/落位框可见性，**不重组 working_、不影响导出**。
void Document::setImageLayersVisible(const bool visible) {
    if (imageLayersVisible_ == visible) return;
    imageLayersVisible_ = visible;
    emit imageLayersVisibleChanged(visible);
}

// 清空叠加图片图层：仅清 layers_ / 每层会话 / 选中，保留 layersActive_ 与 baseImage_（底图恒在基座、绝不入栈）。
//   调用方（clearAllLayersToBase / applyEngineConfig）随后经 layersChanged→recompose 以「仅底图」重出 original_/working_。
void Document::clearLayers() {
    if (!layersActive_) return;
    layers_.clear();
    layerSessions_.clear();
    selectedLayerId_.reset();
}

// 清空全部叠加图片图层、仅留底图：clearLayers 清叠加层（保留 baseImage_/layersActive_），发 layersChanged 交 recompose 以仅底图重出。
//   保留画布级预处理声明与标注（recompose 重放色彩预处理）；底图绝不入栈。仅图层态有效。
bool Document::clearAllLayersToBase() {
    if (!layersActive_) return false;
    const bool hadBase = !baseImage_.empty();
    clearLayers();   // 仅清叠加层（保留 baseImage_/layersActive_）；底图绝不入栈。
    emit layersChanged();   // 交 recompose 以「仅底图 fadedCanvasBase」重出 original_→working_（含色彩预处理重放）。
    return hadBase;
}

// 确保图层画布态：无图层但有单张载入图时，把当前原图纳为**画布底图**（input 生态位，不入 layers、无 transform）。
// 底图声明（若原图为程序图案 / 附图片底）保留在 baseLayer_，序列化时进 §7 generate 段。不动画布级预处理会话（preprocess_）——
// 重组后 composed==original_（空叠加层），工作图经 commitPipeline 重放保持不变。
bool Document::ensureLayerCanvas() {
    if (layersActive_) return true;
    if (original().empty()) return false;
    const int w = original().width();
    const int h = original().height();
    if (w <= 0 || h <= 0) return false;
    // 以原图为底图（base）进入图层编辑态（画布无级背景）；不往 layers 塞底图层（故底图不可拖 / 无 transform / 不失配方）。
    beginLayerCanvas(w, h, original().clone());
    return true;
}

// 设底图整体不透明度（0..255）：与图层自身 transform.opacity 正交——它是「画布基座」的整体淡出。
//   值未变早退；否则置值后发 layersChanged（交控制器以 fadedCanvasBase 重组预览，仅乘 alpha、不重算图案）
//   + changed()（刷新面板 / 状态栏）。透明度恒在 fadedCanvasBase 边界施加，不烘焙进 baseImage_。
void Document::setBaseOpacity(const int opacity) {
    const int v = std::clamp(opacity, 0, 255);
    if (static_cast<int>(baseOpacity_) == v) return;
    baseOpacity_ = static_cast<std::uint8_t>(v);
    fadedBaseCache_.clear();   // 键值已变：旧淡出产物作废（顺带释放全图内存；回 255 恒等态亦回收）。
    emit layersChanged();
    emit changed();
}

// 按 baseOpacity_ 淡出的画布底图（统一 DerivedCache：键＝底图内容签名+不透明度+色彩步，键变自愈重算；
//   命中直接回缓存引用零重跑）。空底图（无 base）返回空图由 composeCanvas 自然处理。
//   取用点：图层态 recompose / 导出重算作 composeCanvas 的底图入参（折入底图色+淡、不回写 original_）；
//   canvasBase() 本身保持原样供几何等用途。
const core::Image& Document::fadedCanvasBase() const {
    const core::Image& base = canvasBase();
    // 恒等早退（内存短路 2）：无底图色彩预处理与淡出 → 淡出结果即底图自身，零克隆零缓存；
    //   若此前缓存着旧淡出产物，释放之（回到恒等态回收一份全画布内存）。
    if (baseSession_.steps().empty() && baseOpacity_ == 255) {
        if (fadedBaseCache_.valid()) fadedBaseCache_.clear();
        return base;
    }
    const FadedBaseKey key{base.width(), base.height(), base.format(), baseFingerprintOf(base),
                           baseOpacity_, baseSession_.steps()};
    // 键未命中：现算（底图色彩预处理仅色彩步、resize 恒空，在合成边界施加——baseImage_ 保持
    //   未处理原始像素，杜绝与 composeBase 双应用；淡出同一边界仅施加一次）。
    return fadedBaseCache_.get(key, [&base, this](const FadedBaseKey&) {
        core::Image img = base.clone();
        if (!baseSession_.steps().empty())
            img = baseSession_.effectivePipeline().applyExec(img);
        canvas::applyAlpha(img, baseOpacity_);
        return img;
    });
}

// 设画布级整体不透明度（0..255）：三元目标 Canvas 的透明度，与图层 transform.opacity、底图 baseOpacity 正交。
//   值未变早退；否则置值后发 changed()（刷新面板 / 历史采集）。重算 working_ 交调用方（SessionController）随后走
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
    fadedBaseCache_.clear();                // 键值已变：旧淡出产物作废（顺带释放全图内存；回空恒等态亦回收）。
    emit layersChanged();
    return true;
}



} // namespace idc::gui
