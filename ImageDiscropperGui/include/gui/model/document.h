// ============================================================================
// 文件：model/document.h
// 作用：GUI 会话状态的「单一真相源」——持有原图 / 工作图、当前模式(L1/L2/L3)、
//       极性、切割几何参数、选择集与导出参数，并负责把它们组装成 Core 的
//       EngineConfig。任何面板/画布都只读写 Document，不各自持有真相，避免耦合。
// 分块依据：
//   - Document 只做「状态存储 + 参数翻译」，绝不含切割/几何/排序/极性判定逻辑
//     （那些全在 Core；A-0.1）。组装 EngineConfig 只是字段搬运与枚举映射。
//   - 真正调用 Core 的动作集中在 EngineBridge（唯一触 Core 处），Document 不碰 Core API。
// 说明：状态变更统一发 changed() 信号；换图发 imageChanged()（需重建预览 pixmap）。
//       MainWindow 监听这两个信号驱动预览刷新与面板同步。
// ============================================================================
#pragma once

#include <QString>
#include <QObject>

#include <vector>

#include "core/color.h"
#include "core/image.h"
#include "engine/engine.h"

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

// ---------------------------------------------------------------------------
// Document：会话状态单一真相源（QObject，便于信号驱动刷新）。
// ---------------------------------------------------------------------------
class Document : public QObject {
    Q_OBJECT
public:
    explicit Document(QObject* parent = nullptr);

    // ---- 图像 ----
    // 载入新图像：original_ 与 working_ 均置为 img（第一阶段无预处理，工作图=原图），
    // 记录路径、清空选区，发 imageChanged()。
    void setImage(idc::core::Image img, QString path);
    const idc::core::Image& original() const { return original_; }
    const idc::core::Image& working() const { return working_; }
    bool hasImage() const { return !working_.empty(); }
    int width() const { return working_.width(); }
    int height() const { return working_.height(); }
    // 工作图是否为灰度（GRAY）——黑白后色道分离无意义、黑白本身幂等，供 GUI 禁用相关控件（反色对灰度仍有效）。
    bool isWorkingGray() const { return working_.isGray(); }
    const QString& imagePath() const { return imagePath_; }

    // ---- 预处理工作图（FR-1）----
    // 用一次预处理结果替换工作图；original_ 始终保留原图，供「重置预处理」还原。
    // 由 MainWindow 经 EngineBridge 算出新图后写回（Document 不碰 Core，A-0.1）；触发 imageChanged()。
    void setWorkingImage(idc::core::Image img);
    // 重置预处理：工作图恢复为原图、清除「已预处理」标记；未预处理时早退。触发 imageChanged()。
    void resetPreprocess();
    // 当前工作图是否已被预处理（与原图不同）——供面板/菜单启用「重置预处理」。
    bool hasPreprocess() const { return preprocessed_; }

    // ---- 模式与极性 ----
    idc::engine::Tier mode() const { return mode_; }
    void setMode(idc::engine::Tier t);                 // 切换模式并套用该模式的默认极性
    idc::engine::Polarity polarity() const { return polarity_; }
    // 切换极性；L1/L2 与极性一一耦合（keep→L1、remove→L2）会同步改模式，L3 极性独立不改模式。
    void setPolarity(idc::engine::Polarity p);

    // ---- L1 / L2 子选项 ----
    L1Shape l1Shape() const { return l1Shape_; }
    void setL1Shape(L1Shape s);
    L2Sub l2Sub() const { return l2Sub_; }
    void setL2Sub(L2Sub s);

    // ---- 切割几何（选区矩形，原图像素坐标，左闭右开）----
    const idc::engine::RectRegion& rect() const { return rect_; }
    bool hasRect() const { return hasRect_; }
    void setRect(const idc::engine::RectRegion& r);     // 设置选区并置 hasRect_
    void clearRect();                                   // 清除选区（Esc / 右键清除）

    // ---- L2 多矩形并集（FR-L2 多矩形；仅 L2Sub::MULTI_RECT 生效）----
    // rects_ 为用户框选追加的多个矩形，Core 对每个矩形诱导贯穿十字带后取并集剔除。
    // GUI 只维护矩形列表（增/删/改/清空）并搬运进 CutConfig::rects，不做任何并集/几何计算（A-0.1）。
    const std::vector<idc::engine::RectRegion>& rects() const { return rects_; }
    void addRect(const idc::engine::RectRegion& r);                 // 追加一个矩形（拒绝退化）
    void updateRect(std::size_t index, const idc::engine::RectRegion& r); // 修改指定矩形（拒绝退化）
    void removeRect(std::size_t index);                             // 删除指定矩形
    void clearRects();                                              // 清空多矩形列表

    // ---- 导出参数（CompositionParams 的 GUI 侧映射）----
    idc::engine::EmitMode emitMode() const { return emitMode_; }
    idc::engine::MergeLayout layout() const { return layout_; }
    void setEmit(idc::engine::EmitMode mode, idc::engine::MergeLayout layout);
    idc::engine::ExportFormat format() const { return format_; }
    void setFormat(idc::engine::ExportFormat f);
    int quality() const { return quality_; }
    void setQuality(int q);
    const QString& naming() const { return naming_; }
    void setNaming(const QString& n);
    const QString& outputDir() const { return outputDir_; }
    void setOutputDir(const QString& d);
    const QString& outputFile() const { return outputFile_; }
    void setOutputFile(const QString& f);

    // ---- 重排合并参数（FR-L3.7 / §5.3；仅 MERGED+REARRANGE 生效）----
    // cols/rows/cellWidth/cellHeight 以 0 表示「未指定」→ buildEngineConfig 保持 nullopt，
    // 交 Core compose 依保留块数自动推导画布；正值才写入。padColor 为空位/余量填充色。
    // GUI 只搬运字段，画布尺寸/落位全由 Core compose 计算（A-0.1）。
    int mergeCols() const { return mergeCols_; }
    int mergeRows() const { return mergeRows_; }
    int mergeCellW() const { return mergeCellW_; }
    int mergeCellH() const { return mergeCellH_; }
    void setMergeGrid(int cols, int rows);   // 重排画布列/行（0=自动推导）
    void setMergeCellSize(int w, int h);     // 重排单元宽/高（0=用保留块原尺寸）
    idc::core::Color padColor() const { return padColor_; }
    void setPadColor(idc::core::Color c);    // 空位/余量填充色（默认透明）

    // ---- 重排填充顺序（MergeOrder；仅 MERGED+REARRANGE 生效，与 L3 选择排序 order_ 正交）----
    // order_ 决定「保留块的先后列表」（含 CUSTOM 拖拽序）；mergeOrder_ 决定「该列表以行/列优先
    // + 蛇形? + 倒序? 的路径铺进 cols×rows 输出画布」。GUI 只搬运字段，落位由 Core compose 计算（A-0.1）。
    const idc::engine::MergeOrder& mergeOrder() const { return mergeOrder_; }
    void setMergeOrderStrategy(idc::engine::SortStrategy s); // 仅 ROW_MAJOR / COLUMN_MAJOR（CUSTOM 按行优先）
    void setMergeOrderReverse(bool on);                      // 填充路径整体倒序
    void setMergeOrderSnake(bool on);                        // 填充路径蛇形

    // ---- L3 网格参数（FR-L3.1 网格定义 / FR-L3.2 余量策略）----
    // 网格仅由「基准点 + 单元尺寸 + 余量策略」定义；行列数由 Core 依图像边界自动推导。
    const idc::engine::GridParams& gridParams() const { return grid_; }
    void setGridOrigin(int x, int y);                  // 基准点 (x0,y0)
    void setCellSize(int w, int h);                    // 单元尺寸 (cellWidth,cellHeight)
    void setRemainder(idc::engine::RemainderPolicy p); // 余量策略 discard/keep-partial/pad
    // 派生行列数（只读显示，由 MainWindow 依 Core Grid 结果回灌；不触发 changed 以免回环）。
    int gridRows() const { return gridRows_; }
    int gridCols() const { return gridCols_; }
    void setDerivedGridSize(int rows, int cols);
    // 「转为网格模式编辑」（FR §4.4.3 / G-15）：把当前矩形选区一键送入 L3——
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
    const idc::engine::SequenceParams& order() const { return order_; }
    void setSortStrategy(idc::engine::SortStrategy s);
    void setSortReverse(bool on);
    void setSortSnake(bool on);

    // ---- 组装 Core 配置（纯字段搬运 + 枚举映射，无引擎逻辑）----
    // 依当前模式/子选项构建切割配置。
    idc::engine::CutConfig buildCutConfig() const;
    // 构建一次完整作业的配置（供 runEngine / exportImage 使用）。
    idc::engine::EngineConfig buildEngineConfig() const;

signals:
    // 换图：需重建预览 pixmap、重置视图。
    void imageChanged();
    // 任意参数变更：仅需刷新预览与状态栏（不重建底图）。
    void changed();

private:
    idc::core::Image original_;   // 原始载入图（RGBA）——始终不变，供「重置预处理」还原
    idc::core::Image working_;    // 切割引擎实际输入图（= 预处理累积后的工作图，无预处理时等于原图）
    bool preprocessed_{false};    // 工作图是否已被预处理（与 original_ 不同）
    QString imagePath_;

    idc::engine::Tier mode_{idc::engine::Tier::L1};
    idc::engine::Polarity polarity_{idc::engine::Polarity::KEEP};
    L1Shape l1Shape_{L1Shape::RECT};
    L2Sub l2Sub_{L2Sub::CROSS};

    idc::engine::RectRegion rect_{};
    bool hasRect_{false};
    std::vector<idc::engine::RectRegion> rects_;  // L2 多矩形并集列表（其余子功能恒空）

    idc::engine::EmitMode emitMode_{idc::engine::EmitMode::MERGED};
    idc::engine::MergeLayout layout_{idc::engine::MergeLayout::COLLAPSE};
    idc::engine::ExportFormat format_{idc::engine::ExportFormat::PNG};
    int quality_{90};
    QString naming_{QStringLiteral("{name}_{index:03d}")};
    QString outputDir_;
    QString outputFile_;

    // 重排合并参数（FR-L3.7）：0 = 未指定（交 Core 自动推导），正值 = 用户显式指定。
    int mergeCols_{0};
    int mergeRows_{0};
    int mergeCellW_{0};
    int mergeCellH_{0};
    idc::core::Color padColor_{idc::core::kTransparent}; // 空位/余量填充色（默认透明）
    idc::engine::MergeOrder mergeOrder_;                 // 重排填充顺序（默认行优先、不蛇形、不倒序）

    // L3 网格参数：默认单元 100×100（避免 GridParams 默认 1×1 在大图产生海量单元）。
    idc::engine::GridParams grid_{0, 0, 100, 100, idc::engine::RemainderPolicy::DISCARD};
    int gridRows_{0};                    // 派生行数（只读显示）
    int gridCols_{0};                    // 派生列数（只读显示）
    idc::engine::SequenceParams order_;  // 排序策略（默认 row-major）
    std::vector<int> selectedCells_;     // L3 显式选择集（L1/L2 恒空，由 Core 自动推导）
};

} // namespace idc::gui
