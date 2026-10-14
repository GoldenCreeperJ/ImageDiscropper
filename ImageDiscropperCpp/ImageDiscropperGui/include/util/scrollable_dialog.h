// ============================================================================
// 文件：util/scrollable_dialog.h
// 作用：小屏 / 移动端对话框的两件通用能力（SPEC §8.2/§8.3 形态差异）——
//       · wrapScrollable：把对话框主体（表单 / 长文本）装进纵向可滑的 QScrollArea，并换装触控滚动
//         （复用 mobile/touch_scroll.h::installTouchScroll）；横滚恒关、纵滚按需、主体 widgetResizable，
//         使主体随视口宽自适应、超高时内部滚动；
//       · limitDialogToScreen：给对话框装一道「屏幕尺寸护栏」事件过滤器，按当前屏幕可用几何×比例钳制最大宽 / 高
//         （比例见 nums::mobile::kDialogMax*Ratio），并在首次显示时把尺寸收到「内容首选尺寸与钳值的较小者」——
//         窗口不随 sizeHint 爆到画面外，内容超出即由内部滚动区消化，配合固定按钮行根治「显示不全 / 无法关闭」；
//         同时监听父窗口 Resize/Move，旋转后把对话框居中回正到父窗口全局中心（部件零重建）；
//         桌面大屏下钳值远超内容，等效无约束、行为不变。
//       · showScrollableMessage：以可滚动外壳呈现长文本信息框（使用说明 / 关于），替代 QMessageBox 的
//         「一长条」窄高呈现——正文 word wrap + 最小宽 + 纵向滚动 + 底部按钮常驻。入参为已解析字符串（本层不含 i18n 依赖）。
// 分块依据：对话框滚动与尺寸钳制为跨对话框通用件，独立成件供 app 对话框与 mobile 壳复用
//           （CONTRIBUTING.md「分层纪律」）。
// ============================================================================
#pragma once

#include <QSize>

class QDialog;
class QScrollArea;
class QString;
class QWidget;

namespace idc::gui {

// 把 body 装进纵向可滑 QScrollArea 并返回该滚动区（body 归其所有）：横滚 AlwaysOff、纵滚 AsNeeded、无边框、
// widgetResizable，并换装触控滚动（桌面回落 QScroller 鼠标协调器，行为不变）。调用方把返回的滚动区加入布局。
QScrollArea* wrapScrollable(QWidget* body);

// 给对话框装「屏幕尺寸护栏」：Show / Resize 时按可用几何×比例钳最大宽 / 高；可选 preferredContentSize 为首次显示
// 的初值目标（缺省用对话框自身 sizeHint）——超出钳值即被缩小，由内部滚动区消化。同时监听 parent 的 Resize/Move，
// 旋转后居中回正到 parent 全局中心、部件零重建。
void limitDialogToScreen(QDialog* dialog, const QSize& preferredContentSize = QSize());

// 以可滚动外壳呈现长文本信息框（使用说明 / 关于）：正文 word wrap + 合理宽度 + 纵向滚动 + 底部确定按钮常驻。
// parent 为空则无父窗口；title / text / okLabel 均由调用方按当前语言解析后传入（本层不涉文案表）。
void showScrollableMessage(QWidget* parent, const QString& title, const QString& text, const QString& okLabel);

} // namespace idc::gui
