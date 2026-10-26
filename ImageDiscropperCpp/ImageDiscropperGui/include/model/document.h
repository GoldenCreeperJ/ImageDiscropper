// ============================================================================
// 文件：model/document.h
// 作用：GUI 会话状态的「单一真相源」——持有原图 / 工作图、当前模式(L1/L2/L3)、
//       极性、切割几何参数、选择集与导出参数，并负责把它们组装成 Core 的
//       EngineConfig。任何面板/画布都只读写 Document，不各自持有真相，避免耦合。
// 分块依据：
//   - Document 只做「状态存储 + 参数翻译」，绝不含切割/几何/排序/极性判定逻辑
//     （那些全在 Core；CONTRIBUTING.md「分层纪律」）。组装 EngineConfig 只是字段搬运与枚举映射。
//   - 真正调用 Core 的动作集中在桥接类（切割/预处理 EngineBridge、标注域 AnnotationBridge），Document 不碰 Core API。
// 说明：状态变更统一发 changed() 信号；换图发 imageChanged()（需重建预览 pixmap）。
//       MainWindow 监听这两个信号驱动预览刷新与面板同步。
// ============================================================================
#pragma once

#include <QObject>

#include <cstdint>
#include <optional>
#include <unordered_map>
#include <vector>

#include "canvas/layer.h"
#include "core/color.h"
#include "core/image.h"
#include "engine/engine.h"
#include "model/layer_session_store.h"
#include "model/preprocess_session.h"
#include "preprocess/preprocess_pipeline.h"
#include "util/gui_nums.h"
#include "i18n/gui_texts.h"

namespace idc::gui {

class ConfigCodec;   // 配置映射在 model/config_codec（friend 直读私有状态，见下方转发）。

// L1 标准提取的形状选择（映射到 Core 的 CutGenerator）。
enum class L1Shape {
    Rect,   // 矩形选框 → CutGenerator::Rect
    Hband,  // 横带     → CutGenerator::HorizontalLine
    Vband,  // 竖带     → CutGenerator::VerticalLine
};

// L2 反向剔除的子功能（映射到 Core 的 CutGenerator）。
enum class L2Sub {
    Cross,     // 十字切割（单矩形诱导贯穿十字带）→ CutGenerator::Rect
    Hline,     // 横线切割 → CutGenerator::HorizontalLine
    Vline,     // 竖线切割 → CutGenerator::VerticalLine
    MultiRect, // 多矩形并集剔除（各矩形诱导十字带取并集）→ CutGenerator::MultiRect
};

// 独立画布尺寸 ResizeTarget 定义于 model/preprocess_session.h（零 Qt 会话对象独立编译），
// 经上方 include 传递可见。

// 共享预处理面板的「当前目标」（三元）：
//   • 选中某图片图层时为该层（逐层预处理，几何 + 色彩）；
//   • 图层态下拾取到「底图（画布基座）」时为目标 Base（仅开放色彩预处理，几何/resize 禁用）；
//   • 否则（未在任何拾取态 / 点击落在画布外）为画布 Canvas（画布级预处理 + 画布级透明度）。
// 三者各持一份预处理声明态与一份不透明度（见 activePreprocessSession / transform.opacity / baseOpacity / canvasOpacity）。
enum class PreprocessTarget {
    Canvas,  // 画布级预处理（作用于展平后的画布图，产出喂引擎的工作图）+ canvasOpacity
    Layer,   // 逐层预处理（作用于选中图片图层，物化后再落位进画布）+ transform.opacity
    Base,    // 底图（画布基座）：仅色彩预处理（几何/resize 禁用，不改画布尺寸/坐标基准）+ baseOpacity
};

// ---------------------------------------------------------------------------
// Document：会话状态单一真相源（QObject，便于信号驱动刷新）。
// ---------------------------------------------------------------------------
class Document : public QObject {
    Q_OBJECT
    friend class ConfigCodec;   // 配置组装/还原直读私有状态（model/config_codec.h）。
public:
    explicit Document(QObject* parent = nullptr);

    // ---- 图像 ----
    // 载入新图像＝建立「含底图的图层画布」（打开图片）：img 立为 baseImage_（input 生态位，**绝不入 layers_**）、
    //   layersActive_ 置真、恒等底图声明带白 backdrop；original_ 与 working_ 均置为 img（无预处理）。记录路径、清空选区，发 imageChanged()。
    void setImage(core::Image img, QString path);
    // 删除底图并复位为空白文档（「重置全部」勾选「同时删除底图」）：把图像/图层/底图/预处理会话/选区
    //   全部回到构造初态（layersActive_=false、各像元清空），发 imageChanged() 交预览 clearAll 清场。
    //   仅复位与「有图」相关的状态；模式/极性/导出/网格参数不动（空白文档下引擎入口自会被 hasImage 早退）。
    void resetToEmpty();
    // 新建语义（**非破坏性**）：以新扁平底图 flat 替换画布基座（入 baseImage_），保留图层栈/每层会话与画布级**色彩预处理**，
    //   清空 resize 与**几何预处理**（防重放改变尺寸）；**不触碰标注**（由调用方捕获后回填）；
    //   尺寸不变则保留框选、尺寸变化则清框选。恒图层画布——无图首次新建经 beginLayerCanvas 起画布（flat 作底图、绝不入
    //   layers_），末尾恒发 layersChanged（交控制器重组重放色彩预处理产出 working_）。
    void resetBaseWith(core::Image flat, std::optional<canvas::ImageLayer> baseLayer);
    const core::Image& original() const { return original_; }
    const core::Image& working() const { return working_; }
    bool hasImage() const { return !working_.empty(); }
    int width() const { return working_.width(); }
    int height() const { return working_.height(); }
    const QString& imagePath() const { return imagePath_; }

    // 重组后回灌「展平后的画布底图」为 original_（多层画布专用）：仅换原图基准、保留画布级预处理会话声明（steps_/resize_），
    // 但失效其 base_ 重放缓存（基准已变）。不碰 working_、不发信号——由控制器随后走 commitPipeline 重放产出新 working_。
    void setOriginalImage(core::Image composedCanvas);

    // ---- 预处理工作图（FR-1；独立 resize + 声明式轻操作队列）----
    // 模型：original_（不可变）→ resize_（可选，恒最先重放、从原图重采样）→ steps_（可排序轻操作队列，绝不含 ResizeOp）
    //       → working_（= 在 resize 产物上重放 steps_ 的物化结果）。
    // 每次编辑由上层（PreprocessController）重放物化后写回；Document 只存状态、不跑像素运算（CONTRIBUTING.md「分层纪律」）。
    // 用一次预处理结果替换工作图；original_ / resize_ / steps_ 保持不变，供「重置预处理」与几何签名重算。
    // 由 PreprocessController 经 EngineBridge 算出新图后写回；触发 imageChanged()。
    void setWorkingImage(core::Image img);
    // 重置预处理：清空 resize_ 与轻操作队列、工作图恢复为原图；未预处理时早退。触发 imageChanged()。
    void resetPreprocess();
    // 当前工作图是否已被预处理（有 resize 或队列非空）——供面板/菜单启用「重置预处理」。
    bool hasPreprocess() const { return preprocess_.hasPreprocess(); }

    // ---- 独立画布尺寸（Resize）----
    // resize_ 为可选目标尺寸；设置后由 PreprocessController 重放（恒在轻操作之前、从原图重采样）。
    bool hasResize() const { return preprocess_.hasResize(); }
    std::optional<ResizeTarget> resizeTarget() const { return preprocess_.resizeTarget(); }
    // 覆盖 / 清除独立 resize（std::nullopt = 无 resize）。仅存状态，不物化、不发信号。
    void setResize(std::optional<ResizeTarget> target);

    // ---- 声明式预处理队列（steps_）----
    // 只读访问当前队列（供面板渲染步骤列表、控制器克隆编辑）。
    const preprocess::PreprocessPipeline& steps() const { return preprocess_.steps(); }
    // 覆盖整条队列（控制器完成增删移改后回灌）；仅更新声明状态，不物化工作图、不发信号
    //（物化与刷新时机由调用方经 setWorkingImage / changed 掌控）。
    void setSteps(preprocess::PreprocessPipeline pipeline);
    // 几何签名：对 original 尺寸跑 steps_ 的纯元数据推演，得 {输出宽高, 净旋转象限}。
    // 队列编辑前后比较此签名——尺寸或净旋转象限变化即判定几何失效（清空框选/标注），
    // 仅换色/换序等不改签名者保留既有选择（不清框）。
    preprocess::PreprocessPipeline::GeometryOut geomSig() const;

    // 队列是否已产出灰度：任一 GrayOp 之后输出恒为灰度（invert/rotate/flip/resize 均不恢复彩色，
    // split 对灰度图为无操作）——供预处理面板 / 菜单在灰度后停用「黑白」「色道分离」两项冗余操作。
    bool preprocessProducesGray() const;

    // ---- base_ 重放缓存（独立 resize 作用于 original 的物化产物；见 preprocess_controller.cpp commitPipeline）----
    // 目的：编辑轻操作队列时，无需从 original_ 重跑含 resize 的重采样——直接复用 base_ 只重放轻队列。
    //       base_ 对应 basePath_（即 [resize?] 前缀），前缀逐字段相等才复用（无 resize 则为原图副本）。
    // 生命周期：仅 PreprocessController 经 setBase 维护；凡换图（setImage）/重置预处理（resetPreprocess）即失效。
    bool hasBase() const { return preprocess_.hasBase(); }
    const core::Image& base() const { return preprocess_.base(); }
    const preprocess::PreprocessPipeline& basePrefix() const { return preprocess_.basePrefix(); }
    // 记录一段前缀的重放产物为 base_（img 为其物化结果，prefix 为其对应的重放前缀队列）。
    void setBase(core::Image img, preprocess::PreprocessPipeline prefix);
    // 释放 base_ 重放缓存（由「有 resize」切到「无 resize」时回收内存；与 setImage / resetPreprocess 的失效同处理）。
    void clearBase() { preprocess_.clearBase(); }
    // 候选前缀是否需要重烤：无缓存，或已缓存 basePath_ 与之逐字段不等 → true。供 commitPipeline 提交前判 baseDirty。
    bool baseDirtyFor(const preprocess::PreprocessPipeline& prefix) const { return preprocess_.baseDirtyFor(prefix); }

    // ---- 多层画布（画布层概念，引擎零改动）----
    // 建模不变式：**任何有效图像都是图层画布**——layersActive_ 恒为真（仅空文档为假）。
    //   original_ 恒为 composeCanvas(layers_, canvasW, canvasH, fadedCanvasBase()) 的产物（PreprocessController 经
    //   EngineBridge 物化回灌），其上再叠画布级预处理（preprocess_）得 working_。底图 baseImage_ 独立保留于 input 生态位、
    //   **绝不入 layers_**，供重组取底。打开图片≡新建画布[白底+原生附图]：底图立为 baseImage_、叠加层栈恒空直至用户加层。
    // hasLayers()＝layersActive_（是否图层画布）：有图即真（底图可选中路经此恒开）；仅空文档为假。
    bool hasLayers() const { return layersActive_; }
    const std::vector<canvas::ImageLayer>& layerStack() const { return layers_; }
    int canvasWidth() const { return canvasWidth_; }
    int canvasHeight() const { return canvasHeight_; }
    // 画布底图（input 生态位）：图层画布态（有图）返回独立保留的 baseImage_，空文档兜底返回 original_。供控制器 composeCanvas 取底。
    const core::Image& canvasBase() const { return layersActive_ ? baseImage_ : original_; }
    // 画布底图声明（同构 canvas::ImageLayer）：可空＝无生成底图（外部文件图 / 纯色底）。随 §7 generate 段往返。
    const std::optional<canvas::ImageLayer>& baseLayer() const { return baseLayer_; }
    void setBaseLayer(std::optional<canvas::ImageLayer> l) { baseLayer_ = std::move(l); }  // onNew 底图声明（图案/附图片落位）留存
    // 显式设底图像素（配置载入时按 generate 段重生底图后回灌；不改 layersActive_ / 声明态）。
    void setBaseImage(core::Image img) { baseImage_ = std::move(img); }

    // ---- 底图整体不透明度（0..255，255＝不透明）----
    //   与图层自身 transform.opacity 正交：它是「底图（画布基座）」的整体淡出，恒在合成边界施加
    //   （见 fadedCanvasBase），不烘焙进 baseImage_ 像素、也不落 baseLayer_ 声明（内存声明恒存 255）。
    //   随 §7 generate 段往返（保存回填 config.base->transform.opacity、载入归一内存声明为 255）。
    std::uint8_t baseOpacity() const { return baseOpacity_; }
    void setBaseOpacity(int opacity);   // 相等早退；否则置值后发 layersChanged（图层态重组）+ changed()（刷新面板）。
    // 按 baseOpacity_ 淡出的画布底图（供 PreprocessController 图层态 recompose / 导出重算作 composeCanvas 的 base 入参）。
    //   canvasBase() 保持原样（几何等其它用途）；淡出只此一处施加，避免与 composeBase 双淡。
    //   拖动提交每次重组都会调用本方法，但图层拖动本身从不改变底图/其色彩预处理/底图不透明度三者中的任一项——
    //   故按「内容签名」（底图尺寸/格式/首尾字节指纹 + baseOpacity + baseSession_ 色彩步声明）缓存淡出结果：
    //   签名未变则零重算直接复用（仅新增一份底图尺寸量级的常驻缓存，随签名变化自动覆盖、无内存泄漏风险）。
    const core::Image& fadedCanvasBase() const;

    // ---- 画布级整体不透明度（0..255，255＝不透明）----
    //   三元目标之一（Canvas）的透明度：作用于合成 + 画布预处理后的整幅工作图，与图层 transform.opacity、
    //   底图 baseOpacity 正交。恒在产出边界（PreprocessController commitPipeline / 导出重算）逐像素 applyAlpha 施加一次，
    //   不烘焙进缓存位图（working_ 每次从基准重放、非累加 → 无重复淡出）。随 §7 顶层 opacity 键往返。
    int canvasOpacity() const { return canvasOpacity_; }
    // 相等早退；否则置值后发 changed()（刷新面板 / 历史采集）——重算 working_ 由调用方（SessionController）随后走 replayCanvasPreprocess。
    void setCanvasOpacity(int opacity);

    // ---- 底图（画布基座）可选中 + 底图色彩预处理 ----
    //   底图作为可选中对象（图片拾取态下点画布内非图层处）：与 selectLayer 互斥（选底图清选中层、选层清底图选中）。
    bool baseSelected() const { return baseSelected_; }
    void selectBase(bool on);   // 置真时清 selectedLayerId_；发 changed()（切换预处理当前目标）。
    // 底图色彩预处理声明态（恒不含几何/resize）：供共享预处理面板按目标驱动（activeSession 路由到 baseSession_）。
    //   updateBasePreprocess 只留色彩步、发 layersChanged（交控制器以 fadedCanvasBase 重组预览）；底图色彩预处理仅作用底图，
    //   恒经 fadedCanvasBase 边界施加：由 recompose 取用折进 original_，commitPipeline 再以 original_ 为基准产出 working_
    //   （与 CLI composeBase 逐像素同序）。
    preprocess::PreprocessPipeline basePreprocess() const { return baseSession_.steps(); }
    bool updateBasePreprocess(const preprocess::PreprocessPipeline &colorSteps);   // 过滤至色彩步后写 baseSession_；内容未变早退返回 false。

    // 当前拾取的图片图层（nullopt＝未选中→预处理目标为底图或画布）。
    std::optional<int> selectedLayerId() const { return selectedLayerId_; }
    void selectLayer(std::optional<int> id);            // 设选中态并发 changed（清 baseSelected_；切换预处理当前目标）
    const canvas::ImageLayer* layerById(int id) const;  // 只读取层（越界/无→nullptr）
    int layerCount() const { return static_cast<int>(layers_.size()); }
    // 叠加图层声明态拷贝（含 decodedPixels）——保存配置打包资产时取像素用；
    // 常规读取仍走 layerById（避免外部持引用越过不变式维护）。
    std::vector<canvas::ImageLayer> layersSnapshot() const { return layers_; }

    // 预处理当前目标三元路由：选中图层→Layer；否则选中底图→Base；否则→Canvas。
    PreprocessTarget preprocessTarget() const {
        if (selectedLayerId_.has_value()) return PreprocessTarget::Layer;
        if (baseSelected_) return PreprocessTarget::Base;
        return PreprocessTarget::Canvas;
    }
    // 当前目标的声明态会话（画布 preprocess_ 或选中图层会话）；供共享预处理面板按目标驱动。
    PreprocessSession& activeSession();
    const PreprocessSession& activeSession() const;
    // 图层会话库（每层声明态会话 + 物化签名 + 物化位图）：供 PreprocessController 驱动刷新与 composeCanvas 取位图缓存。
    LayerSessionStore& layerSessions() { return layerSessions_; }
    const LayerSessionStore& layerSessions() const { return layerSessions_; }

    // ---- 图层声明态 CRUD（纯改栈、不跑像素；重算工作图由控制器接 layersChanged 触发重组）----
    // 起一个图层画布：置 layersActive_、设尺寸、留存底图 base（入 baseImage_、绝不入 layers_）、清空旧叠加层与会话。
    // base 为空＝无底图（仅透明底）；非空＝画布基座（input 生态位，不入 layers）。画布无级背景（新画布底色已烤入 base 像素）。
    void beginLayerCanvas(int width, int height, core::Image base = {});
    int  addImageLayer(canvas::ImageLayer layer);   // 追加，分配递增 id 与置顶 zOrder；返回新 id
    bool removeLayer(int id);                       // 删除某层（连带回收其会话）
    bool moveLayerZOrder(int id, bool up);          // 与相邻层交换 zOrder（上/下移一层）
    bool moveLayerToEdge(int id, bool toTop);       // 移至顶层（zOrder = 当前最大 +1）/ 底层（最小 −1）
    bool updateLayer(int id, const canvas::ImageLayer& layer); // 就地替换（transform/z/preprocess 编辑）
    // 图片图层「显示」开关（图层面板）：**仅影响画布预览**——纯 GUI 运行态标志（不入 §7 配置、不随存盘往返），
    //   纯显示切换：发 imageLayersVisibleChanged 交上层切场景图层像素/落位框可见性，**不驱动重组**（working_ 恒为全量合成）。
    //   导出是否并入叠加层由下方「烧录」开关独立决定，二者正交。值未变则短路。
    void setImageLayersVisible(bool visible);
    bool imageLayersVisible() const { return imageLayersVisible_; }
    // 图片图层「烧录」开关（导出面板）：**仅影响导出**——导出时是否把叠加图片图层并入像素随切割。
    // 纯 GUI 运行期标志（对标 AnnotationBridge::burnInEnabled），不入 §7 配置、不随存盘往返，默认开。
    bool layersBurnIn() const { return layersBurnIn_; }
    void setLayersBurnIn(const bool on) { layersBurnIn_ = on; }
    void clearLayers();                             // 清空叠加图片图层（保留 layersActive_ 与 baseImage_）
    // 清空全部叠加图片图层、仅留底图（图层画布与 baseImage_ 原样保留、底图绝不入栈）：
    //   发 layersChanged 交控制器以「仅底图」重出 original_/working_。**保留画布级预处理声明与标注**。仅图层态有效，否则早退返回 false。
    bool clearAllLayersToBase();
    // 声明画布尺寸：更新 canvasW/H 并发 layersChanged（交控制器重组）。尺寸变化只清框选、不清图层/标注。
    void setCanvasSize(int width, int height);

    // 确保处于图层画布态（兜底）：有图即图层画布（layersActive_ 恒真）→ 直接返回 true；无图 → 返回 false。
    //   「添加图片图层」入口调用它，正常路径恒命中「已图层画布」早退；保留仅为防御空文档。
    bool ensureLayerCanvas();

    // ---- 模式与极性 ----
    engine::Tier mode() const { return mode_; }
    void setMode(engine::Tier t);                 // 切换模式并套用该模式的默认极性
    engine::Polarity polarity() const { return polarity_; }
    // 切换极性；L1/L2 与极性一一耦合（keep→L1、remove→L2）会同步改模式，L3 极性独立不改模式。
    void setPolarity(engine::Polarity p);

    // ---- L1 / L2 子选项 ----
    L1Shape l1Shape() const { return l1Shape_; }
    void setL1Shape(L1Shape s);
    L2Sub l2Sub() const { return l2Sub_; }
    // 切到多矩形时，当前单选区会被带入空的多矩形列表（见 adoptRectIntoMultiRects）。
    void setL2Sub(L2Sub s);

    // ---- 切割几何（选区矩形，原图像素坐标，左闭右开）----
    const engine::RectRegion& rect() const { return rect_; }
    bool hasRect() const { return hasRect_; }
    void setRect(const engine::RectRegion& r);     // 设置选区并置 hasRect_
    void clearRect();                                   // 清除选区（Esc / 右键清除）

    // ---- L2 多矩形并集（FR-L2 多矩形；仅 L2Sub::MultiRect 生效）----
    // rects_ 为用户框选追加的多个矩形，Core 对每个矩形诱导贯穿十字带后取并集剔除。
    // GUI 只维护矩形列表（增/删/改/清空）并搬运进 CutConfig::rects，不做任何并集/几何计算（CONTRIBUTING.md「分层纪律」）。
    const std::vector<engine::RectRegion>& rects() const { return rects_; }
    void addRect(const engine::RectRegion& r);                 // 追加一个矩形（拒绝退化）
    void updateRect(std::size_t index, const engine::RectRegion& r); // 修改指定矩形（拒绝退化）
    void removeRect(std::size_t index);                             // 删除指定矩形
    void clearRects();                                              // 清空多矩形列表

    // ---- 导出参数（CompositionParams 的 GUI 侧映射）----
    engine::ExportMode exportMode() const { return exportMode_; }
    engine::MergeLayout layout() const { return layout_; }
    void setExport(engine::ExportMode mode, engine::MergeLayout layout);
    engine::ExportFormat format() const { return format_; }
    void setFormat(engine::ExportFormat f);
    int quality() const { return quality_; }
    void setQuality(int q);
    const QString& naming() const { return naming_; }
    void setNaming(const QString& n);
    const QString& outputDir() const { return outputDir_; }
    void setOutputDir(const QString& d);
    const QString& outputFile() const { return outputFile_; }
    void setOutputFile(const QString& f);

    // ---- 重排合并参数（FR-L3.7 / SPEC §4.3；仅 Merged+Rearrange 生效）----
    // cols/rows/cellWidth/cellHeight 以 0 表示「未指定」→ buildEngineConfig 保持 nullopt，
    // 交 Core compose 依保留块数自动推导画布；正值才写入。padColor 为空位/余量填充色。
    // GUI 只搬运字段，画布尺寸/落位全由 Core compose 计算（CONTRIBUTING.md「分层纪律」）。
    int mergeCols() const { return mergeCols_; }
    int mergeRows() const { return mergeRows_; }
    int mergeCellW() const { return mergeCellW_; }
    int mergeCellH() const { return mergeCellH_; }
    void setMergeGrid(int cols, int rows);   // 重排画布列/行（0=自动推导）
    void setMergeCellSize(int w, int h);     // 重排单元宽/高（0=用保留块原尺寸）
    core::Color padColor() const { return padColor_; }
    void setPadColor(core::Color c);    // 空位/余量填充色（默认透明）

    // ---- 重排填充顺序（MergeOrder；仅 Merged+Rearrange 生效，与 L3 选择排序 order_ 正交）----
    // order_ 决定「保留块的先后列表」（含 Custom 拖拽序）；mergeOrder_ 决定「该列表以行/列优先
    // + 蛇形? + 倒序? 的路径铺进 cols×rows 输出画布」。GUI 只搬运字段，落位由 Core compose 计算（CONTRIBUTING.md「分层纪律」）。
    const engine::MergeOrder& mergeOrder() const { return mergeOrder_; }
    void setMergeOrderStrategy(engine::SortStrategy s); // 仅 RowMajor / ColumnMajor（Custom 按行优先）
    void setMergeOrderReverse(bool on);                      // 填充路径整体倒序
    void setMergeOrderSnake(bool on);                        // 填充路径蛇形

    // ---- L3 网格参数（FR-L3.1 网格定义 / FR-L3.2 余量策略）----
    // 每轴独立定义（Core GridAxisSpec 三态）：周期（基准点 + 单元尺寸）、分割数（等分）、比例（权重）；
    // 行列数由 Core 依图像边界自动推导。
    const engine::GridParams& gridParams() const { return grid_; }
    void setGridOrigin(int x, int y);                  // 基准点 (x0,y0)
    void setCellSize(int w, int h);                    // 单元尺寸 (cellWidth,cellHeight)
    // 一次性写回全部网格几何（画布拖拽基准手柄的单一入口）：同一次手势只发一次 changed，
    // 避开「先改基准再改尺寸」两次全量重算；几何变即清空选择集（同上面两个单参入口）。
    // 只写平铺（周期）字段——显式定义的轴（分割数/比例）由 axisX/axisY 持有，不受影响。
    void setGridParams(int x, int y, int w, int h);
    void setRemainder(engine::RemainderPolicy p); // 余量策略 discard/keep-partial/pad
    // ---- L3 单轴定义（按轴三形态；与 config/CLI 的 x/y 嵌套定义同一口径）----
    // 取某轴的有效定义（未显式定义的轴由平铺字段合成周期定义，余量策略取网格级 remainder）：
    // 直接转调 Core，GUI 不自算轴语义。
    engine::GridAxisSpec gridAxis(bool horizontal) const;
    // 该轴是否走周期定义（仅周期轴产生残缺单元 → 余量策略 / 画布基准手柄据此门控）。
    bool gridAxisIsPitch(bool horizontal) const;
    // 写回某轴定义：周期收回平铺字段（axisX/axisY 置空，避免双真值），分割数/比例写入显式定义。
    // 非法定义（周期非正 / 份数 <1 / 权重非正）一律拒绝，保持原状。
    void setGridAxis(bool horizontal, const engine::GridAxisSpec& spec);
    // 派生行列数（只读显示，由 PreviewController 依 Core Grid 结果回灌；不触发 changed 以免回环）。
    int gridRows() const { return gridRows_; }
    int gridCols() const { return gridCols_; }
    void setDerivedGridSize(int rows, int cols);
    // 「转为网格模式编辑」：把当前矩形选区一键送入 L3——
    // 以选区左上为基准点、选区宽高为单元尺寸，切到 L3（极性置 keep）并清空选择集（用户再逐单元精修）。
    void convertRectToGrid();

    // ---- L3 选择集（FR-L3.3：全选 / 反选 / 清空；单元点选交互见画布）----
    // 说明：selectedCells_ 的顺序在 CUSTOM 排序下即自定义序列（Core runEngine 约定）；
    //       L1/L2 恒空，由 Core 依生成器自动推导选择区。
    const std::vector<int>& selectedCells() const { return selectedCells_; }
    void setSelectedCells(std::vector<int> cells);  // 覆盖选择集（发 changed）
    void selectAllCells();                          // 全选（依派生行列数）
    void invertCells();                             // 反选（依派生行列数取补集）
    void clearCells();                              // 清空选择集
    void toggleCell(int index);                     // 单击切换某单元（CUSTOM 下点选顺序即序号）
    void addCells(const std::vector<int>& indices); // 并入框选命中的单元（去重，保持纳入顺序）
    // CUSTOM 拖拽调序：to 已选中 → 与 from 互换位次（其余单元的位次不变）；
    // to 未选中 → to 顶替 from 的原序位（from 移出选择集），位次与数量均不变。
    void swapCellOrder(int from, int to);

    // ---- L3 排序（FR-L3.5：row-major / column-major / reverse / snake / custom）----
    const engine::SequenceParams& order() const { return order_; }
    void setSortStrategy(engine::SortStrategy s);
    void setSortReverse(bool on);
    void setSortSnake(bool on);

    // ---- 组装 Core 配置（纯字段搬运 + 枚举映射，无引擎逻辑）----
    // 依当前模式/子选项构建切割配置。
    engine::CutConfig buildCutConfig() const;
    // 运行/可序列化配置组装与反向还原在 ConfigCodec（model/config_codec，本类 friend）；
    // 以下为转发到 ConfigCodec 的一行包装（定义在 document.cpp，避免头文件依赖 ConfigCodec 完整类型）。
    engine::EngineConfig buildEngineConfig() const;
    engine::EngineConfig buildSerializableConfig() const;
    void applyEngineConfig(const engine::EngineConfig& cfg, bool preserveBase = false);

signals:
    // 换图：需重建预览 pixmap、重置视图。
    void imageChanged();
    // 任意参数变更：仅需刷新预览与状态栏（不重建底图）。
    void changed();
    // 模式 / 极性切换（仅公开 setMode / setPolarity 触发；配置加载直写字段不走此信号）：
    // 供上层在用户切模式/极性后把画布交互态收口回「框选」。
    void modeOrPolarityChanged();
    // 图层栈/画布尺寸声明态变更（addImageLayer/removeLayer/move/update/setCanvasSize/beginLayerCanvas）：
    // 需 PreprocessController 接此信号走 composeCanvas 重算 original_（再叠画布预处理→working_）。
    void layersChanged();
    // 图片图层「显示」开关切换（setImageLayersVisible）：**纯显隐**，不重组工作图。
    // 需 MainWindow 接此信号→ CanvasScene::setImageLayersVisible 切图层像素覆盖图元与落位框可见性。
    void imageLayersVisibleChanged(bool visible);

private:
    // 进入 L2 多矩形时的选区带入：列表为空且有有效单选区时，把该矩形作为首个多矩形（见 setMode / setPolarity / setL2Sub）。
    void adoptRectIntoMultiRects();

    core::Image original_;   // 原始载入图（RGBA）——始终不变，供队列重放与「重置预处理」还原
    core::Image working_;    // 切割引擎实际输入图（= steps_ 重放到 original 的物化结果，无预处理时等于原图）
    // 预处理会话态（steps_/resize_/base_/basePath_ 四件套 + geo→color 不变式/归一化/签名推演）内聚于此，
    // Document 组合持有作单一真相源；读写经上方转委托方法暴露，Controller 经 Document 驱动（见 preprocess_session.h）。
    PreprocessSession preprocess_;
    QString imagePath_;

    // 多层画布声明态（有图即 layersActive_ 真）：original_ 由底图 baseImage_ + 叠加层栈展平而来。
    core::Image baseImage_;                                  // 画布底图像素（input 生态位；overlays 之下的基座，不随重组刷新）
    std::optional<canvas::ImageLayer> baseLayer_;            // 底图声明（同构 ImageLayer，钉住最底：图案配方或文件+落位），随 §7 generate 段往返
    std::uint8_t baseOpacity_{255};                          // 底图整体不透明度（0..255，255＝不透明）：合成边界淡出用，内存 baseLayer_ 声明恒存 255
    bool baseSelected_{false};                               // 底图是否被选中（图片拾取态点画布内非图层处）；与 selectedLayerId_ 互斥
    PreprocessSession baseSession_;                          // 底图色彩预处理声明态（steps_ 仅色彩、resize_ 恒 nullopt）；fadedCanvasBase 边界施加、不烘焙进 baseImage_
    // fadedCanvasBase 的「内容签名 + 淡出结果」缓存：签名（见 matches()）不变即直接复用 value_、免重跑 clone/applyExec/applyAlpha。
    //   不采用「显式失效挂钩」——因 baseImage_/baseOpacity_/baseSession_ 均可被 ConfigCodec（friend）直接改字段绕过 setter，
    //   挂钩遗漏会造成静默脏缓存；改用内容指纹自行判等，签名变了自然重算，无需任何调用点配合。
    struct FadedBaseCache {
        bool valid = false;
        int baseWidth = 0;
        int baseHeight = 0;
        core::ImageFormat baseFormat = core::ImageFormat::Rgba;
        std::uint64_t baseFingerprint = 0;   // 首尾各采样至多 4 字节的轻量内容指纹（O(1)，不遍历全图）
        std::uint8_t opacity = 255;
        preprocess::PreprocessPipeline steps;

        bool matches(const FadedBaseCache& o) const {
            return valid && o.valid && baseWidth == o.baseWidth && baseHeight == o.baseHeight &&
                   baseFormat == o.baseFormat && baseFingerprint == o.baseFingerprint &&
                   opacity == o.opacity && steps == o.steps;
        }

        core::Image value;   // 淡出后的完整底图位图（命中时直接按引用回给调用方，零拷贝）
    };
    mutable FadedBaseCache fadedBaseCache_;
    std::uint8_t canvasOpacity_{255};                        // 画布级整体不透明度（0..255）：产出边界 applyAlpha 施加一次、影响预览与导出，随 §7 顶层 opacity 往返
    std::vector<canvas::ImageLayer> layers_;                   // 叠加图片图层数组（声明态；**不含底图**；直接以数组承载）
    bool imageLayersVisible_{true};                            // 图片图层整体显隐（纯 GUI 运行态，不入配置；对标 layersBurnIn_）
    bool layersActive_{false};                               // 是否为图层画布（有图即真；仅空文档为假）
    bool layersBurnIn_{true};                                // 图片图层「烧录」导出开关（纯 GUI 运行态，对标标注 burnInEnabled；不入配置）
    int canvasWidth_{0};                                     // 声明画布宽（layersActive_ 下有效）
    int canvasHeight_{0};                                    // 声明画布高
    std::optional<int> selectedLayerId_;                     // 当前拾取的图片图层 id（nullopt＝无）
    int nextLayerId_{1};                                     // 递增图层 id 发号器（跨会话稳定标识）
    LayerSessionStore layerSessions_;                        // 每层预处理声明态 + 物化签名 + 物化位图（按 layerId，单点生命周期）

    engine::Tier mode_{engine::Tier::Standard};
    engine::Polarity polarity_{engine::Polarity::Keep};
    L1Shape l1Shape_{L1Shape::Rect};
    L2Sub l2Sub_{L2Sub::Cross};

    engine::RectRegion rect_{};
    bool hasRect_{false};
    std::vector<engine::RectRegion> rects_;  // L2 多矩形并集列表（其余子功能恒空）

    engine::ExportMode exportMode_{engine::ExportMode::Merged};
    engine::MergeLayout layout_{engine::MergeLayout::Collapse};
    engine::ExportFormat format_{engine::ExportFormat::Png};
    int quality_{nums::model::kDefaultExportQualityPercent};   // 出厂默认转引 Core（与导出面板 range 同源）。
    QString naming_{QString::fromUtf8(texts::ident::kNamingDefaultTemplate)};   // 命名模板初值与面板输入框初值同字。
    QString outputDir_;
    QString outputFile_;

    // 重排合并参数（FR-L3.7）：0 = 未指定（交 Core 自动推导），正值 = 用户显式指定。
    int mergeCols_{0};
    int mergeRows_{0};
    int mergeCellW_{0};
    int mergeCellH_{0};
    core::Color padColor_{core::kTransparent}; // 空位/余量填充色（默认透明）
    engine::MergeOrder mergeOrder_;                 // 重排填充顺序（默认行优先、不蛇形、不倒序）

    // L3 网格参数：默认单元 100×100（避免 GridParams 默认 1×1 在大图产生海量单元）。
    engine::GridParams grid_{0, 0, 100, 100, engine::RemainderPolicy::Discard};
    int gridRows_{0};                    // 派生行数（只读显示）
    int gridCols_{0};                    // 派生列数（只读显示）
    engine::SequenceParams order_;  // 排序策略（默认 row-major）
    std::vector<int> selectedCells_;     // L3 显式选择集（L1/L2 恒空，由 Core 自动推导）
};

} // namespace idc::gui
