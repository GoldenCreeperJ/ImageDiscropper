// ============================================================================
// 文件：mobile/touch_scroll.h
// 作用：移动端滚动接管（独立通用件）：
//       · WheelToHScrollFilter——底部工具条滚轮重定向：把 Wheel 事件换算为水平滚动位移
//         （桌面预览亦用，补齐拖拽之外的第二条滚动路径）；
//       · installTouchScroll——移动端全部滚动区统一换装触控滚动（QScroller 替代，
//         Android QPA 属性通道不完整）；桌面保持 QScroller 鼠标协调器。
//       触控滚动过滤器实现（TouchScrollFilter）为文件内件，不外露。
// 分块依据：滚动件自包含、与壳装配无耦合，独立成件供抽屉/条带复用
//           （CONTRIBUTING.md「分层纪律」；SPEC §8.3 触控精度）。
// ============================================================================
#pragma once

#include <QObject>

class QScrollArea;
class QScrollBar;

namespace idc::gui {

// ---------------------------------------------------------------------------
// WheelToHScrollFilter：滚轮重定向——QScrollArea 默认滚轮处理驱动的是**垂直**滚动条，
// 横向条带里会出现「上下滚动」的错乱。拦截视口链（含悬停在按钮上时）的 Wheel 事件，
// 换算成水平位移；无溢出/非滚轮事件不消费（交回默认链）。
// ---------------------------------------------------------------------------
class WheelToHScrollFilter : public QObject {
public:
    explicit WheelToHScrollFilter(QScrollBar* target, QObject* parent = nullptr);

protected:
    bool eventFilter(QObject* obj, QEvent* ev) override;

private:
    QScrollBar* target_;
};

// 移动端全部滚动区（抽屉页/分组/底部条带）统一换装触控过滤器；
// 桌面预览保持 QScroller 鼠标协调器模式（无触摸事件，不受影响）。
void installTouchScroll(QScrollArea* area);

} // namespace idc::gui
