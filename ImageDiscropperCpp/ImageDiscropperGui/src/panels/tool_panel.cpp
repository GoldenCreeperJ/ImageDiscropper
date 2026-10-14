// ============================================================================
// 文件：src/panels/tool_panel.cpp
// 作用：实现「交互模式」面板的构建与模型反向同步（见同名头文件说明）。
// 分块依据：模式清单与形状清单各集中为一张 {枚举, 标签, 提示} 表，构造时按表批量建按钮入互斥组；
//           顶部模式组（modeGroup_）四态互斥，形状子行（toolGroup_ + shapeArea_）仅「标注绘制」态展开；
//           槽函数只把选择翻译为 modeChanged / toolSelected 信号，不触碰任何几何（「分层纪律」）。
// ============================================================================
#include "panels/tool_panel.h"

#include <QButtonGroup>
#include <QGridLayout>
#include <QGroupBox>
#include <QPushButton>
#include <QVBoxLayout>

#include "util/gui_nums.h"
#include "i18n/gui_texts.h"
#include "i18n/gui_i18n.h"

namespace idc::gui {
namespace {

// 按钮样式：未选态底/字/边框走 palette(...) 令牌（随深/浅主题自动给正确字色，旧「硬编码浅底配深字」hack 已删），
// 选中态蓝色高亮（#3b7ddd 配白字）与 LeftPanel 模式按钮观感一致、两主题皆可读。
// 注：QSS palette() 角色名不带连字符（buttontext）。
auto kToolStyle =
    "QPushButton{padding:5px;border:1px solid palette(mid);border-radius:4px;background:palette(button);color:palette(buttontext);}"
    "QPushButton:checked{background:#3b7ddd;color:#fff;border-color:#3b7ddd;}";

// 交互模式清单：{模式枚举, 标签, 悬停提示}。顺序即面板排布顺序（网格 2 列）。
// 模式名与菜单「画布模式」子菜单同源（menu::kCanvas*）；悬停说明为本面板专属措辞。
// 表位存 IDC_GT（键 + 中文源值成对），按钮文案在构造循环里 i18n::qs() 现取——
// 构造期取值 = 一期「改语言重启生效」的语义，与就地 IDC_GTR 等价。
struct ModeEntry {
    CanvasMode mode;
    i18n::TextRef label;
    i18n::TextRef tip;
};

constexpr ModeEntry kModes[] = {
    {CanvasMode::Marquee,    IDC_GT(menu, kCanvasMarquee),    IDC_GT(tooltip, kModeMarqueeBtn)},
    {CanvasMode::ImagePick,  IDC_GT(menu, kCanvasImagePick),  IDC_GT(tooltip, kModeImagePickBtn)},
    {CanvasMode::AnnoSelect, IDC_GT(menu, kCanvasAnnoSelect), IDC_GT(tooltip, kModeAnnoSelectBtn)},
    {CanvasMode::AnnoCreate, IDC_GT(menu, kCanvasAnnoCreate), IDC_GT(tooltip, kModeAnnoCreateBtn)},
};

// 形状工具清单（「标注绘制」态子行）：{工具枚举, 标签, 悬停提示}。不含 SELECT（已上移为「标注选取」模式）。
// 形状名与菜单「标注→工具」同源（label::kTool*，面板为真相源）。
// 表序即网格入位序（3 列）：四个三角形（ISOS/EQU/RECT_TRI/EQU_RECT_TRI）刻意用 LINE/POLYLINE 等
// 非三角项分隔，使任一行不超过 2 个三角形（旧序 EQU_TRI/RECT_TRI/EQU_RECT_TRI 挤成同一行）。仅改表序，不动枚举值。
struct ToolEntry {
    AnnoTool tool;
    i18n::TextRef label;
    i18n::TextRef tip;
};
const ToolEntry kTools[] = {
    {AnnoTool::RECT,           IDC_GT(label, kToolRect),          IDC_GT(tooltip, kToolRectBtn)},
    {AnnoTool::SQUARE,         IDC_GT(label, kToolSquare),        IDC_GT(tooltip, kToolSquareBtn)},
    {AnnoTool::RHOMBUS,        IDC_GT(label, kToolRhombus),       IDC_GT(tooltip, kToolRhombusBtn)},
    {AnnoTool::ISOS_TRI,       IDC_GT(label, kToolIsoTriangle),   IDC_GT(tooltip, kToolIsoTriBtn)},
    {AnnoTool::CIRCLE,         IDC_GT(label, kToolCircle),        IDC_GT(tooltip, kToolCircleBtn)},
    {AnnoTool::ELLIPSE,        IDC_GT(label, kToolEllipse),       IDC_GT(tooltip, kToolEllipseBtn)},
    {AnnoTool::EQU_TRI,        IDC_GT(label, kToolEquTriangle),   IDC_GT(tooltip, kToolEquTriBtn)},
    {AnnoTool::LINE,           IDC_GT(label, kToolLine),          IDC_GT(tooltip, kToolLineBtn)},
    {AnnoTool::POLYLINE,       IDC_GT(label, kToolPolyline),      IDC_GT(tooltip, kToolPolylineBtn)},
    {AnnoTool::RECT_TRI,       IDC_GT(label, kToolRightTriangle), IDC_GT(tooltip, kToolRightTriBtn)},
    {AnnoTool::TEXT,           IDC_GT(label, kToolText),          IDC_GT(tooltip, kToolTextBtn)},
    {AnnoTool::BRUSH,          IDC_GT(label, kToolBrush),         IDC_GT(tooltip, kToolBrushBtn)},
    {AnnoTool::EQU_RECT_TRI,   IDC_GT(label, kToolIsoRightTri),   IDC_GT(tooltip, kToolIsoRightTriBtn)},
};

} // namespace

// 构建面板：一个「交互模式」分组——顶部四态模式互斥单选（2 列网格），下方形状子行（3 列网格，仅绘制态可见）。
ToolPanel::ToolPanel(QWidget* parent) : QWidget(parent) {
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(nums::panels::kPanelMarginPx, nums::panels::kPanelMarginPx,
                             nums::panels::kPanelMarginPx, nums::panels::kPanelMarginPx);
    root->setSpacing(nums::panels::kPanelSpacingPx);

    auto* box = new QGroupBox(this);
    i18n::bindTitle(retranslator_, box, IDC_GT(label, kInteractionModeBox));
    auto* boxLay = new QVBoxLayout(box);
    boxLay->setSpacing(nums::panels::kPanelSpacingPx);

    // ---- 顶部：四态互斥模式按钮（2 列网格）----
    auto* modeGrid = new QGridLayout();
    modeGrid->setSpacing(nums::panels::kPanelTightSpacingPx);
    modeGroup_ = new QButtonGroup(this);
    modeGroup_->setExclusive(true);
    int mi = 0;
    for (const auto&[mode, label, tip] : kModes) {
        constexpr int cols = 2;
        auto* btn = new QPushButton(box);
        i18n::bindButton(retranslator_, btn, label);
        btn->setCheckable(true);
        btn->setStyleSheet(kToolStyle);
        i18n::bindToolTip(retranslator_, btn, tip);
        modeGroup_->addButton(btn, static_cast<int>(mode));
        modeGrid->addWidget(btn, mi / cols, mi % cols);
        ++mi;
    }
    connect(modeGroup_, &QButtonGroup::idToggled, this, &ToolPanel::onModeToggled);
    boxLay->addLayout(modeGrid);

    // ---- 下方：形状子行（3 列网格；仅「标注绘制」态可见）----
    shapeArea_ = new QWidget(box);
    auto* shapeGrid = new QGridLayout(shapeArea_);
    shapeGrid->setSpacing(nums::panels::kPanelTightSpacingPx);
    toolGroup_ = new QButtonGroup(this);
    toolGroup_->setExclusive(true);
    int i = 0;
    for (const auto&[tool, label, tip] : kTools) {
        constexpr int cols = 3;
        auto* btn = new QPushButton(shapeArea_);
        i18n::bindButton(retranslator_, btn, label);
        btn->setCheckable(true);
        btn->setStyleSheet(kToolStyle);
        i18n::bindToolTip(retranslator_, btn, tip);
        toolGroup_->addButton(btn, static_cast<int>(tool));
        shapeGrid->addWidget(btn, i / cols, i % cols);
        ++i;
    }
    connect(toolGroup_, &QButtonGroup::idToggled, this, &ToolPanel::onToolToggled);
    boxLay->addWidget(shapeArea_);

    root->addWidget(box);
    root->addStretch(1);

    // 默认勾选「框选」态；形状子行随之隐藏。
    if (auto* b = modeGroup_->button(static_cast<int>(CanvasMode::Marquee))) b->setChecked(true);
    shapeArea_->setVisible(false);
}

void ToolPanel::setModel(AnnotationBridge* model) {
    model_ = model;
    syncFromModel();
}

// 从模型反向同步当前形状工具选中态（SELECT 无对应按钮 → 子行不勾选任何项）。
void ToolPanel::syncFromModel() const {
    if (!model_ || !toolGroup_) return;
    toolGroup_->blockSignals(true);
    if (auto* b = toolGroup_->button(static_cast<int>(model_->currentTool()))) b->setChecked(true);
    else if (auto* none = toolGroup_->checkedButton()) none->setChecked(false);
    toolGroup_->blockSignals(false);
}

void ToolPanel::setShapeRowVisible(const bool visible) const {
    if (shapeArea_) shapeArea_->setVisible(visible);
}

// 编程式切换模式：勾选对应按钮→走与用户点击同一 idToggled 路径（发 modeChanged + 子行显隐）。
void ToolPanel::setMode(const CanvasMode mode) const {
    if (auto* b = modeGroup_->button(static_cast<int>(mode))) b->setChecked(true);
}

// 模式按钮切换 → 发 modeChanged（仅选中时上报），并联动形状子行显隐（仅「标注绘制」展开）。
void ToolPanel::onModeToggled(const int id, const bool checked) {
    if (!checked) return;
    const auto mode = static_cast<CanvasMode>(id);
    if (shapeArea_) shapeArea_->setVisible(mode == CanvasMode::AnnoCreate);
    emit modeChanged(mode);
}

// 形状工具切换 → 发 toolSelected（仅在选中时上报，避免互斥组取消信号重复触发）。
void ToolPanel::onToolToggled(const int id, const bool checked) {
    if (!checked) return;
    emit toolSelected(static_cast<AnnoTool>(id));
}

} // namespace idc::gui
