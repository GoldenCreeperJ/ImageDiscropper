// ============================================================================
// 文件：canvas/items/z_order.h
// 作用：集中定义画布各图层的 z 值（叠放次序），对应本目录 README「图层 z 序」。
//       单一定义点，避免各图元文件散落魔法数、次序互相打架。
// 分块依据：纯常量头（header-only），无逻辑、无 Q_OBJECT，被场景与各图层实现共同 include
//           （canvas_scene / mask_layer / grid_layer / cell_picker_item / annotation_item）。
// 说明：本目录 README「图层 z 序」列出的次序为 底图<图片图层落位框<标注<标注绘制预览/OBB<遮罩<网格<选区<编号<网格基准手柄。
//       遮罩为**单一图层**（kMask）：一个图元内先铺「红色删除底铺满全图」、再叠「绿色保留块」于其上
//       （GUI 不做区域布尔，CONTRIBUTING.md「分层纪律」），透出红处即删除、绿覆盖处即保留；
//       保留/删除共用此单 z 层，视觉观感（保留绿 / 删除红）不变。
// ============================================================================
#pragma once

namespace idc::gui::zorder {

constexpr double kBase = 0.0;        // 1. 底图（预处理后的图像）
constexpr double kImageLayerPixels = 5.0;  // 1a. 图片图层像素（多层画布解耦渲染）：叠在底图之上、落位框之下的独立像素图元，
                                       //     承载「图层于透明底」的合成分层，随图层显隐独立于底图切换（关底图不误关图层）。
constexpr double kImageLayer = 10.0;  // 1b. 图片图层落位框（多层画布）：位于底图之上、标注之下，标示各图片图层可拾取区域。
constexpr double kAnnotation = 20.0; // 2. 标注图层（矢量叠加）
constexpr double kAnnotationPreview = 25.0; // 2b. 标注绘制预览（橡皮筋）＋选中标注的 OBB 高亮框（独立顶层图元）。
constexpr double kMask = 30.0;       // 保留/删除遮罩（单一图层：一个图元内先铺红色删除底、再叠绿色保留块）。
constexpr double kGrid = 40.0;        // 5. 网格线（L3 / L2 多矩形诱导）
constexpr double kSelection = 50.0;  // 6. 选区边框（橙色）+ 拖拽手柄 + 贯穿切割线延伸（同一橙色图元）
constexpr double kCellNumber = 55.0; // 7. 单元格编号角标（L3 自定义序；CellPickerItem 的角标子图层，子 z = kCellNumber − kSelection）
constexpr double kGridHandle = 60.0; // 8. 网格基准手柄（L3）：网格线层的基准点/单元尺寸拖拽手柄，置顶以确保按下先落到手柄
                                     //    而不被单元点选图元（kSelection）抢走。

} // namespace idc::gui::zorder
