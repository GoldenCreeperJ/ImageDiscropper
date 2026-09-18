// ============================================================================
// 文件：src/panels/tool_panel.cpp
// 作用：实现标注工具面板的构建与模型双向同步（见同名头文件说明）。
// 分块依据：工具清单集中为一张 {工具, 标签, 提示} 表，构造时按表批量建按钮入互斥组；
//           槽函数只把选择翻译为 toolSelected 信号，不触碰任何几何（CONTRIBUTING.md「分层纪律」）。
// ============================================================================
#include "panels/tool_panel.h"

#include <QButtonGroup>
#include <QGridLayout>
#include <QGroupBox>
#include <QPushButton>
#include <QVBoxLayout>

namespace idc::gui {
namespace {

// 工具按钮样式：中性底 + 选中态蓝色高亮（与 LeftPanel 模式按钮观感一致）。
// 【深色模式】未选态背景为硬编码浅色（#f2f2f2），需显式指定深色文字，否则系统深色主题下白字浅底不可读。
auto kToolStyle =
    "QPushButton{padding:5px;border:1px solid #bbb;border-radius:4px;background:#f2f2f2;color:#1b1b1b;}"
    "QPushButton:checked{background:#3b7ddd;color:#fff;border-color:#3b7ddd;}";

// 工具清单：{工具枚举, 中文标签, 悬停提示}。顺序即面板排布顺序（网格 3 列）。
struct ToolEntry {
    AnnoTool tool;
    const char* label;
    const char* tip;
};
const ToolEntry kTools[] = {
    {AnnoTool::SELECT,         "选择/移动",   "选择并拖动已有标注；Delete 删除，Ctrl+Z/Y 撤销重做"},
    {AnnoTool::RECT,           "矩形",        "两点拖拽绘制矩形"},
    {AnnoTool::SQUARE,         "正方形",      "两点拖拽绘制正方形"},
    {AnnoTool::RHOMBUS,        "菱形",        "起点为中心，拖拽绘制菱形"},
    {AnnoTool::CIRCLE,         "圆形",        "起点为圆心，拖拽绘制圆形"},
    {AnnoTool::ELLIPSE,        "椭圆",        "拖拽出外接矩形绘制椭圆"},
    {AnnoTool::ROUNDRECT,      "圆角矩形",    "两点拖拽绘制圆角矩形"},
    {AnnoTool::ISOS_TRI,       "等腰三角形",  "起点为底边中点，拖拽绘制等腰三角形"},
    {AnnoTool::EQU_TRI,        "等边三角形",  "起点到当前点作为一条边，绘制等边三角形"},
    {AnnoTool::RECT_TRI,       "直角三角形",  "两点拖拽绘制直角三角形（两点为斜边两端）"},
    {AnnoTool::EQU_RECT_TRI,   "等腰直角三角形", "拖拽绘制等腰直角三角形"},
    {AnnoTool::LINE,           "直线",        "两点拖拽绘制直线段"},
    {AnnoTool::POLYLINE,       "多线段",      "连续单击追加顶点，Esc 或切换工具收笔"},
    {AnnoTool::TEXT,           "文字",        "单击落点，输入文字内容后添加"},
    {AnnoTool::BRUSH,          "画笔",        "按住拖动绘制自由路径，Esc 或切换工具收笔"},
};

} // namespace

// 构建面板：一个「标注工具」分组，内部 3 列网格排布互斥按钮。
ToolPanel::ToolPanel(QWidget* parent) : QWidget(parent) {
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(8, 8, 8, 8);
    root->setSpacing(6);

    auto* box = new QGroupBox(QStringLiteral("标注工具"), this);
    auto* grid = new QGridLayout(box);
    grid->setSpacing(4);

    toolGroup_ = new QButtonGroup(this);
    toolGroup_->setExclusive(true);

    int i = 0;
    for (const auto&[tool, label, tip] : kTools) {
        constexpr int cols = 3;
        auto* btn = new QPushButton(QString::fromUtf8(label), box);
        btn->setCheckable(true);
        btn->setStyleSheet(kToolStyle);
        btn->setToolTip(QString::fromUtf8(tip));
        toolGroup_->addButton(btn, static_cast<int>(tool));
        grid->addWidget(btn, i / cols, i % cols);
        ++i;
    }
    connect(toolGroup_, &QButtonGroup::idToggled, this, &ToolPanel::onToolToggled);
    root->addWidget(box);
    root->addStretch(1);

    // 默认勾选「选择/移动」。
    if (auto* b = toolGroup_->button(static_cast<int>(AnnoTool::SELECT))) b->setChecked(true);
}

void ToolPanel::setModel(AnnotationBridge* model) {
    model_ = model;
    syncFromModel();
}

// 从模型反向同步当前工具选中态。
void ToolPanel::syncFromModel() const {
    if (!model_) return;
    toolGroup_->blockSignals(true);
    if (auto* b = toolGroup_->button(static_cast<int>(model_->currentTool()))) b->setChecked(true);
    toolGroup_->blockSignals(false);
}

// 工具按钮切换 → 发 toolSelected（仅在选中时上报，避免互斥组取消信号重复触发）。
void ToolPanel::onToolToggled(const int id, const bool checked) {
    if (!checked) return;
    emit toolSelected(static_cast<AnnoTool>(id));
}

} // namespace idc::gui
