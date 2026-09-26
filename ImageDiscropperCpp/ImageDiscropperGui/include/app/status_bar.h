// ============================================================================
// 文件：app/status_bar.h
// 作用：主窗口状态栏组件——集中呈现光标坐标/像素颜色/当前模式/保留块数/缩放倍数/提示信息
//       （含非模态 notify），从 MainWindow 拆出以瘦身主窗口（避免上帝文件）。
// 分块依据：
//   - 状态栏纯展示、无编排逻辑：只提供 setter，不依赖 Document/桥，由 app 层调用方喂值；
//   - 原 MainWindow::buildStatus/modeName/notify 与各处 st* 标签写入点收拢于此。
// 说明：notify 沿用「状态栏提示标签 + 限时 showMessage」的非模态反馈约定（错误 8s、普通 4s）。
// ============================================================================
#pragma once

#include <QStatusBar>

#include "engine/engine.h"

class QLabel;

namespace idc::gui {

// ---------------------------------------------------------------------------
// StatusBar：主窗口状态栏（六标签 + 非模态提示）。
// ---------------------------------------------------------------------------
class StatusBar : public QStatusBar {
    Q_OBJECT
public:
    explicit StatusBar(QWidget* parent = nullptr);

    void setCoord(int x, int y) const;          // 「坐标: (x, y)」
    void setColor(int r, int g, int b) const;   // 「RGB(r,g,b)」
    void clearColor() const;                    // 「RGB(-,-,-)」（光标移出图像）
    void setModeTier(engine::Tier tier) const;  // 「模式: L1 标准提取」等（吸收原 MainWindow::modeName）
    void setCount(int kept) const;              // 「保留块: N」
    void setZoomPercent(int percent) const;     // 「缩放: N%」
    void setHint(const QString& text) const;    // 提示标签
    // 紧凑模式（移动端，SPEC §8.3 布局）：只保留模式/保留块数/缩放三项，
    // 坐标、RGB 与提示标签隐藏（详情入抽屉）；桌面默认关闭，行为零变化。
    void setCompactMode(bool compact) const;
    // 非模态提示：提示标签 + 状态栏限时消息（错误 8s、普通 4s），不打断用户。
    void notify(const QString& msg, bool isError);

private:
    QLabel* coordLabel_{nullptr};
    QLabel* colorLabel_{nullptr};
    QLabel* modeLabel_{nullptr};
    QLabel* countLabel_{nullptr};
    QLabel* zoomLabel_{nullptr};
    QLabel* hintLabel_{nullptr};  // addWidget(..., 1)：占据剩余空间
};

} // namespace idc::gui
