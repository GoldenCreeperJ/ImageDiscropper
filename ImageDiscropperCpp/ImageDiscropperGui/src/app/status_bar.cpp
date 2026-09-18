// ============================================================================
// 文件：app/status_bar.cpp
// 作用：实现 StatusBar（见同名头文件说明）。
// 分块依据：构造只建标签，其余为纯 setter，无状态机。
// ============================================================================
#include "app/status_bar.h"

#include <QLabel>

namespace idc::gui {

// 构造：建 6 个标签（坐标 / 颜色 / 模式 / 保留块数 / 缩放 / 提示），提示标签占剩余空间。
StatusBar::StatusBar(QWidget* parent) : QStatusBar(parent) {
    coordLabel_ = new QLabel(QStringLiteral("坐标: (-, -)"), this);
    colorLabel_ = new QLabel(QStringLiteral("RGB(-,-,-)"), this);
    modeLabel_ = new QLabel(QStringLiteral("模式: -"), this);
    countLabel_ = new QLabel(QStringLiteral("保留块: 0"), this);
    zoomLabel_ = new QLabel(QStringLiteral("缩放: 100%"), this);
    hintLabel_ = new QLabel(QStringLiteral("就绪"), this);
    addWidget(coordLabel_);
    addWidget(colorLabel_);
    addWidget(modeLabel_);
    addWidget(countLabel_);
    addWidget(zoomLabel_);
    addWidget(hintLabel_, 1); // 提示信息占据剩余空间。
}

void StatusBar::setCoord(const int x, const int y) {
    coordLabel_->setText(QStringLiteral("坐标: (%1, %2)").arg(x).arg(y));
}

void StatusBar::setColor(const int r, const int g, const int b) {
    colorLabel_->setText(QStringLiteral("RGB(%1,%2,%3)").arg(r).arg(g).arg(b));
}

void StatusBar::clearColor() {
    colorLabel_->setText(QStringLiteral("RGB(-,-,-)"));
}

// 模式中文名（原 MainWindow::modeName 收拢于此）。
void StatusBar::setModeTier(const engine::Tier tier) {
    switch (tier) {
        case engine::Tier::L1: modeLabel_->setText(QStringLiteral("模式: L1 标准提取")); break;
        case engine::Tier::L2: modeLabel_->setText(QStringLiteral("模式: L2 反向剔除")); break;
        case engine::Tier::L3: modeLabel_->setText(QStringLiteral("模式: L3 网格分割")); break;
        default: modeLabel_->setText(QStringLiteral("模式: -")); break;
    }
}

void StatusBar::setCount(const int kept) {
    countLabel_->setText(QStringLiteral("保留块: %1").arg(kept));
}

void StatusBar::setZoomPercent(const int percent) {
    zoomLabel_->setText(QStringLiteral("缩放: %1%").arg(percent));
}

void StatusBar::setHint(const QString& text) {
    hintLabel_->setText(text);
}

// 非模态提示：写状态栏提示标签 + 限时消息（错误不打断用户）。
void StatusBar::notify(const QString& msg, const bool isError) {
    hintLabel_->setText(msg);
    showMessage(msg, isError ? 8000 : 4000);
}

} // namespace idc::gui
