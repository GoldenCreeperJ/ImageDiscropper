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

#include <optional>
#include <vector>

#include "core/color.h"
#include "core/image.h"
#include "engine/engine.h"
#include "preprocess/preprocess_pipeline.h"

namespace idc::gui {

// L1 标准提取的形状选择（映射到 Core 的 CutGenerator）。
enum class L1Shape {
    RECT,   // 矩形选框 → CutGenerator::RECT
    HBAND,  // 横带     → CutGenerator::HORIZONTAL_LINE
    VBAND,  // 竖带     → CutGenerator::VERTICAL_LINE
};

// L2 反向剔除的子功能（映射到 Core 的 CutGenerator）。
enum class L2Sub {
    CROSS,       // 十字切割（单矩形诱导贯穿十字带）→ CutGenerator::RECT
    HLINE,       // 横线切割 → CutGenerator::HORIZONTAL_LINE
    VLINE,       // 竖线切割 → CutGenerator::VERTICAL_LINE
    MULTI_RECT,  // 多矩形并集剔除（各矩形诱导十字带取并集）→ CutGenerator::MULTI_RECT
};

// 独立画布尺寸（Resize）目标：对原图设定的绝对宽高（像素）。
// 不再作为可排序队列步骤，而是常驻独立设置；恒在轻操作队列之前重放（见 preprocess_controller.cpp）。
struct ResizeTarget {
    int width = 0;
    int height = 0;
};

// ---------------------------------------------------------------------------
// Document：会话状态单一真相源（QObject，便于信号驱动刷新）。
// ---------------------------------------------------------------------------
class Document : public QObject {
    Q_OBJECT
public:
    explicit Document(QObject* parent = nullptr);

    // ---- 图像 ----
    // 载入新图像：original_ 与 working_ 均置为 img（新图无预处理，工作图=原图），
    // 记录路径、清空选区，发 imageChanged()。
    void setImage(core::Image img, QString path);
    const core::Image& original() const { return original_; }
    const core::Image& working() const { return working_; }
    bool hasImage() const { return !working_.empty(); }
    int width() const { return working_.width(); }
    int height() const { return working_.height(); }
    const QString& imagePath() const { return imagePath_; }

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
    bool hasPreprocess() const { return !steps_.empty() || resize_.has_value(); }

    // ---- 独立画布尺寸（Resize）----
    // resize_ 为可选目标尺寸；设置后由 PreprocessController 重放（恒在轻操作之前、从原图重采样）。
    bool hasResize() const { return resize_.has_value(); }
    std::optional<ResizeTarget> resizeTarget() const { return resize_; }
    // 覆盖 / 清除独立 resize（std::nullopt = 无 resize）。仅存状态，不物化、不发信号。
    void setResize(std::optional<ResizeTarget> target);

    // ---- 声明式预处理队列（steps_）----
    // 只读访问当前队列（供面板渲染步骤列表、控制器克隆编辑）。
    const preprocess::PreprocessPipeline& steps() const { return steps_; }
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
    bool hasBase() const { return base_.has_value(); }
    const core::Image& base() const { return *base_; }
    const preprocess::PreprocessPipeline& basePrefix() const { return basePath_; }
    // 记录一段前缀的重放产物为 base_（img 为其物化结果，prefix 为其对应的重放前缀队列）。
    void setBase(core::Image img, preprocess::PreprocessPipeline prefix);
    // 释放 base_ 重放缓存（由「有 resize」切到「无 resize」时回收内存；与 setImage / resetPreprocess 的失效同处理）。
    void clearBase() { base_.reset(); basePath_.clear(); }

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
    void setL2Sub(L2Sub s);

    // ---- 切割几何（选区矩形，原图像素坐标，左闭右开）----
    const engine::RectRegion& rect() const { return rect_; }
    bool hasRect() const { return hasRect_; }
    void setRect(const engine::RectRegion& r);     // 设置选区并置 hasRect_
    void clearRect();                                   // 清除选区（Esc / 右键清除）

    // ---- L2 多矩形并集（FR-L2 多矩形；仅 L2Sub::MULTI_RECT 生效）----
    // rects_ 为用户框选追加的多个矩形，Core 对每个矩形诱导贯穿十字带后取并集剔除。
    // GUI 只维护矩形列表（增/删/改/清空）并搬运进 CutConfig::rects，不做任何并集/几何计算（CONTRIBUTING.md「分层纪律」）。
    const std::vector<engine::RectRegion>& rects() const { return rects_; }
    void addRect(const engine::RectRegion& r);                 // 追加一个矩形（拒绝退化）
    void updateRect(std::size_t index, const engine::RectRegion& r); // 修改指定矩形（拒绝退化）
    void removeRect(std::size_t index);                             // 删除指定矩形
    void clearRects();                                              // 清空多矩形列表

    // ---- 导出参数（CompositionParams 的 GUI 侧映射）----
    engine::EmitMode emitMode() const { return emitMode_; }
    engine::MergeLayout layout() const { return layout_; }
    void setEmit(engine::EmitMode mode, engine::MergeLayout layout);
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

    // ---- 重排合并参数（FR-L3.7 / SPEC §4.3；仅 MERGED+REARRANGE 生效）----
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

    // ---- 重排填充顺序（MergeOrder；仅 MERGED+REARRANGE 生效，与 L3 选择排序 order_ 正交）----
    // order_ 决定「保留块的先后列表」（含 CUSTOM 拖拽序）；mergeOrder_ 决定「该列表以行/列优先
    // + 蛇形? + 倒序? 的路径铺进 cols×rows 输出画布」。GUI 只搬运字段，落位由 Core compose 计算（CONTRIBUTING.md「分层纪律」）。
    const engine::MergeOrder& mergeOrder() const { return mergeOrder_; }
    void setMergeOrderStrategy(engine::SortStrategy s); // 仅 ROW_MAJOR / COLUMN_MAJOR（CUSTOM 按行优先）
    void setMergeOrderReverse(bool on);                      // 填充路径整体倒序
    void setMergeOrderSnake(bool on);                        // 填充路径蛇形

    // ---- L3 网格参数（FR-L3.1 网格定义 / FR-L3.2 余量策略）----
    // 网格仅由「基准点 + 单元尺寸 + 余量策略」定义；行列数由 Core 依图像边界自动推导。
    const engine::GridParams& gridParams() const { return grid_; }
    void setGridOrigin(int x, int y);                  // 基准点 (x0,y0)
    void setCellSize(int w, int h);                    // 单元尺寸 (cellWidth,cellHeight)
    void setRemainder(engine::RemainderPolicy p); // 余量策略 discard/keep-partial/pad
    // 派生行列数（只读显示，由 MainWindow 依 Core Grid 结果回灌；不触发 changed 以免回环）。
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
    void moveCellOrder(int from, int to);           // CUSTOM 拖拽调序：把 from 单元移到 to 单元原序位

    // ---- L3 排序（FR-L3.5：row-major / column-major / reverse / snake / custom）----
    const engine::SequenceParams& order() const { return order_; }
    void setSortStrategy(engine::SortStrategy s);
    void setSortReverse(bool on);
    void setSortSnake(bool on);

    // ---- 组装 Core 配置（纯字段搬运 + 枚举映射，无引擎逻辑）----
    // 依当前模式/子选项构建切割配置。
    engine::CutConfig buildCutConfig() const;
    // 构建一次完整作业的配置（供 runEngine / exportImage 使用）。
    // 约定不变：引擎永远收到「已预处理的工作图 + 空 preprocess」——预处理已物化进 working_，
    // 故此处 preprocess 恒空，runEngine 不会二次应用（结构性根除双应用）。
    engine::EngineConfig buildEngineConfig() const;
    // 构建可序列化配置（保存 / 载入用，SPEC §7）：与 buildEngineConfig 同字段，但
    // preprocess 填入完整声明队列 steps_、source 取原图尺寸——还原端从 original 重放整链即可复现 working_。
    // 与 buildEngineConfig 的关键差异只在 preprocess 段：运行配置恒空、序列化配置携带完整管线。
    engine::EngineConfig buildSerializableConfig() const;
    // 反向映射（配置加载与撤销重做共用）：把一份 EngineConfig 的字段搬回 Document 状态，
    // 与 buildEngineConfig 互逆——生成器+tier 还原 L1 形状/L2 子功能，搬运几何/选择集/排序/导出参数。
    // 不还原 source 尺寸（图像不随配置/快照改变）；直接改字段后只发一次 changed()（避免逐 setter 多次刷新）。
    // 维持模型不变式：非 L3 不得为重排（复位为坍缩）、网格单元尺寸非正时回退默认，杜绝非法组合。
    void applyEngineConfig(const engine::EngineConfig& cfg);

signals:
    // 换图：需重建预览 pixmap、重置视图。
    void imageChanged();
    // 任意参数变更：仅需刷新预览与状态栏（不重建底图）。
    void changed();

private:
    core::Image original_;   // 原始载入图（RGBA）——始终不变，供队列重放与「重置预处理」还原
    core::Image working_;    // 切割引擎实际输入图（= steps_ 重放到 original 的物化结果，无预处理时等于原图）
    preprocess::PreprocessPipeline steps_;  // 声明式轻操作队列（绝不含 ResizeOp），几何签名与序列化的真相源
    std::optional<ResizeTarget> resize_;     // 独立画布尺寸目标（可选，恒最先重放）
    // base_ 重放缓存：独立 resize 作用于 original 的物化产物；basePath_ 为其 [resize?] 前缀，供失效比对。
    std::optional<core::Image> base_;
    preprocess::PreprocessPipeline basePath_;
    QString imagePath_;

    engine::Tier mode_{engine::Tier::L1};
    engine::Polarity polarity_{engine::Polarity::KEEP};
    L1Shape l1Shape_{L1Shape::RECT};
    L2Sub l2Sub_{L2Sub::CROSS};

    engine::RectRegion rect_{};
    bool hasRect_{false};
    std::vector<engine::RectRegion> rects_;  // L2 多矩形并集列表（其余子功能恒空）

    engine::EmitMode emitMode_{engine::EmitMode::MERGED};
    engine::MergeLayout layout_{engine::MergeLayout::COLLAPSE};
    engine::ExportFormat format_{engine::ExportFormat::PNG};
    int quality_{90};
    QString naming_{QStringLiteral("{name}_{index:03d}")};
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
    engine::GridParams grid_{0, 0, 100, 100, engine::RemainderPolicy::DISCARD};
    int gridRows_{0};                    // 派生行数（只读显示）
    int gridCols_{0};                    // 派生列数（只读显示）
    engine::SequenceParams order_;  // 排序策略（默认 row-major）
    std::vector<int> selectedCells_;     // L3 显式选择集（L1/L2 恒空，由 Core 自动推导）
};

} // namespace idc::gui
