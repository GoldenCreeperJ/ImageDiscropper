// ============================================================================
// 文件：app/status_bar.cpp
// 作用：实现 StatusBar（见同名头文件说明）。
// 分块依据：构造只建标签，其余为纯 setter，无状态机。
// ============================================================================
#include "app/status_bar.h"

#include <QLabel>

#include "util/gui_nums.h"
#include "i18n/gui_texts.h"
#include "i18n/gui_i18n.h"

namespace idc::gui {

// 构造：建 4 个标签（坐标 / 颜色 / 缩放 / 提示），提示标签占剩余空间。
// 初值文案经 bindText 登记：切语言时重放为新语言的初值（动态 coord/color/zoom 由后续事件重新覆盖）。
StatusBar::StatusBar(QWidget* parent) : QStatusBar(parent) {
    coordLabel_ = new QLabel(this);
    colorLabel_ = new QLabel(this);
    zoomLabel_ = new QLabel(this);
    hintLabel_ = new QLabel(this);
    i18n::bindText(retranslator_, coordLabel_, IDC_GT(status, kCoordInit));
    i18n::bindText(retranslator_, colorLabel_, IDC_GT(status, kColorInit));
    i18n::bindText(retranslator_, zoomLabel_, IDC_GT(status, kZoomInit));
    i18n::bindText(retranslator_, hintLabel_, IDC_GT(status, kStatusReady));
    addWidget(zoomLabel_);
    addWidget(coordLabel_);
    addWidget(colorLabel_);
    addWidget(hintLabel_, 1); // 提示信息占据剩余空间。
}

void StatusBar::setCoord(const int x, const int y) const {
    coordLabel_->setText(IDC_GTR(status, kCoordFmt).arg(x).arg(y));
}

void StatusBar::setColor(const int r, const int g, const int b) const {
    colorLabel_->setText(IDC_GTR(status, kColorFmt).arg(r).arg(g).arg(b));
}

void StatusBar::clearColor() const {
    colorLabel_->setText(IDC_GTR(status, kColorInit));
}

void StatusBar::setZoomPercent(const int percent) const {
    zoomLabel_->setText(IDC_GTR(status, kZoomFmt).arg(percent));
}

void StatusBar::setHint(const QString& text) const {
    hintLabel_->setText(text);
}

// 紧凑模式（移动端）：隐藏坐标/RGB/提示三项，只留缩放（SPEC §8.3 布局）。
// 提示仍经 showMessage 限时弹出（notify 里的 showMessage 不受标签隐藏影响），错误可见。
void StatusBar::setCompactMode(const bool compact) const {
    coordLabel_->setVisible(!compact);
    colorLabel_->setVisible(!compact);
    hintLabel_->setVisible(!compact);
}

// 非模态提示：写状态栏提示标签 + 限时消息（错误不打断用户）。
void StatusBar::notify(const QString& msg, const bool isError) {
    hintLabel_->setText(msg);
    showMessage(msg, isError ? nums::app::kErrorShowMs : nums::app::kHintShowMs);
}

} // namespace idc::gui
