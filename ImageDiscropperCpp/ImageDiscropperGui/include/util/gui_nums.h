// ============================================================================
// 文件：util/gui_nums.h（GUI 常量层 · 数字）
// 作用：把 GUI 里「会被人工调整」的呈现与交互数值收编为命名常量：布局尺寸与间距、控件触控下限、
//       动画 / 节流时长、缩放步进、阈值、参数范围上下限、绘制工具与标注的出厂默认属性。
// 分块依据：按来源模块 —— canvas（画布与图元）、panels（左侧 / 右侧各面板）、dialog（各类对话框）、
//       mobile（移动骨架与手势）、app（窗口 / 状态栏 / 偏好默认值）、model（文档与标注编辑器数据桥）。
//       每组注释写明实际读取处，方便「改一个数就能看清它影响了哪些观感」。
// 命名：k + 语义名，**口径必须入名**：…ScreenPx（屏幕像素，与缩放无关）、…ScenePx（场景 / 原图
//       像素，随缩放线性放大）、…Ms、…Ratio、…Px。两类口径混用是本层最容易出的错，务必核对。
// 类型：inline constexpr int / double / long long（Qt 侧使用点自行隐式转 qreal/qint64，与现状一致）。
//       **本头刻意不含任何 Qt 头**：include/model/annotation_editor.h 须保持零 Qt 依赖，
//       它会被 Core 的 unit_tests 目标直接编译（该目标不挂 Qt include 路径），引 <QtGlobal> 即编不过。
// 同值不同义：允许各立一名（如坐标量级上限与缩放百分比上限同为 100000）——按语义命名优先于去重，
//       与 Core 常量层同一取舍（见 core_nums.h geometry 组的说明）。
// 上游真相源：Core 已有的护栏与出厂默认**只做转引、不复制数值**（generate::kMaxDimension、
//       core::nums::{engine,annotation,generate}）；改上游值 GUI 随之变，属期望行为。
// 不提取（保留原样，勿为提而提）：
//   · 数学与换算常数：255（alpha 满值，故不透明度输入框恒 (0,255) 原样保留）、100（百分比换算）、
//     180/360/π、1e-6 一类浮点容差、0.5 半像素、1000（ms/s）。
//   · 类型边界：噪声种子上限 2147483647（即 int 上限）、QWIDGETSIZE_MAX。
//   · 颜色：Palette 六色与三 alpha 默认、图元内的 constexpr QColor —— 属「画布视觉规范」成表且
//     已单点集中，改动走「外观/交互」对话框，不在本层重复定义。
//   · 图层 z 序（统一定义于 include/canvas/items/z_order.h，勿在本层重复定义）。
//   · 零边距 (0,0,0,0)、循环边界 0/1、Qt 枚举与标志位组合、单点自明的局部临时值
//     （如 kMaxRecent、流式读块大小、导出源/预览尺寸表——各自已就地 constexpr 且有注释）。
//   · tests/ 内的期望值。
// 与偏好的关系：已进 CanvasPrefs / AppPrefs 的值，其**范围上下限与出厂默认**在本层定义，
//       由 prefs 类与对话框共同引用（避免对话框 setRange 与 prefs clamp 两处各写一份）。
// 登记纪律：新增呈现参数时在此登记，不要回到 .cpp 里写裸数字；各使用点只引本层命名常量。
// ============================================================================
#pragma once

#include "core/core_nums.h"   // 上游出厂默认与区间（engine / annotation / generate 三组）

namespace idc::gui::nums {

// canvas：画布视图、场景与图元的呈现与交互参数（src/canvas/**）。
namespace canvas {
inline constexpr double kDesktopOverlayScreenPx = 8.0;  // 桌面覆盖层抓取带的屏幕基准（本项目实现选择，支撑 SPEC §8.2 拖拽能力）：
                                                        // 手柄/抓边条带/点选阈值的屏幕口径。两处同值共用
                                                        // （适配器接口默认值 + 视图取不到适配器时的兜底）。
inline constexpr double kTouchOverlayScreenPx = 12.0;   // 触控同基准（Android 逻辑 px≈dp≈pt，SPEC §8.3 精度下限）；
                                                        // 调它只改触控下抓取带宽度。
inline constexpr double kFallbackHandleScenePx = 8.0;   // 视图尚未下发尺寸时图元自己的**场景口径**兜底初值。
                                                        // 与上行同为 8 但口径不同（上者屏幕像素、本者场景像素），故不合并；
                                                        // CanvasView::updateHandleSize 一经运行就以「屏幕基准 / 缩放」覆盖它。
                                                        // 五处落点：items/annotation_item.h、items/selection_rect_item.h、
                                                        // items/cell_picker_item.h（overlayScale_）、canvas_scene.h（annoHandleSize_ 与 multiHandleSize_）。
inline constexpr double kWheelZoomFactor = 1.15;        // 滚轮一格 = ×1.15 / ÷1.15（桌面专属设备语义）：
                                                        // 调大每格跳跃更明显。
inline constexpr int kNudgeStepPx = 1;                  // 方向键微调选区一步（原图像素）。
inline constexpr int kNudgeStepShiftPx = 10;            // Shift 大步；移动端步进盘 ×10 倍率同值。
inline constexpr int kMarqueeMinSizePx = 3;             // 框选误触阈值：橡皮筋宽高须同时大于它才上报选区（视口像素）。
                                                        // 调大更耐误触，但小范围精确框选会被吞掉。
inline constexpr double kFitToWindowKeepRatio = 0.94;   // 适应窗口时内容占视口的比例（固定小边距，与滚动余量偏好解耦）：
                                                        // 默认余量 3% 下 0.94＋2×3%＝1.00，适配后场景矩形恰等于视口、不出滚动条；
                                                        // 适应窗口**不读 scrollMarginPercent**——否则大的滚动余量会
                                                        // 把内容挤到 (1－2×百分比) 乃至趋零、退化成“缩到最小尺寸”。
inline constexpr double kPickSamePointTolScenePx = 3.0; // 连击循环选层的「近似同点」容差（场景单位）：
                                                        // 调大则手指微抖更易续上同一候选栈。
// ——触控手势判定（canvas_touch_input_adapter.cpp）：双击＝适应窗口，轻点是它的最小单位——
inline constexpr long long kDoubleTapGapMs = 300;   // 第二击按下距上一击释放的上限；调小则双击要求更手快。
inline constexpr int kDoubleTapPosScreenPx = 64;    // 两击落点曼哈顿距离容差（屏幕像素，手指抖动余量）。
inline constexpr long long kTapMaxMs = 250;         // 轻点的最长按下时长；超出视为长按，不作双击候选。
inline constexpr int kTapMoveScreenPx = 8;          // 轻点允许的移动（屏幕像素）；超出即视为拖拽，作废双击候选。
// ——标注图元（items/annotation_item.cpp）：以下均为 handleSize（随缩放换算的场景手柄基准）的倍数——
inline constexpr double kAnnoScaleLimit = 50.0;         // 手柄拖拽缩放系数限幅：防一次拖拽把形状放大到失控。
inline constexpr double kAnnoScaleMinAbs = 0.02;        // 缩放系数绝对值下限：近 0 会使行列式为 0、逆变换退化、形状塌缩。
inline constexpr double kAnnoHandleDrawRatio = 1.35;    // 可视手柄边长 = handleSize × 本值（略放大便于看清与点中）。
inline constexpr double kAnnoHandleGrabRatio = 2.0;     // 手柄抓取半径 = handleSize × 本值（shape() 与命中测试同一口径）。
inline constexpr double kAnnoRotateLiftRatio = 3.0;     // 旋转手柄自顶边中点外推距离 = handleSize × 本值。
inline constexpr double kAnnoBoundsMarginRatio = 5.0;   // 包围盒余量里的 handleSize 倍项：须容下旋转手柄与其抓取区。
inline constexpr double kAnnoBoundsMarginPadPx = 4.0;   // 包围盒固定附加余量（场景像素），与描边宽度相加。
// ——L3 CUSTOM 序的单元编号角标（items/cell_picker_item.cpp）：以设备像素绘制，字号与底框不随缩放变——
inline constexpr int kCellBadgeFontScreenPx = 13;       // 角标字号（屏幕像素）。
inline constexpr double kCellBadgeMinCellScreenPx = 14.0; // 单元屏幕尺寸小于本值则跳过角标，避免缩小时糊成一片。
inline constexpr double kCellBadgeHeightScreenPx = 16.0;  // 角标底框高度。
inline constexpr double kCellBadgeCharWidthScreenPx = 8.0; // 角标底框宽度的每字符增量。与同组另两条 8.0
                                                        // （kDesktopOverlayScreenPx / kFallbackHandleScenePx）同值纯属巧合。
inline constexpr double kCellBadgeWidthPadScreenPx = 6.0;  // 角标底框宽度的固定余量。
inline constexpr double kCellBadgeRadiusScreenPx = 3.0;    // 角标底框圆角半径。
} // namespace canvas

// panels：左右各面板的排版口径与输入框边界（src/panels/**）。
namespace panels {
inline constexpr int kPanelMarginPx = 8;        // 面板内边距：八个面板（参数/导出/预处理/标注属性/图层编辑/图层/工具/左侧模式）
                                                // 的 setContentsMargins 四边同取此值。
inline constexpr int kPanelSpacingPx = 6;       // 面板内节 / 表单行距（含主窗口与移动抽屉里竖排面板的列间距）。
inline constexpr int kPanelTightSpacingPx = 4;  // 行内紧排（一行多框的容器）间距。
inline constexpr int kPanelGroupSpacingPx = 12; // 分组框之间的间距（预处理面板与左侧模式面板共用）。
inline constexpr int kLeftPanelMinWidthPx = 220; // 左侧模式面板下限宽（实际左栏最小宽由主窗 leftScroll 控制，见 app::kLeftPanelWidthPx）。
inline constexpr int kAccordionSectionSpacingPx = 8; // 右侧折叠栏「节与节」之间留白（标题底边线仍保留作分节描边）。
inline constexpr int kAccordionTitleSpacingPx = 2;   // 折叠栏标题行内部间距。
inline constexpr int kCoordSpinWidthPx = 90;    // 坐标输入框定宽（三栏分栏拖窄时不挤压坐标行）。
inline constexpr int kCoordLimitPx = 100000;    // 选区坐标输入框上限：有图时会被收紧到图像尺寸，此值是无图时的兜底上界。
inline constexpr int kMergeValueLimitPx = 100000; // 合并重排的列/行/单元宽高输入上限（0 表示「自动」，由 Core 推导）。
inline constexpr int kResizeMaxEdgePx = 20000;  // 画布尺寸改边的上限：覆盖 8000×8000 大图并留余量。
inline constexpr int kResizeScaleMaxPercent = 1000; // 按比例缩放的上限（1%–1000%）。
inline constexpr int kExportPreviewWidthPx = 200;   // 导出面板输出预览框尺寸（定宽定高，使预览缩放口径确定）。
inline constexpr int kExportPreviewHeightPx = 160;
// 以下三条转引 Core（出厂默认与合法区间由上游定义，GUI 不写第二份数值）：
inline constexpr int kQualityMinPercent = core::nums::engine::kQualityMin;         // 有损格式质量下限。
inline constexpr int kQualityMaxPercent = core::nums::engine::kQualityMax;         // 质量上限（同时是 JPEG 编码器上界）。
inline constexpr int kQualityDefaultPercent = core::nums::engine::kDefaultQuality; // 质量出厂默认。
} // namespace panels

// dialog：新建画布 / 外观交互 / 图案与落位表单的尺寸与边界（src/app/*_dialog.cpp、src/app/forms/**）。
namespace dialog {
inline constexpr int kTouchFieldMinHeightPx = 32;   // 对话框内数值框的触控下限高（与移动端抽屉控件同口径）。
inline constexpr int kSwatchButtonMinHeightPx = 36; // 取色按钮高：色块需按得住才好点，比数值框再高一档。
inline constexpr int kColorSwatchWidthPx = 64;      // 图案表单的底色 / 前景色块按钮尺寸。
inline constexpr int kColorSwatchHeightPx = 22;
inline constexpr int kNewCanvasPreviewMinWidthPx = 280;  // 新建画布预览区最小尺寸（小于此预览不可读）。
inline constexpr int kNewCanvasPreviewMinHeightPx = 200;
inline constexpr int kPatternPeriodMaxPx = 4096;    // 周期 / 格子边长 / 环间距上限（下限转引 Core kMinPeriod）。
inline constexpr int kPatternParamMaxPx = 256;      // 线宽与点半径共用的上限。
inline constexpr int kPatternHuePeriodPx = 128;     // 进入色相渐变时写入的色相波长初值（一个彩虹循环的像素宽）。
// 落位偏移与目标尺寸的输入上限见 nums::model::kTransformCoordLimitPx（面板与表单共用一条）。
} // namespace dialog

// mobile：移动骨架、工具条带与手势补偿参数（src/mobile/**）。
namespace mobile {
inline constexpr int kWindowWidthPx = 480;  // 移动壳初启尺寸（竖屏典型比例；横屏同为合法形态，SPEC §8.3 双布局）。
inline constexpr int kWindowHeightPx = 960;
inline constexpr int kDrawerMinWidthPx = 320;   // 抽屉最小尺寸＝设计下限（真机横/竖屏均放得下；
                                                // 防内容自然 hint 无界回压，取值依据见 src/mobile/README.md「触控精度与方向」）。
inline constexpr int kDrawerMinHeightPx = 240;
inline constexpr int kDrawerRatioDen = 5;           // 抽屉停靠占比的公共分母（横 2/5、竖 3/5）。
inline constexpr int kDrawerPortraitHeightNum = 3;  // 竖屏：抽屉占底部高的 3/5。
inline constexpr int kDrawerLandscapeWidthNum = 2;  // 横屏：抽屉占右侧宽的 2/5。
inline constexpr int kStripHeightPx = 48;       // 竖屏工具条条带定高（两处同值：初始构建与方向翻转重排）。
inline constexpr int kStripFallbackWidthPx = 96; // 横屏定宽取内容 sizeHint，取不到时的兜底宽。
inline constexpr int kStripMarginPx = 2;        // 条带内容外边距。
inline constexpr int kStripSpacingPx = 4;       // 条带按钮间距。
inline constexpr int kStepPadMarginPx = 6;      // 8 向步进盘网格外边距。
inline constexpr int kStepPadSpacingPx = 4;     // 步进盘按钮间距。
inline constexpr int kMinButtonSizePt = 44;     // 可点控件最小边长（Android 逻辑 px≈dp≈pt，SPEC §8.3 ≥44×44pt）：
                                                // 外观对话框的触控下限注释亦以此为准绳。
inline constexpr int kStripButtonHPadPx = 20;   // 条带文本按钮按文字实测宽时追加的水平余量（左右合计），
                                                // 避免 Fixed 策略下 sizeHint 低估致长标签（英文 Zoom In/Out）被裁。
// 对话框屏幕护栏（util/scrollable_dialog.cpp limitDialogToScreen）：小屏/横屏/长文本时把对话框
// 最大宽/高钳到可用几何的比例，内容超出即由内部滚动区消化、按钮行常驻——根治「显示不全 / 无法关闭」。
// 比例入本层（而非散写），旋转后重钳同取此值；桌面大屏下钳值远超内容，等效无约束。
inline constexpr double kDialogMaxWidthRatio = 0.95;    // 对话框最大宽 = 可用屏宽 × 本值。
inline constexpr double kDialogMaxHeightRatio = 0.9;    // 对话框最大高 = 可用屏高 × 本值。
inline constexpr int kStackColumnsMaxWidthPx = 900;     // 可用屏宽低于本值时对话框两栏元素分组叠为两段（移动端窄屏）。
inline constexpr int kInfoDialogMinWidthPx = 320;       // 可滚动信息框（使用说明/关于）正文标签最小宽：
                                                        // 窄屏下靠它 + word wrap 控宽，避免长行顶成竖窄条。
inline constexpr int kInfoDialogMinHeightPx = 240;      // 可滚动信息框滚动区最小高（保证正文有可读区、按钮不被挤没）。
// 错误提示里长路径（content:// 内容 URI 尤甚）的显示截断宽度（app/new_canvas_dialog.cpp 源图读取失败行）：
// 超出即按本宽用 QFontMetrics 省略号截断，防长串撑宽表单主体。
inline constexpr int kErrorPathMaxWidthPx = 200;
} // namespace mobile

// app：主窗口三栏尺寸、节流时长与画布交互偏好的出厂默认与钳制区间（src/app/**）。
namespace app {
inline constexpr int kWindowWidthPx = 1280;     // 桌面主窗口初启尺寸（首次显示前的默认值，之后由窗口管理器定夺）。
inline constexpr int kWindowHeightPx = 800;
inline constexpr int kLeftPanelWidthPx = 330;   // 左栏最小宽＝初始宽：须容下标注工具 3 列网格（含长标签）不截断。
inline constexpr int kRightPanelWidthPx = 375;  // 右栏最小宽＝初始宽：避免落位坐标行被挤到错位。
inline constexpr int kCanvasAreaWidthPx = 860;  // 三栏分隔条初始尺寸里的画布列宽（左/右见同行两条）。
inline constexpr int kSplitHandleWidthPx = 6;   // 分隔条宽度：稍宽则拖拽手感更明显。
inline constexpr int kHistoryDebounceMs = 500;  // 撤销/重做的防抖静默时长：拖拽释放或滑块停手 500ms 视为一次离散操作。
inline constexpr int kHintShowMs = 4000;        // 状态栏限时消息普通停留（app/status_bar.cpp::notify）。
inline constexpr int kErrorShowMs = 8000;       // 错误回报停留：比常规长一档，留出读完报因的时间。
inline constexpr int kHandlePxMin = 4;          // 手柄/命中尺寸显式覆盖的钳制区间（0 表示跟随平台默认，不在本区间内）。
inline constexpr int kHandlePxMax = 64;         // 上限同时是外观对话框该输入框的 range 上界。
inline constexpr int kScrollMarginPercentMax = 45;  // 边缘滚动余量（平移范围）上限：配合 0.94 适配比例，适配档下空白
                                                    // （两侧各 45%＝90% 视口）仍小于内容（94% 视口）；与适应窗口缩放无关（适配已用固定小边距、不读此值）。
inline constexpr int kZoomStepPercentMin = 1;   // 缩放步进区间（对话框 range 与 prefs clamp 共用，防持久化出非法值）。
inline constexpr int kZoomStepPercentMax = 100;
inline constexpr int kZoomMinPercentLower = 1;      // 缩放下限可填区间（上界 9999 < 缩放上限最小 10000，维持 min < max）。
inline constexpr int kZoomMinPercentUpper = 9999;
inline constexpr int kZoomMaxPercentLower = 2;      // 缩放上限可填区间（下界 2 > 缩放下限最小 1，同上）。
inline constexpr int kZoomMaxPercentUpper = 10000;
// 画布交互偏好的出厂默认（CanvasPrefs 成员初值、QSettings 读取回退、恢复默认三处共用）：
inline constexpr bool kPrefSnapDefault = true;              // 选区吸附默认开。
inline constexpr int kPrefHandlePxDefault = 0;              // 手柄尺寸 0 = 跟随平台默认（桌面 8 / 触控 12+）。
inline constexpr int kPrefScrollMarginPercentDefault = 3;   // 边缘滚动余量（视口各边百分比，0 = 不留）。
inline constexpr int kPrefZoomStepPercentDefault = 25;      // 缩放步进 25% → ×1.25 / ÷1.25。
inline constexpr int kPrefZoomMinPercentDefault = 5;        // 缩放下限 0.05 倍。
inline constexpr int kPrefZoomMaxPercentDefault = 4000;     // 缩放上限 40 倍。
inline constexpr int kPrefNewWidthPxDefault = 1024;         // 「新建画布」对话框默认宽（每次新建确定后自动记住）。
inline constexpr int kPrefNewHeightPxDefault = 768;
} // namespace app

// model：标注编辑器出厂默认属性与输入边界（src/model/**、属性面板与图层编辑面板共用）。
namespace model {
inline constexpr int kDefaultStrokeWidthPx = core::nums::annotation::kDefaultStrokeWidth;   // 描边宽度出厂默认。
inline constexpr int kStrokeWidthMinPx = core::nums::annotation::kMinStrokeWidth;           // 下限：0 会只剩填充。
inline constexpr int kStrokeWidthMaxPx = 200;             // 属性面板描边上限（远超图元常规线宽，供极端样式）。
inline constexpr double kDefaultFontSizePx = core::nums::annotation::kDefaultFontSizePx;    // 文字标注字号出厂默认（原图像素）。
inline constexpr int kFontSizeMinPx = 1;                  // 字号下限：面板 range 与编辑器钳制共用（编辑器侧转 double）。
inline constexpr int kFontSizeMaxPx = 2000;               // 字号上限。
inline constexpr double kDefaultCornerRadiusPx = 0.0;     // 转角圆角出厂默认＝直角。
inline constexpr double kCornerRadiusMinPx = 0.0;
inline constexpr double kCornerRadiusMaxPx = 500.0;       // 圆角上限（场景像素）。
inline constexpr double kCornerRadiusStepPx = 1.0;        // 圆角步进。
inline constexpr bool kDefaultFillOn = false;             // 填充开关出厂默认（改 true 则新画形状默认带填充）。
inline constexpr int kTransformCoordLimitPx = 1000000;    // 落位自由偏移（含负）与目标宽高的量级上限：
                                                          // 图层编辑面板与新建对话框的落位行共用一条。
inline constexpr int kAnnoScalePercentLimit = 100000;     // 缩放百分比输入限幅（含负＝翻转）：忠实反映大幅缩放与翻转。
inline constexpr int kAnnoRotateDegLimit = 36000;         // 旋转角度输入限幅：容纳多次旋转的累积角度。
inline constexpr int kDefaultExportQualityPercent = core::nums::engine::kDefaultQuality;
                                                          // 导出质量字段初值（model/document.h）：与 panels::kQualityDefaultPercent
                                                          // 同转引 Core 出厂默认，数值单点在 Core，本模块只按名取用。
} // namespace model

} // namespace idc::gui::nums
