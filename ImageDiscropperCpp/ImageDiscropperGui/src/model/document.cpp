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

#include "model/config_codec.h"

namespace idc::gui {

// 构造：无图像、默认 L1 + KEEP。
Document::Document(QObject* parent) : QObject(parent) {}
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
    emit modeOrPolarityChanged();
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
    emit modeOrPolarityChanged();
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


// 配置组装/还原：实现已拆至 ConfigCodec（model/config_codec），此处为保持公有 API 形状的一行转发。
engine::EngineConfig Document::buildEngineConfig() const {
    return ConfigCodec::buildEngineConfig(*this);
}

engine::EngineConfig Document::buildSerializableConfig() const {
    return ConfigCodec::buildSerializableConfig(*this);
}

void Document::applyEngineConfig(const engine::EngineConfig& cfg) {
    ConfigCodec::applyEngineConfig(*this, cfg);
}

} // namespace idc::gui
