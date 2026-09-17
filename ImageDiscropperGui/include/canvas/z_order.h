// ============================================================================
// 文件：canvas/z_order.h
// 作用：集中定义画布各图层的 z 值（叠放次序），对应 guideline §4.2「图层顺序」。
//       单一定义点，避免各图元文件散落魔法数、次序互相打架。
// 分块依据：纯常量头（header-only），无逻辑、无 Q_OBJECT，被 scene / mask_layer /
//           selection_rect_item 共同 include。
// 说明：§4.2 列出的次序为 底图<标注<保留遮罩<删除遮罩<网格<切割线<选区<编号。
//       第一阶段用「红色删除底铺满全图 + 绿色保留块叠加其上」的渲染法呈现删除/保留
//       （GUI 不做区域布尔，A-0.1），故删除底须位于保留块「之下」——本表据此把
//       DELETE 置于 KEEP 之前，视觉效果（保留绿 / 删除红）与 §4.2、§5.3 完全一致。
// ============================================================================
#pragma once

namespace idc::gui::zorder {

constexpr double kBase = 0.0;        // 1. 底图（预处理后的图像）
constexpr double kAnnotation = 10.0; // 2. 标注图层（第四阶段接入）
constexpr double kDelete = 20.0;     // 删除块遮罩（红色，铺满全图作底）
constexpr double kKeep = 30.0;       // 保留块遮罩（绿色，叠加于红色之上）
constexpr double kGrid = 40.0;       // 5. 网格线（第二阶段 L3 接入）
constexpr double kCutLine = 50.0;    // 6. 切割线层（保留值；第一阶段切割线已并入选区图元，见 kSelection）
constexpr double kSelection = 60.0;  // 7. 选区边框（橙色）+ 拖拽手柄 + 贯穿切割线延伸（同一橙色图元）
constexpr double kCellNumber = 70.0; // 8. 单元格编号（第二阶段 L3 接入）

} // namespace idc::gui::zorder
