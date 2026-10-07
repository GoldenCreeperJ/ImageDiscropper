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

namespace idc::gui {
namespace {

// 按钮样式：中性底 + 选中态蓝色高亮（与 LeftPanel 模式按钮观感一致）。
// 【深色模式】未选态背景为硬编码浅色（#f2f2f2），需显式指定深色文字，否则系统深色主题下白字浅底不可读。
auto kToolStyle =
    "QPushButton{padding:5px;border:1px solid #bbb;border-radius:4px;background:#f2f2f2;color:#1b1b1b;}"
    "QPushButton:checked{background:#3b7ddd;color:#fff;border-color:#3b7ddd;}";

// 交互模式清单：{模式枚举, 中文标签, 悬停提示}。顺序即面板排布顺序（网格 2 列）。
struct ModeEntry {
    CanvasMode mode;
    const char* label;
    const char* tip;
};

constexpr ModeEntry kModes[] = {
    {CanvasMode::Marquee,    "框选",     "橡皮筋框选生成选区 / 拖动选区框；标注与图片落位框不可拾取"},
    {CanvasMode::ImagePick,  "图片拾取", "在画布上点选 / 拖拽图片图层落位框调位（释放写回落位偏移）"},
    {CanvasMode::AnnoSelect, "标注选取", "点选 / 拖动 / 手柄变换既有标注；禁框选（切换即回到画布预处理目标）"},
    {CanvasMode::AnnoCreate, "标注绘制", "在下方选择形状后于画布绘制新标注"},
};

// 形状工具清单（「标注绘制」态子行）：{工具枚举, 中文标签, 悬停提示}。不含 SELECT（已上移为「标注选取」模式）。
struct ToolEntry {
    AnnoTool tool;
    const char* label;
    const char* tip;
};
const ToolEntry kTools[] = {
    {AnnoTool::RECT,           "矩形",        "两点拖拽绘制矩形；面板“圆角”>0 即得圆角矩形"},
    {AnnoTool::SQUARE,         "正方形",      "落点为一个顶点，拖拽定边长（取 max(宽,高)）画轴对齐正方形；面板“圆角”>0 即得圆角正方形"},
    {AnnoTool::RHOMBUS,        "菱形",        "起点为中心，拖拽绘制菱形"},
    {AnnoTool::CIRCLE,         "圆形",        "起点为圆心，拖拽绘制圆形"},
    {AnnoTool::ELLIPSE,        "椭圆",        "落点为中心，拖拽定两半轴（dx/dy）绘制椭圆"},
    {AnnoTool::ISOS_TRI,       "等腰三角形",  "起点为底边中点，拖拽绘制等腰三角形"},
    {AnnoTool::EQU_TRI,        "等边三角形",  "落点为底边中点，拖拽绘制底边水平、顶角朝光标生长的等边三角形"},
    {AnnoTool::RECT_TRI,       "直角三角形",  "落点为直角顶点，拖拽沿两轴定两直角边长（dx、dy）"},
    {AnnoTool::EQU_RECT_TRI,   "等腰直角三角形", "落点为直角顶点，拖拽定直角边长（取 max(宽,高)）画轴对齐等腰直角三角形"},
    {AnnoTool::LINE,           "直线",        "两点拖拽绘制直线段"},
    {AnnoTool::POLYLINE,       "多线段",      "连续单击追加顶点，Esc 或切换工具收笔"},
    {AnnoTool::TEXT,           "文字",        "单击落点，输入文字内容后添加"},
    {AnnoTool::BRUSH,          "画笔",        "按住拖动绘制自由路径，Esc 或切换工具收笔"},
};

} // namespace

// 构建面板：一个「交互模式」分组——顶部四态模式互斥单选（2 列网格），下方形状子行（3 列网格，仅绘制态可见）。
ToolPanel::ToolPanel(QWidget* parent) : QWidget(parent) {
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(8, 8, 8, 8);
    root->setSpacing(6);

    auto* box = new QGroupBox(QStringLiteral("交互模式"), this);
    auto* boxLay = new QVBoxLayout(box);
    boxLay->setSpacing(6);

    // ---- 顶部：四态互斥模式按钮（2 列网格）----
    auto* modeGrid = new QGridLayout();
    modeGrid->setSpacing(4);
    modeGroup_ = new QButtonGroup(this);
    modeGroup_->setExclusive(true);
    int mi = 0;
    for (const auto&[mode, label, tip] : kModes) {
        constexpr int cols = 2;
        auto* btn = new QPushButton(QString::fromUtf8(label), box);
        btn->setCheckable(true);
        btn->setStyleSheet(kToolStyle);
        btn->setToolTip(QString::fromUtf8(tip));
        modeGroup_->addButton(btn, static_cast<int>(mode));
        modeGrid->addWidget(btn, mi / cols, mi % cols);
        ++mi;
    }
    connect(modeGroup_, &QButtonGroup::idToggled, this, &ToolPanel::onModeToggled);
    boxLay->addLayout(modeGrid);

    // ---- 下方：形状子行（3 列网格；仅「标注绘制」态可见）----
    shapeArea_ = new QWidget(box);
    auto* shapeGrid = new QGridLayout(shapeArea_);
    shapeGrid->setSpacing(4);
    toolGroup_ = new QButtonGroup(this);
    toolGroup_->setExclusive(true);
    int i = 0;
    for (const auto&[tool, label, tip] : kTools) {
        constexpr int cols = 3;
        auto* btn = new QPushButton(QString::fromUtf8(label), shapeArea_);
        btn->setCheckable(true);
        btn->setStyleSheet(kToolStyle);
        btn->setToolTip(QString::fromUtf8(tip));
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
