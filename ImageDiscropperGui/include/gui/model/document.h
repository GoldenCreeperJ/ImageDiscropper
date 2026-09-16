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

// L2 反向剔除的子功能（映射到 Core 的 CutGenerator）。多矩形并集在第二阶段接入。
enum class L2Sub {
    CROSS,  // 十字切割（单矩形诱导贯穿十字带）→ CutGenerator::RECT
    HLINE,  // 横线切割 → CutGenerator::HORIZONTAL_LINE
    VLINE,  // 竖线切割 → CutGenerator::VERTICAL_LINE
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
    const QString& imagePath() const { return imagePath_; }

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

    // ---- 选择集（第一阶段留空，L1/L2 由 Core 依生成器自动推导）----
    const std::vector<int>& selectedCells() const { return selectedCells_; }

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
    idc::core::Image original_;   // 原始载入图（RGBA）
    idc::core::Image working_;    // 切割引擎实际输入图（第一阶段 = 原图）
    QString imagePath_;

    idc::engine::Tier mode_{idc::engine::Tier::L1};
    idc::engine::Polarity polarity_{idc::engine::Polarity::KEEP};
    L1Shape l1Shape_{L1Shape::RECT};
    L2Sub l2Sub_{L2Sub::CROSS};

    idc::engine::RectRegion rect_{};
    bool hasRect_{false};

    idc::engine::EmitMode emitMode_{idc::engine::EmitMode::MERGED};
    idc::engine::MergeLayout layout_{idc::engine::MergeLayout::COLLAPSE};
    idc::engine::ExportFormat format_{idc::engine::ExportFormat::PNG};
    int quality_{90};
    QString naming_{QStringLiteral("{name}_{index:03d}")};
    QString outputDir_;
    QString outputFile_;

    std::vector<int> selectedCells_; // 第一阶段恒空
};

} // namespace idc::gui
