// ============================================================================
// 文件：model/document.cpp
// 作用：实现 Document 的状态存取与 EngineConfig 组装（见同名头文件说明）。
// 分块依据：setter 一律「改值 → 发对应信号」，把刷新时机交给 MainWindow；
//           buildCutConfig / buildEngineConfig 只做字段搬运与枚举映射（无引擎逻辑）。
// ============================================================================
#include "model/document.h"

#include <utility>

namespace idc::gui {

// 构造：无图像、默认 L1 + KEEP。
Document::Document(QObject* parent) : QObject(parent) {}

// 载入新图像：置原图与工作图，记录路径，清空选区。
void Document::setImage(idc::core::Image img, QString path) {
    original_ = img;             // 保留一份原始副本（供后续「重置预处理」用）。
    working_ = std::move(img);   // 第一阶段无预处理，工作图即原图。
    imagePath_ = std::move(path);
    hasRect_ = false;
    rect_ = idc::engine::RectRegion{};
    emit imageChanged();
}

// 切换模式：套用该模式的默认极性（L1/L3→keep，L2→remove），并发变更信号。
void Document::setMode(const idc::engine::Tier t) {
    if (mode_ == t) return;
    mode_ = t;
    // 模式默认极性：反向剔除(L2)默认删除框内，其余默认保留框内。
    polarity_ = (t == idc::engine::Tier::L2) ? idc::engine::Polarity::REMOVE
                                             : idc::engine::Polarity::KEEP;
    emit changed();
}

// 设置极性（keep/remove），实时驱动遮罩刷新（NFR-6）。
// 语义耦合：L1（标准提取）＝保留框内(keep)、L2（反向剔除）＝删除框内(remove)，二者本质是
// 同一交互的两种极性表述，故在 L1/L2 下切换极性会同步切换模式（keep→L1、remove→L2）；
// L3（网格）的极性独立于模式，切换极性不改变 L3。
void Document::setPolarity(const idc::engine::Polarity p) {
    if (polarity_ == p) return;
    polarity_ = p;
    // L1/L2 与极性一一对应：切换极性即切换模式；L3 极性独立，模式保持不变。
    if (mode_ == idc::engine::Tier::L1 || mode_ == idc::engine::Tier::L2) {
        mode_ = (p == idc::engine::Polarity::REMOVE) ? idc::engine::Tier::L2
                                                     : idc::engine::Tier::L1;
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
// 故在此统一兜底（所有写回路径共用），退化输入时保持上一次有效选区不变（A-0.1 输入校验）。
void Document::setRect(const idc::engine::RectRegion& r) {
    if (r.width() <= 0 || r.height() <= 0) return;
    rect_ = r;
    hasRect_ = true;
    emit changed();
}

// 清除选区。
void Document::clearRect() {
    if (!hasRect_) return;
    hasRect_ = false;
    rect_ = idc::engine::RectRegion{};
    emit changed();
}

// 设置导出模式与布局（分离 / 坍缩 / 重排）。
void Document::setEmit(const idc::engine::EmitMode mode, const idc::engine::MergeLayout layout) {
    if (emitMode_ == mode && layout_ == layout) return;
    emitMode_ = mode;
    layout_ = layout;
    emit changed();
}

// 设置导出格式。
void Document::setFormat(const idc::engine::ExportFormat f) {
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

// 依当前模式/子选项构建切割配置（枚举映射，无几何计算）。
idc::engine::CutConfig Document::buildCutConfig() const {
    idc::engine::CutConfig c;
    c.tier = mode_;
    c.polarity = polarity_;
    c.rect = rect_;

    // 生成器映射：L1 形状 / L2 子功能 → Core CutGenerator；L3 → GRID（第二阶段细化）。
    switch (mode_) {
        case idc::engine::Tier::L1:
            switch (l1Shape_) {
                case L1Shape::RECT:  c.generator = idc::engine::CutGenerator::RECT; break;
                case L1Shape::HBAND: c.generator = idc::engine::CutGenerator::HORIZONTAL_LINE; break;
                case L1Shape::VBAND: c.generator = idc::engine::CutGenerator::VERTICAL_LINE; break;
            }
            break;
        case idc::engine::Tier::L2:
            switch (l2Sub_) {
                case L2Sub::CROSS: c.generator = idc::engine::CutGenerator::RECT; break;
                case L2Sub::HLINE: c.generator = idc::engine::CutGenerator::HORIZONTAL_LINE; break;
                case L2Sub::VLINE: c.generator = idc::engine::CutGenerator::VERTICAL_LINE; break;
            }
            break;
        case idc::engine::Tier::L3:
            c.generator = idc::engine::CutGenerator::GRID;
            break;
    }
    return c;
}

// 构建一次完整作业的配置（供 runEngine / exportImage 使用）。
idc::engine::EngineConfig Document::buildEngineConfig() const {
    idc::engine::EngineConfig cfg;

    // 源尺寸：以工作图实际尺寸为准（第一阶段无预处理，等于原图尺寸）。
    cfg.source.width = width();
    cfg.source.height = height();

    // 切割配置（含模式/极性/几何）。
    cfg.cut = buildCutConfig();

    // 显式选择集（第一阶段空 → 由 Core 依生成器自动推导）。
    cfg.selectedCells = selectedCells_;

    // 排序策略：第一阶段用默认 row-major（L3 排序面板在第二阶段接入）。
    cfg.order = idc::engine::SequenceParams{};

    // 导出参数：把 GUI 侧字段搬运进 CompositionParams。
    cfg.emitParams.mode = emitMode_;
    cfg.emitParams.layout = layout_;
    cfg.emitParams.format = format_;
    cfg.emitParams.quality = quality_;
    cfg.emitParams.naming = naming_.toStdString();
    cfg.emitParams.padColor = idc::core::kTransparent;
    // cols/rows/cellWidth/cellHeight 保持 nullopt（重排画布参数在第二阶段接入）。

    // preprocess 保持默认空流水线（预处理面板在第三阶段接入）。
    return cfg;
}

} // namespace idc::gui
