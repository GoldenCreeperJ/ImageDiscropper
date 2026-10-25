// ============================================================================
// 文件：panels/param_panel_internal.h
// 作用：ParamPanel 共享内部件（param_panel.cpp 与 param_panel_pages.cpp 两个 TU 共用）——
//       参数页专用坐标输入框构造 makeCoordSpin(QWidget*)。坐标框类本体在 util/coord_spin.h
//       （参数页 / 导出重排 / 图层落位 / 预处理画布尺寸等几何输入点共用同一实现）。
// 说明：本构造只固化参数页的两处站点差异——取值范围 [0, kCoordLimitPx] 与定宽、关闭逐键跟踪
//       （提交才触发，NFR-5）；百分比语法与换算口径见 CoordSpinBox。
// ============================================================================
#pragma once

#include "util/coord_spin.h"

class QWidget;

namespace idc::gui {

// 创建参数页坐标输入框：范围 [0, kCoordLimitPx]、定宽 kCoordSpinWidthPx、关闭逐键跟踪。
CoordSpinBox* makeCoordSpin(QWidget* parent);

} // namespace idc::gui
