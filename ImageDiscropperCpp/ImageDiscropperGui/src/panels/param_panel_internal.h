// ============================================================================
// 文件：src/panels/param_panel_internal.h
// 作用：ParamPanel 共享内部件（param_panel.cpp 与 param_panel_pages.cpp 两个 TU 共用）——
//       坐标输入框构造 makeCoordSpin。声明在此、定义留在 param_panel.cpp。
// ============================================================================
#pragma once

class QSpinBox;
class QWidget;

namespace idc::gui {

// 创建坐标输入框：范围 [0,100000]，关闭逐键跟踪（提交才触发），回车即生效（NFR-5）。
QSpinBox* makeCoordSpin(QWidget* parent);

} // namespace idc::gui
