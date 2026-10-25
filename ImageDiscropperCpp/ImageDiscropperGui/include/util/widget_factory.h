// ============================================================================
// 文件：util/widget_factory.h
// 作用：Qt 控件构造微工厂——收敛散落在各面板 / 对话框里逐字重复的控件构造脚手架。
//       · makeSpin：把 `new QSpinBox(parent) + setRange(lo, hi) [+ setSuffix(单位)]`
//         这段各站点必写的构造收成一行。
//       · makeGroupBox：把 `new QGroupBox(parent) + i18n::bindTitle(...)` 收成一行
//         （全仓建组框必配标题重放，二者严格 1:1，是不变契约而非站点差异）。
//       · makePanelLayout：把面板根 `new QVBoxLayout(panel) + setContentsMargins(四边 kPanelMarginPx)`
//         收成一行（八个面板内边距四边同取此值，是文档化不变式）。
//       · makeButton / makeCheckBox：把 `new QPushButton/QCheckBox(parent) + i18n::bindButton(...)`
//         收成一行（带持久可译标题的按钮 / 复选框必配 bindButton 重放，是 i18n 零回落不变式；
//         无静态标题的色块 / 动态 / 字形按钮不套此工厂）。
// 分块依据：仅「构造 + 恒随构造发生的不变装配」是真·公共部分——spin 的取值范围（+ 普遍携带的
//       单位后缀）、组框的标题重放绑定、面板根的四边内边距；连接信号、其余 i18n 重放绑定（bindFormRow /
//       bindToolTip / bindSpecialValue）、keyboardTracking、触控最小高、setSpecialValueText、组框挂哪种 layout、
//       面板根 spacing（各面板取 kPanelSpacingPx / kPanelGroupSpacingPx 不一）等
//       **站点差异一律留调用处**（与 panels/param_panel.cpp 的 makeCoordSpin 同一定位，makeSpin 是它的通用版）。
//       循环内统一设 range 的站点（导出重排、预处理缩放尺寸）不改——那里 setRange 本就只写一次，
//       改用工厂反而逐行重复 range 实参。
// 类型：header-only inline（同 retranslate.h 的 bind* 助手风格）；仅 GUI 目标引用，故可含 <QSpinBox>。
// ============================================================================
#pragma once

#include <QCheckBox>
#include <QGroupBox>
#include <QPushButton>
#include <QSpinBox>
#include <QString>
#include <QVBoxLayout>

#include "i18n/retranslate.h"   // i18n::Retranslator / TextRef / bindTitle（makeGroupBox 的标题重放绑定）
#include "util/gui_nums.h"      // nums::panels::kPanelMarginPx（makePanelLayout 的四边内边距）

namespace idc::gui {

// 创建整数数值框：设定取值范围；suffix 非空时附带单位后缀（如 " px" / " %"）。
// 返回裸指针，交由调用方继续 setValue / connect / i18n::bind* 等站点专属装配。
inline QSpinBox* makeSpin(QWidget* parent, const int minVal, const int maxVal,
                          const QString& suffix = QString()) {
    auto* spin = new QSpinBox(parent);
    spin->setRange(minVal, maxVal);
    if (!suffix.isEmpty()) spin->setSuffix(suffix);
    return spin;
}

// 创建分组框：构造即绑定可重放标题（全仓建组框必配 bindTitle，是不变契约）。
// 返回裸指针 QGroupBox*，交由调用方继续 setLayout（Form / Box / Grid 因站点而异）等装配；
// 赋给 QWidget* 成员时天然上转，无需再 dynamic_cast 回 QGroupBox* 去绑标题。
inline QGroupBox* makeGroupBox(i18n::Retranslator& r, QWidget* parent, const i18n::TextRef& title) {
    auto* box = new QGroupBox(parent);
    i18n::bindTitle(r, box, title);
    return box;
}

// 创建面板根竖布局：四边内边距同取 kPanelMarginPx（八面板的文档化不变式）。
// 返回布局指针，交由调用方按需 setSpacing（各面板取值不同）与 addWidget。
inline QVBoxLayout* makePanelLayout(QWidget* panel) {
    auto* lay = new QVBoxLayout(panel);
    lay->setContentsMargins(nums::panels::kPanelMarginPx, nums::panels::kPanelMarginPx,
                            nums::panels::kPanelMarginPx, nums::panels::kPanelMarginPx);
    return lay;
}

// 创建按钮：构造即绑定可重放标题（带持久可译标题的按钮必配 bindButton，是 i18n 零回落不变式）。
// 返回裸指针，交由调用方继续 setCheckable / bindToolTip / connect 等站点专属装配。
inline QPushButton* makeButton(i18n::Retranslator& r, QWidget* parent, const i18n::TextRef& title) {
    auto* btn = new QPushButton(parent);
    i18n::bindButton(r, btn, title);
    return btn;
}

// 创建复选框：构造即绑定可重放文本（同 makeButton，bindButton 也适用于 QCheckBox 的文本）。
inline QCheckBox* makeCheckBox(i18n::Retranslator& r, QWidget* parent, const i18n::TextRef& text) {
    auto* check = new QCheckBox(parent);
    i18n::bindButton(r, check, text);
    return check;
}

} // namespace idc::gui
