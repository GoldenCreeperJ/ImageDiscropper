// ============================================================================
// 文件：panels/accordion_panel.cpp
// 作用：实现 AccordionPanel（见同名头文件说明）。
// 实现要点：
//   - 每节 = 一个 section 容器（标题 QPushButton + 内容 widget 竖排），整体堆进
//     本 widget 的 QVBoxLayout；尾部常驻一个 stretch——吸收「内容短于视口」的剩余
//     空间，节本身永不被拉伸（全折叠时标题条紧凑贴顶，不出大色块）；
//     内容高出视口时 stretch 归零，外层 QScrollArea 拿到真实滚动范围；
//   - 展开/收起 = 标题按钮 checkable 态直接驱动内容 setVisible（多节互不影响，
//     允许同时展开）；标题前缀 ▾/▸（Unicode 转义写死，防编辑链字符损坏）；
//   - 标题用 checkable 扁平 QPushButton 而非 QToolButton：后者在 QSS 下常无视
//     text-align 仍居中绘制，QPushButton 的 text-align: left 可靠生效（tab 头左对齐）；
//   - 本类只发 sectionToggled 信号做联动（如导出预览补渲染），不做业务。
// ============================================================================
#include "panels/accordion_panel.h"

#include <QPushButton>
#include <QVBoxLayout>

namespace idc::gui {

namespace {
// 展开/收起指示前缀（\u25BE ▾ / \u25B8 ▸）。
QString headerGlyph(bool expanded) {
    return expanded ? QStringLiteral("\u25BE ") : QStringLiteral("\u25B8 ");
}
} // namespace

AccordionPanel::AccordionPanel(QWidget* parent) : QWidget(parent) {
    body_ = new QVBoxLayout(this);
    body_->setContentsMargins(0, 0, 0, 0);
    body_->setSpacing(0);   // 节与节紧贴，分隔感由标题按钮底边线承担。
    body_->addStretch(1);   // 尾部吸收剩余空间；addSection 经 insertWidget 插在它之前。
}

void AccordionPanel::addSection(QWidget* content, const QString& title) {
    if (!content) return;

    auto* section = new QWidget(this);
    auto* lay = new QVBoxLayout(section);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(2);

    auto* header = new QPushButton(section);
    header->setCheckable(true);
    header->setChecked(true);            // 初始全展开：四面板内容默认可达（滚动触达）。
    header->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    // 轻量分隔条样式（逐按钮设定，不波及节内容里的同类控件）：
    // 展开＝白底加粗 + ▾，收起＝灰字 + ▸，hover 浅底；底边线分节，无原生 checked 下陷色块。
    header->setStyleSheet(QStringLiteral(
        "QPushButton { text-align: left; padding: 6px 8px; border: none;"
        " border-bottom: 1px solid palette(mid); background: transparent; }"
        "QPushButton:hover { background: palette(light); }"
        "QPushButton:checked { font-weight: bold; }"
        "QPushButton:!checked { color: palette(midtext); font-weight: normal; }"));

    lay->addWidget(header);
    lay->addWidget(content);             // addWidget 语义：content reparent 到 section。
    body_->insertWidget(static_cast<int>(sections_.size()), section);   // 插在尾部 stretch 之前，保持添加序。

    Section s;
    s.header = header;
    s.content = content;
    s.title = title;
    sections_.push_back(s);
    refreshHeaderText(sections_.back(), true);

    const int index = sections_.size() - 1;
    connect(header, &QPushButton::toggled, this, [this, index](bool expanded) {
        Section& sec = sections_[index];
        sec.content->setVisible(expanded);
        refreshHeaderText(sec, expanded);
        emit sectionToggled(sec.content, expanded);
    });
}

bool AccordionPanel::isSectionExpanded(const QWidget* content) const {
    for (const Section& s : sections_)
        if (s.content == content) return s.header->isChecked();
    return false;
}

void AccordionPanel::expandSection(QWidget* content) {
    for (Section& s : sections_) {
        if (s.content == content && !s.header->isChecked()) {
            s.header->setChecked(true);   // 触发 toggled → 显隐/文案/信号统一走一条链。
            return;
        }
    }
}

void AccordionPanel::refreshHeaderText(Section& section, bool expanded) {
    section.header->setText(headerGlyph(expanded) + section.title);
}

} // namespace idc::gui
