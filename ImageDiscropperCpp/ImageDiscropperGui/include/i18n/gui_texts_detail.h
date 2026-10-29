// ============================================================================
// 文件：i18n/gui_texts_detail.h（GUI 常量层 · 字符串 · 后半：tooltip / status / msg）
// 作用：常量层拆分两半之一——收录三个**可译组**：
//       · tooltip —— 悬停 / 状态栏说明（桌面与移动两处入口同源）；
//       · status  —— 状态栏与进度提示（含 %N 占位符的回报整句）；
//       · msg     —— 弹窗正文 / 标题 / 确认问句 / 桥与相册发布的失败报因。
//       ident / menu / label 见同目录 gui_texts_base.h；gui_texts.h 为聚合转发头。
// 架构定位：GUI 顶层 i18n 基建；本半面向「运行时反馈与人读说明」，与基座的「契约 + 静态标签」分工。
// 命名 / 类型：k + 域 + 语义驼峰；inline constexpr char[]（UTF-8 无 BOM）；「&」「…」与 %N 编号逐字保留。
// 目录与对账：tooltip / status / msg 的英文表住 src/i18n/lang_en_detail.cpp（后半，与 menu/label
//       分两 TU 装配，installCatalogs 内先后登记）。tests/test_i18n_catalog.cpp 逐文件对账本层全部可译组。
// ============================================================================
#pragma once

namespace idc::gui::texts {
// 悬停提示（tooltip / statusTip，桌面与移动两处入口文案须同源）。
namespace tooltip {
// 以下为 src/app/main_window_menus.cpp 的菜单项悬停说明（与面板同名控件同源）。
inline constexpr char kNewCanvas[] = "新建空白或程序生成画布（尺寸、底色、图案）。";
inline constexpr char kExportRearrange[] = "按 cols×rows 重排输出；面板可调行列与单元尺寸。";
inline constexpr char kBurnAnnotation[] = "导出时把标注合成进像素（标注随切割被切开）。";
inline constexpr char kBurnImageLayers[] = "导出时并入图片图层（与图层面板「显示」仅预览正交）。";
inline constexpr char kConfirmRearrange[] = "重排有风险时导出前二次确认；关则直接导出。";
inline constexpr char kConfirmBurnIn[] = "有标注或图片图层却未勾选对应烧录时，导出前弹框提醒；关则直接导出。";
inline constexpr char kLoadConfig[] = "从配置文件还原切割/排序/导出参数；可撤销。";
inline constexpr char kSaveConfig[] = "把当前参数存为配置文件，同尺寸图像可复用。";
inline constexpr char kUndo[] = "撤销上一步（标注或选区/排序/导出参数）。";
inline constexpr char kRedo[] = "重做：恢复上一步被撤销的操作。";
inline constexpr char kClearSelection[] = "清除选区与切割线（网格/多矩形/单矩形）。";
inline constexpr char kResetAll[] = "清空图层/标注/预处理/选区，仅留底图。";
inline constexpr char kModeL3[] = "网格分割：铺满全图的网格 + 单元选择 + 排序。";
inline constexpr char kPolarityKeep[] = "保留选区内的单元（绿色遮罩）。";
inline constexpr char kPolarityRemove[] = "剔除选区内的单元（红色遮罩），保留其余部分。";
inline constexpr char kCanvasMarquee[] = "框选生成/拖动选区；标注与图片落位框不可拾取。";
inline constexpr char kCanvasImagePick[] = "点选 / 拖拽图片图层落位框调位。";
inline constexpr char kCanvasAnnoSelect[] = "点选 / 拖动 / 手柄变换既有标注。";
inline constexpr char kCanvasAnnoCreate[] = "用「标注→工具」所选形状在画布绘制新标注。";
inline constexpr char kSortReverse[] = "填充路径整体倒序（可与横/竖优先叠加）。";
inline constexpr char kSortSnake[] = "隔行（行优先）/隔列（列优先）反向。";
inline constexpr char kAddImageLayer[] = "从文件添加图片图层到画布。";
inline constexpr char kClearAllImageLayers[] = "清空全部叠加图片图层，仅保留底图。";   // 两处落点：图像菜单项（app/main_window_menus.cpp）、图层编辑面板按钮（panels/layer_edit_panel.cpp）——同值同角色，合一。
inline constexpr char kLayerUp[] = "把当前拾取的图片图层上移一层（向顶层）。";
inline constexpr char kLayerDown[] = "把当前拾取的图片图层下移一层（向底层）。";
inline constexpr char kLayerRemove[] = "删除当前拾取的图片图层。";
inline constexpr char kAnnoDeleteSelected[] = "删除当前选中的标注。";
inline constexpr char kAnnoFill[] = "切换选中标注的填充（无选中时设下次默认）。";
inline constexpr char kSnap[] = "拖动选区/边时吸附到图像边缘与中线，跨会话记住。";
inline constexpr char kAppearance[] = "自定义画布配色与交互偏好，跨会话记住。";
// 以下为导出面板（src/panels/export_panel.cpp）各控件悬停说明。
inline constexpr char kExportModeCombo[] = "分离＝每块一张；坍缩＝删整行/列后拼一张；重排＝按序填入新画布。";
inline constexpr char kNamingTemplate[] = "文件名模板：{name} 原名、{index} 序号（{index:03d} 补零）、{row}/{col} 行列号。";
inline constexpr char kMergeCols[] = "重排画布列数；「自动」按保留块数推导。";
inline constexpr char kMergeRows[] = "重排画布行数；「自动」按保留块数推导。";
inline constexpr char kMergeCellW[] = "重排单元宽度；「自动」用保留块原尺寸。";
inline constexpr char kMergeCellH[] = "重排单元高度；「自动」用保留块原尺寸。";
inline constexpr char kAutoGrid[] = "依保留块数自动推算行列数（接近正方形）并填入。";
inline constexpr char kMergeOrder[] = "重排填充顺序：行优先（逐行）或列优先（逐列）。";
inline constexpr char kMergeSnake[] = "蛇形铺排：隔行（行优先）或隔列（列优先）反向填充。";
inline constexpr char kMergeReverse[] = "倒序：从最后一个块开始反向填充。";
inline constexpr char kPadColor[] = "合并画布底色、Pad 余量补白（分离 / 合并导出一致）与有损格式（JPEG/WebP/BMP）压平透明像素的背景色，默认透明；点击选择。";
inline constexpr char kPreviewThumb[] = "输出图低分辨率预览（仅示意，导出为原分辨率）。";
inline constexpr char kBurnInHint[] = "勾选则导出时把标注画进图像；否则不含。";
inline constexpr char kLayersBurnInHint[] = "导出时把图片图层并入像素；与面板「显示」独立。";
inline constexpr char kExportButton[] = "按当前参数导出。";
// 以下为参数面板（src/panels/param_panel_pages.cpp）各控件悬停说明。
inline constexpr char kParamL1ShapeCombo[] = "选择保留区域的形状：矩形选框 / 整条横带 / 整条竖带。";
inline constexpr char kParamL2SubCombo[]   = "每矩形删除贯穿全图的十字带；多矩形取并集。";
inline constexpr char kParamRectList[]     = "画布拖拽追加/缩放矩形；选中项可在上方坐标框微调或删除。";
inline constexpr char kParamToGrid[]       = "以当前选区左上为基准、宽高为单元尺寸切到网格模式继续精修。";
inline constexpr char kGridOriginXSpin[]   = "基准点 x0：竖切割线的起始位置。";
inline constexpr char kGridOriginYSpin[]   = "基准点 y0：横切割线的起始位置。";
inline constexpr char kCellWidthSpin[]     = "单元宽 cw：相邻竖切割线的间距（像素）。";
inline constexpr char kCellHeightSpin[]    = "单元高 ch：相邻横切割线的间距（像素）。";
inline constexpr char kRemainderCombo[]    = "边缘残缺单元：丢弃、保留或补白。";
inline constexpr char kGridModeCombo[]     = "该轴的定义方式：周期（基准点 + 单元尺寸）/ 分割数（等分）/ 比例（权重）；两轴可各用各的。";
inline constexpr char kGridCountSpin[]     = "份数：把该方向等分成若干份（基准点恒 0），末份吸收取整误差；不得超过该方向像素数。";
inline constexpr char kGridWeightsEdit[]   = "权重：逗号分隔的正数或百分比，如 1,2,1 或 25%,50%,25%；只取相对比例，末份吸收取整误差。";
inline constexpr char kSortCombo[]         = "输出顺序：行优先 / 列优先 / 自定义。";
// 以下为图层编辑面板（src/panels/layer_edit_panel.cpp）各控件悬停说明。
inline constexpr char kLayerAddImage[]     = "添加图片图层（生成图案或导入图）；超出画布部分合成时裁剪。";
inline constexpr char kLayerMoveUp[]       = "把选中图层向顶层移动一层（更靠近前面）。";
inline constexpr char kLayerMoveDown[]     = "把选中图层向底层移动一层（更靠近后面）。";
inline constexpr char kLayerMoveTop[]      = "把选中图层移到最顶层（盖住其余所有图片图层）。";
inline constexpr char kLayerMoveBottom[]   = "把选中图层移到最底层（在所有图片图层之下、底图之上）。";
inline constexpr char kLayerEditRemove[]   = "删除选中图层。";   // 菜单侧同名动作的说明见 tooltip::kLayerRemove（拾取语境措辞不同）。
inline constexpr char kLayerCopy[]         = "复制选中图层到顶层（声明与原层同构、像素独立）。";   // 面板按钮与菜单共用。
inline constexpr char kLayerAnchorCombo[]  = "画布锚点：图层对齐到画布的哪个位置。";
inline constexpr char kLayerPivotCombo[]   = "图片枢轴：图层自身以哪个部位对齐到锚点。";
inline constexpr char kLayerOffsetXSpin[]  = "自由偏移 X（叠加在锚点/枢轴结果之上）。";
inline constexpr char kLayerOffsetYSpin[]  = "自由偏移 Y。";
inline constexpr char kLayerWidthSpin[]    = "目标宽（0＝用原生尺寸不缩放）。";
inline constexpr char kLayerHeightSpin[]   = "目标高（0＝用原生尺寸不缩放）。";
inline constexpr char kLayerOpacitySpin[]  = "不透明度 0–255（255 不透明），影响预览与导出。";
// 以下为预处理面板（src/panels/preprocess_panel.cpp 与 preprocess_panel_sections.cpp）各控件悬停说明。
inline constexpr char kPreprocessTarget[]   = "预处理作用于当前目标：选中图层/底图（仅色彩）/画布。";
inline constexpr char kResizeEdgeSpin[]     = "目标边长（像素）；勾选保持宽高比时改宽联动高。";
inline constexpr char kResizeScaleSpin[]    = "按当前画布尺寸目标（无则原图尺寸）的百分比缩放。";
inline constexpr char kApplySizeBtn[]       = "把工作图设为上面输入的绝对尺寸。";
inline constexpr char kScaleSizeBtn[]       = "按上面百分比缩放画布尺寸。";
inline constexpr char kClearSizeBtn[]       = "取消画布尺寸，回原图尺寸（保留轻操作队列）。";
inline constexpr char kResetPreprocessBtn[] = "丢弃画布尺寸与全部预处理，恢复原图。";
inline constexpr char kGeoAddBtn[]          = "把上面的几何操作追加到几何列表末尾。";
inline constexpr char kGeoReplaceBtn[]      = "用当前配置就地替换几何列表中选中的步骤。";
inline constexpr char kColorAddBtn[]        = "把上面的色彩操作追加到色彩列表末尾。";
// 同一按钮的正常态 / 受阻态两套说明（队列已产出灰度时置灰「黑白」「色道分离」）。
inline constexpr char kColorAddBlocked[]    = "队列已产出灰度，此操作冗余或无效果，不可添加。";
inline constexpr char kColorReplaceBtn[]    = "用当前配置就地替换色彩列表中选中的步骤。";
inline constexpr char kGeoList[]            = "几何操作队列（旋转/翻转）；选中后可删除/移动/更新。";
inline constexpr char kColorList[]          = "色彩操作队列（黑白/反色/分离）；选中后可删除/移动/更新。";
inline constexpr char kGrayOutItem[]        = "队列已产出灰度：此操作冗余或对灰度无操作";   // 下拉置灰项的 tooltip。
// 以下为标注属性页（src/panels/annotation_prop_panel.cpp）各控件悬停说明。
inline constexpr char kStrokeColorBtn[]     = "标注描边颜色（RGBA）。";
inline constexpr char kStrokeWidthSpin[]    = "描边线宽（原图像素），烧录时按此粗细合成";
inline constexpr char kFillCheckTip[]       = "形状是否填充内部；直线/文字不可填充（置灰）。填充色见下行。";
inline constexpr char kFillColorBtn[]       = "标注填充颜色（与描边独立，可填充时可用）。";
inline constexpr char kTextEditField[]      = "文字标注的内容（选择文字工具后单击落点使用）";
inline constexpr char kFontSizeSpin[]       = "文字标注的字号（像素）";
inline constexpr char kCornerRadiusSpin[]   = "圆角半径 0＝直角；仅多边形/折线顶点生效（其余置灰）。";
inline constexpr char kScaleXSpin[]         = "水平缩放 100%＝原尺寸，负值＝翻转；与画布手柄实时联动。";
inline constexpr char kScaleYSpin[]         = "垂直缩放 100%＝原尺寸，负值＝翻转；与画布手柄实时联动。";
inline constexpr char kRotateSpin[]         = "旋转角度（相对原方向累积）；与画布手柄实时联动。";
inline constexpr char kStackRaiseBtn[]      = "将选中标注向上移动一层（更靠近顶层）。";
inline constexpr char kStackLowerBtn[]      = "将选中标注向下移动一层（更靠近底层）。";
inline constexpr char kStackToFrontBtn[]    = "将选中标注移到最顶层。";
inline constexpr char kStackToBackBtn[]     = "将选中标注移到最底层。";
inline constexpr char kDeleteSelectedAnnoBtn[] = "删除选中标注（无选中置灰，可撤销）。";
inline constexpr char kCopySelectedAnnoBtn[] = "复制选中标注到顶层（错位一小段便于直接拖走）。";
inline constexpr char kAnnoCopySelected[]   = "复制所选标注到顶层（错位一小段便于直接拖走）。";   // 菜单侧说明（措辞同面板按钮）。
inline constexpr char kClearAllAnnoBtn[]    = "清除全部标注（不影响底图/图层/预处理，可撤销）。";
// 以下为工具面板（src/panels/mode_tool_panel.cpp）的形状悬停说明（与菜单侧 tooltip 不共用：面板说「怎么拖」，菜单说「在哪儿选」）。
inline constexpr char kModeMarqueeBtn[]     = "框选生成/拖动选区；标注与图片落位框不可拾取";
inline constexpr char kModeImagePickBtn[]   = "点选/拖拽图片图层落位框调位。";
inline constexpr char kModeAnnoSelectBtn[]  = "点选/拖动/手柄变换既有标注；禁框选。";
inline constexpr char kModeAnnoCreateBtn[]  = "在下方选择形状后于画布绘制新标注";
inline constexpr char kToolRectBtn[]        = "两点拖拽画矩形（圆角>0 得圆角）";
inline constexpr char kToolSquareBtn[]      = "落点为顶点，拖拽定边长画正方形（圆角>0 得圆角）";
inline constexpr char kToolRhombusBtn[]     = "起点为中心，拖拽绘制菱形";
inline constexpr char kToolCircleBtn[]      = "起点为圆心，拖拽绘制圆形";
inline constexpr char kToolEllipseBtn[]     = "落点为中心，拖拽定两半轴绘制椭圆";
inline constexpr char kToolIsoTriBtn[]      = "起点为底边中点，拖拽绘制等腰三角形";
inline constexpr char kToolEquTriBtn[]      = "落点为底边中点，拖拽画等边三角形（顶角朝光标）";
inline constexpr char kToolRightTriBtn[]    = "落点为直角顶点，拖拽沿两轴定两直角边长";
inline constexpr char kToolIsoRightTriBtn[] = "落点为直角顶点，拖拽画等腰直角三角形";
inline constexpr char kToolLineBtn[]        = "两点拖拽绘制直线段";
inline constexpr char kToolPolylineBtn[]    = "连续单击追加顶点，Esc 或切换工具收笔";
inline constexpr char kToolTextBtn[]        = "单击落点，输入文字内容后添加";
inline constexpr char kToolBrushBtn[]       = "按住拖动绘制自由路径，Esc 或切换工具收笔";
// 以下为图层面板（src/panels/layer_panel.cpp）七行显示开关的悬停说明（与视图菜单同名开关措辞不同，各自逐字保留）。
inline constexpr char kLayerShowBase[]      = "显示 / 隐藏底图（仅影响画布预览，不影响导出）";
inline constexpr char kLayerShowImage[]     = "显示/隐藏图片图层（隐藏则不参与合成与导出）";
inline constexpr char kLayerShowAnno[]      = "显示/隐藏标注图层（矢量叠加，不改底图）";
inline constexpr char kLayerShowMask[]      = "显示/隐藏保留(绿)/删除(红)预览遮罩（不影响导出）";
inline constexpr char kLayerShowGrid[]      = "显示 / 隐藏网格线与切割线（同一开关：L3 为网格线，L1/L2 为贯穿全图的橙色切割线即选区边的延伸）";
inline constexpr char kLayerShowSelection[] = "显示/隐藏选区边框与手柄；隐藏后选区不可编辑";
inline constexpr char kLayerShowNumber[]    = "显示 / 隐藏单元编号角标（仅自定义排序时出现）";
// 移动端专属控件（src/mobile/mobile_step_pad.cpp）：8 向步进盘的倍率切换按钮。
inline constexpr char kStepFactorBtn[]      = "切换步进倍率（对应桌面 Shift 大步）";
// 左侧模式面板（src/panels/left_panel.cpp）的五个按钮：与菜单侧 tooltip 措辞不同（面板无快捷键说明），各自逐字保留。
inline constexpr char kModeL1Btn[]          = "保留选框内区域得裁剪结果，默认模式。";
inline constexpr char kModeL2Btn[]          = "每矩形删除贯穿全图的十字带；多矩形取并集。";
inline constexpr char kModeL3Btn[]          = "以基准点 + 单元尺寸铺满全图，选择单元并排序导出。";
inline constexpr char kPolarityKeepBtn[]    = "保留选区内的单元（绿色遮罩）。";   // 与 tooltip::kPolarityKeep 同值（两侧均不含快捷键说明）。
inline constexpr char kPolarityRemoveBtn[]  = "剔除选区内的单元（红色遮罩），保留其余部分。";   // 同上（tooltip::kPolarityRemove）。
// 落位行（src/app/forms/placement_row.cpp）的偏移框说明：与图层编辑面板的 tooltip::kLayerOffsetXSpin / kLayerOffsetYSpin
// 措辞微异（本组无「结果」二字、句末无句号），故不合并。
inline constexpr char kPlaceOffsetXTip[]    = "自由偏移 X（叠加在锚点/枢轴之上）";
inline constexpr char kPlaceOffsetYTip[]    = "自由偏移 Y";
// 桌面工具栏「重置全部」按钮（src/app/main_window_ui.cpp）。
inline constexpr char kTbResetAllTip[]      = "一键重置：清图层/标注/预处理/选区，仅留底图。";
// 外观 / 交互对话框（src/app/appearance_dialog.cpp）七行数值框的悬停说明。
inline constexpr char kHandleSizeSpin[]     = "手柄/抓边/点选阈值的屏幕像素；「自动」按平台默认。";
inline constexpr char kScrollMarginSpin[]   = "画布外可平移幅度，按视口边百分比；「不留」到图像边缘即止。";
inline constexpr char kZoomStepSpin[]       = "每次放大/缩小按钮的倍率增量（滚轮步进固定，不随此项）。";
inline constexpr char kZoomMinSpin[]        = "允许的最小缩放倍率（低于此自动收敛）。";
inline constexpr char kZoomMaxSpin[]        = "允许的最大缩放倍率（高于此自动收敛）。";
inline constexpr char kNewWidthSpin[]       = "「新建画布」对话框默认宽度（每次新建确定后自动记住）。";
inline constexpr char kNewHeightSpin[]      = "「新建画布」对话框默认高度（每次新建确定后自动记住）。";
// 外观 / 交互对话框主题下拉的悬停说明。
inline constexpr char kThemeCombo[]         = "应用外壳深浅配色：跟随系统或固定浅/深；与画布配色无关。";
// 图案参数表单（src/app/forms/pattern_form.cpp）三个控件的悬停说明。
inline constexpr char kPatternBgSwatch[]    = "选择底色（可设透明度，0＝全透明）";
inline constexpr char kPatternFgSwatch[]    = "选择前景/图案色（可设透明度，非纯色生效）";
inline constexpr char kPatternSeedSpin[]    = "噪声随机种子：同种子+同尺寸恒同图";
// 新建画布对话框（src/app/new_canvas_dialog.cpp）两级落位与附加图片区的悬停说明。
inline constexpr char kLayerPlaceRowTip[]   = "图层框相对底图的落位（图层框摆到底图哪里）。";
inline constexpr char kImagePlaceRowTip[]   = "图片相对图层框的落位（源图摆进图层框哪里）。";
inline constexpr char kAttachImageTip[]     = "勾选后把一张源图按适应方式放进图层框、再按图层落位摆到底图上；\n"
                                              "不勾选即可直接导出纯图案画布。";
inline constexpr char kPickSourceImageBtn[] = "选择要附加的源图（PNG/JPG/BMP/WebP 等），由引擎解码";
} // namespace tooltip

// 状态栏与进度提示（src/app/status_bar.cpp、各 controller）。
namespace status {
// 导出回报（app/controllers/export_controller.cpp 桌面分支 + mobile/android_export_publish.cpp 相册发布，均经 `StatusBar::notify`）：
inline constexpr char kExportNoImage[]      = "无图像可导出，请先打开一张图像。";   // 前置校验：无图。
inline constexpr char kExportNoSelection[] = "请先在画布创建选区";                       // 前置校验：无选区（无尾标点，与其他回报不一致的原文如此）。
inline constexpr char kExportOk[]            = "导出成功：%1";                 // 直写成功（路径已定）。
inline constexpr char kExportFailed[]        = "导出失败：%1";                 // Core 导出报错（携报因）。
inline constexpr char kExportNoOutputFiles[] = "导出失败：没有产生输出文件";   // 发布层拿到空目录。
inline constexpr char kExportUnknownError[]  = "未知错误";                     // 发布层未给出 err 文本时的占位报因。
// 以下为 Android 相册发布专属（仅 Q_OS_ANDROID 编译；「兜底」指复制到应用文档目录）：
inline constexpr char kExportFallbackCopyFailed[] = "导出失败：%1（且兜底保存失败，临时文件：%2）";
inline constexpr char kExportFallbackSaved[]      = "导出到相册/所选位置失败（%1），已保存到应用目录：%2";
inline constexpr char kExportGalleryFailedAll[]   = "导出失败（%1），文件已保存到应用目录：%2";
inline constexpr char kExportGalleryOkSingle[]    = "导出成功：相册 Pictures/%1/%2";
inline constexpr char kExportGalleryOkSingleDup[] = "导出成功：相册 Pictures/%1/%2（同名已存在，实际文件名 %3）";
inline constexpr char kExportGalleryOkMulti[]     = "导出成功：相册 Pictures/%1（%2 张）";
inline constexpr char kExportGalleryPartial[]     = "部分成功：已导出 %1/%2 个文件；失败文件已保存到应用目录：%3";
// 会话回报（app/controllers/session_controller.cpp：打开 / 新建 / 图层增删 / 全重置）：
inline constexpr char kSessionOpened[]           = "已打开：%1（%2×%3）";   // 路径 + 宽×高。
inline constexpr char kSessionOpenFailed[]       = "打开失败：%1";
inline constexpr char kSessionOpenContentFailed[] = "打开失败：无法读取该内容提供者地址";   // Android SAF 内容复制失败。
inline constexpr char kSessionNewFailedBadSize[] = "新建失败：尺寸非法";               // Core 回空图（超护栏）。
inline constexpr char kSessionNewOk[]            = "已新建：%1（保留图层/标注/色彩预处理）";   // %1 为对话框汇总行。
inline constexpr char kSessionNeedImageForLayer[] = "请先打开或新建一张图像，再添加图片图层。";
inline constexpr char kSessionLayerAdded[]       = "已添加图片图层（已切到「图片拾取」，可在画布上拖拽落位框）";
inline constexpr char kSessionLayerCopied[]      = "已复制图片图层（置于顶层）";
inline constexpr char kSessionNoLayersToClear[]  = "当前无图片图层可清空";
inline constexpr char kSessionLayersCleared[]    = "已清空全部图片图层（保留底图/标注/预处理）";
inline constexpr char kSessionNothingToReset[]   = "无图像可重置";
inline constexpr char kSessionAllReset[]         = "已重置全部（保留底图）";
inline constexpr char kSessionAllResetNoBase[]   = "已重置全部（含底图，画布已清空）；可「打开/新建」重新载入图像";
// 配置存取回报（app/controllers/config_controller.cpp）：
inline constexpr char kConfigSaved[]        = "配置已保存：%1";
inline constexpr char kConfigSaveFailed[]   = "保存配置失败：%1";
inline constexpr char kConfigLoaded[]       = "配置已加载：%1";
inline constexpr char kConfigLoadFailed[]   = "加载配置失败：%1";
inline constexpr char kDegradedBase[] = "，底图引用的位图未能载入（已按白/透明底显示）";   // 追在 kConfigLoaded 之后：空文档重载 File/placement 底但位图不可得（配置不含位图、GUI 无 --input 通道）。
inline constexpr char kDegradedLayersFmt[] = "，其中 %1 个图层资源未能载入（已按空占位降级）";   // 追在 kConfigLoaded 之后：File 层源缺失/越界/解码失败计数（%1 失败数）。
inline constexpr char kConfigBundledFmt[] = "，图层资产已打包至 %1";   // 追在 kConfigSaved 之后：保存配置时勾选打包且成功。
inline constexpr char kConfigBundleFailed[] = "打包图层资产失败：%1";  // 保存配置打包失败（越界名 / 缺像素 / 写盘失败），配置 JSON 已写出。
inline constexpr char kConfigNoneSaved[]    = "没有已保存的配置，请先在「更多」中保存配置。";   // Android 列表为空。
// 右侧参数面板的只读回显（src/panels/param_panel.cpp）：模板携占位符，由 Document 实时数值填入。
inline constexpr char kGridDerivedInfo[]   = "网格：%1 行 × %2 列 · 共 %3 单元";   // 行列数由 Core 依图像边界推导。
inline constexpr char kGridPendingInfo[]   = "网格：待计算";                        // 尚未能推导出行列时的初值。
inline constexpr char kGridSelectedInfo[]  = "已选：%1 单元";                        // 面板初值与同步回显均取本模板（初值传 0）。
inline constexpr char kGridNoPitchHint[]   = "两轴均为分割数/比例：恰好铺满、无残缺单元，余量策略不生效。";   // 余量策略组合框置灰时的同屏说明。
inline constexpr char kRectListItem[]      = "矩形 %1：(%2,%3)-(%4,%5)";           // 多矩形列表行文本（列表重建与单行刷新两处同源）。
inline constexpr char kCollapseHintInit[]  = "坍缩可行性：请先框选选区。";         // PreviewController 回灌前的占位提示。
inline constexpr char kCollapseHintOk[]    = "坍缩可行：剩余块可紧贴拼成一张图。";
inline constexpr char kCollapseHintFail[]  = "坍缩不可行：%1（导出将自动改用分离导出）";
// 预处理面板（src/panels/preprocess_panel.cpp）的实时回显与队列行文本。
inline constexpr char kTargetLabelFmt[]    = "当前目标：%1";              // 三元目标名见 label::kImageLayerName / kTargetCanvasName 与 menu::kShowBase。
inline constexpr char kTargetLabelInit[]   = "当前目标：画布";            // 尚未同步时的初值（与模板+「画布」同字）。
inline constexpr char kCurrentSizeFmt[]    = "当前输出尺寸: %1×%2";
inline constexpr char kCurrentSizeUnknown[]= "当前输出尺寸: —×—";         // 无图时的占位回显。
inline constexpr char kStepInvertDesc[]    = "色道反色 [%1]";             // %1 为 RGB 三位遮码（'1'/'0'）。
inline constexpr char kStepSplitDesc[]     = "色道分离 [%1]";
inline constexpr char kStepRotateDesc[]    = "旋转 %1°";
// 黑白步的行文本无独立条目：同字同义，直接用 label::kColorTypeGray；水平/垂直翻转行文本用 menu::kFlipHorizontal / kFlipVertical。
inline constexpr char kStepUnknown[]       = "未知步骤";                 // PreprocessConfig 无对应分支时的兼容回退文本。
// 色块按钮的实时回显（src/panels/annotation_prop_panel.cpp::applySwatch）：四个数字依次为红/绿/蓝/透明度。
inline constexpr char kSwatchColorText[]   = "颜色(%1,%2,%3,%4)";
// 预处理回报（src/app/controllers/preprocess_controller*.cpp）：「已… 」为状态栏一句话回报，「正在… 」为忙碌对话框正文。
inline constexpr char kPrepQueueLoaded[]      = "已载入预处理队列";
inline constexpr char kPrepQueueReplayBusy[]  = "正在按载入的队列重放图像，请稍候…";
inline constexpr char kPrepCanvasApplied[]    = "已应用画布预处理";
inline constexpr char kPrepStepAdded[]        = "已添加预处理步骤";
inline constexpr char kPrepStepUpdated[]      = "已更新预处理步骤";
inline constexpr char kPrepStepRemoved[]      = "已删除预处理步骤";
inline constexpr char kPrepStepMoved[]        = "已调整步骤顺序";
inline constexpr char kPrepBaseReset[]        = "已重置底图预处理";
inline constexpr char kPrepLayerReset[]       = "已重置该图层预处理";
inline constexpr char kPrepReset[]            = "已重置预处理，恢复原图";
inline constexpr char kPrepResizeSet[]        = "已设置画布尺寸 %1×%2";
inline constexpr char kPrepResizeBusy[]       = "正在缩放到目标尺寸，请稍候…";
inline constexpr char kPrepResizeScaled[]     = "已按比例缩放至 %1×%2";
inline constexpr char kPrepResizeScaleBusy[]  = "正在按比例缩放图像，请稍候…";
inline constexpr char kPrepResizeCleared[]    = "已清除画布尺寸";
inline constexpr char kPrepRotateAdded[]      = "已添加旋转 %1°";
inline constexpr char kPrepFlipHAdded[]       = "已添加水平翻转";
inline constexpr char kPrepFlipVAdded[]       = "已添加垂直翻转";
inline constexpr char kPrepGrayAdded[]        = "已添加黑白";
inline constexpr char kPrepInvertAdded[]      = "已添加按通道反色";
inline constexpr char kPrepSplitAdded[]       = "已添加按通道分离";
// 提交链与图层重组（app/controllers/preprocess_controller_commit.cpp / _canvas.cpp）：失败回报携 true 走错误色显示。
inline constexpr char kPrepFailedEmpty[]      = "预处理失败：结果为空图";   // 两处落点：commitPipeline 的重放后与回灌前两道空图门。
inline constexpr char kPrepReplayBusy[]       = "正在重放预处理队列，请稍候…";   // 忙碌框正文（resize 前缀真变且确有重采样时才弹）。
inline constexpr char kLayerComposeFailed[]   = "图层合成失败（空图）";    // app/controllers/preprocess_controller_canvas.cpp::recomposeLayerCanvas。
inline constexpr char kLayerRecomposed[]      = "已重组多层画布";
inline constexpr char kLayerRecomposeBusy[]   = "正在重组画布，请稍候…";
// 状态栏常驻回显（src/app/status_bar.cpp）：初值与逐帧刷新模板成对（「坐标: 」的半角冒号加空格为原文）。
inline constexpr char kStatusReady[]       = "就绪";                 // 提示标签初值（无任何回报时）；预览刷新成功时的 setHint 三处共用（preview_controller.cpp）。
inline constexpr char kCoordInit[]         = "坐标: (-, -)";
inline constexpr char kCoordFmt[]          = "坐标: (%1, %2)";
inline constexpr char kColorInit[]         = "RGBA(-,-,-,-)";      // 两处落点：构造初值与 clearColor（鼠标移出图像）。
inline constexpr char kColorFmt[]          = "RGBA(%1,%2,%3,%4)";  // 四值依次为红/绿/蓝/透明度。
inline constexpr char kZoomInit[]          = "缩放: 100%";
inline constexpr char kZoomFmt[]           = "缩放: %1%";
// 参数撤销 / 重做回报（src/app/controllers/doc_history.cpp）：「已撤销 / 已重做」与菜单同名项同义不同长度，本组只收回报句。
inline constexpr char kUndone[]            = "已撤销";
inline constexpr char kRedone[]            = "已重做";
inline constexpr char kUndoNone[]          = "没有可撤销的参数变更";
inline constexpr char kRedoNone[]          = "没有可重做的参数变更";
// 导出面板重排区的内联警告（src/panels/export_panel_merge.cpp::rearrangeWarning）：多条用换行拼接后加前缀红字显示。
inline constexpr char kWarnPrefix[]            = "警告：";   // 与弹窗标题 msg::kRearrangeWarnTitle（「重排参数警告」）同事件两套呈现。
inline constexpr char kRearrangeColsShortFmt[] = "画布 %1×%2=%3 格 < 保留块数 %4，将自动增加行数以容纳全部块（与设置的画布大小不符）。";
inline constexpr char kRearrangeCellWShortFmt[]= "单元宽 %1 小于网格单元宽 %2，相邻块会相互覆盖或越界被裁剪。";
inline constexpr char kRearrangeCellHShortFmt[]= "单元高 %1 小于网格单元高 %2，相邻块会相互覆盖或越界被裁剪。";
// 画布协调器的常驻提示（src/app/controllers/canvas_coordinator.cpp）：把当前交互模式与预处理目标拼为一行。
// 模式四态名复用 menu::kCanvas*，目标三名复用 label::kImageLayerName / menu::kShowBase / label::kTargetCanvasName。
inline constexpr char kModeTargetHintFmt[]  = "交互模式：%1  |  预处理目标：%2";   // 分隔符两侧各两个空格，原文如此。
inline constexpr char kCellsCleared[]       = "已清除单元选择";
inline constexpr char kSelectionCleared[]   = "已清除选区与切割线";   // 两处落点：L3 分支与非 L3 分支的同句回报。
// 预览刷新的状态提示与输出预览信息行（src/app/controllers/preview_controller.cpp::refreshPreview）。
inline constexpr char kMaskShown[]           = "已显示预览遮罩";   // 开关回报（onToggleMasks）：上下文菜单侧 menu::kCtxShowMask / kCtxHideMask 说动作，本组说结果。
inline constexpr char kMaskHidden[]          = "已隐藏预览遮罩";
inline constexpr char kNeedImage[]           = "请打开图像";         // 状态栏：无图时的行动提示。
inline constexpr char kNoImageLoaded[]       = "尚未加载图像。";     // 导出面板信息行：同一情形的静态说明。
inline constexpr char kDragAppendRect[]      = "在画布上拖拽以追加矩形";
inline constexpr char kDragCreateSel[]       = "在画布上拖拽以创建选区";
inline constexpr char kWaitingRect[]         = "等待矩形…";
inline constexpr char kWaitingSelection[]    = "等待选区…";
inline constexpr char kPreviewNoResultFmt[]  = "无有效结果：%1";     // 三处落点：L3 / L2 多矩形 / 其余分支的失败回显。
inline constexpr char kGridPreviewFmt[]      = "网格: %1×%2 · 输出画布 %3×%4 · 保留 %5 块";
inline constexpr char kMultiRectPreviewFmt[] = "多矩形: %1 个 · 输出画布 %2×%3 · 保留 %4 块";
inline constexpr char kOutPreviewFmt[]       = "输出画布: %1×%2 · 保留 %3 块 · %4";   // 末位取下方两条之一。
inline constexpr char kCollapseYes[]         = "可坍缩";
inline constexpr char kCollapseNo[]          = "需重排";
// 新建画布 / 添加图层的定稿忙碌框正文（src/app/new_canvas_dialog.cpp::accept，总像素≥阈值才弹）。
inline constexpr char kNewCanvasGenerateBusy[] = "正在生成图像，请稍候…";
} // namespace status

// 弹窗正文：确认 / 警告 / 错误 / 结果提示（含 "%1" 占位符的模板整条集中）。
namespace msg {
// 合并重排的导出前二次确认（app/controllers/export_controller.cpp；标题与正文后缀分两条，正文本体由面板实时给出）。
inline constexpr char kRearrangeWarnTitle[]  = "重排参数警告";            // QMessageBox::warning 标题。
inline constexpr char kRearrangeAskContinue[] = "\n\n仍要继续导出吗？";  // 拼在面板警告文本之后的追问句。
inline constexpr char kRearrangeDontRemind[] = "下次不再提醒";   // QMessageBox::setCheckBox 内嵌复选框：勾选即关掉 UiPrefs::confirmRearrangeRisk 偏好，下次导出不再弹。
// 导出前「未烧录提醒」（app/controllers/export_controller.cpp）：画布有标注/图层但未勾对应烧录时弹框，
//   三键：开烧录并导出（视同用户勾选后继续）/ 按现状导出（不改开关）/ 取消（中止）。可经「导出设置·未烧录时导出前提醒」关闭。
inline constexpr char kBurnSkipTitle[]        = "导出未包含标注/图层";   // QMessageBox::warning 标题。
inline constexpr char kBurnSkipBodyAnno[]     = "画布上有标注，但未勾选「导出时烧录标注」，导出结果将不含标注。";
inline constexpr char kBurnSkipBodyLayers[]   = "画布上有图片图层，但未勾选「导出时烧录图片」，导出结果将不含图层。";
inline constexpr char kBurnSkipBodyBoth[]     = "画布上有标注与图片图层，但二者均未勾选烧录，导出结果将都不含它们。";
inline constexpr char kBurnSkipEnableExport[] = "开烧录并导出";   // 视同用户勾选对应烧录后继续导出。
inline constexpr char kBurnSkipExportAsIs[]   = "按现状导出";     // 不改烧录开关，直接导出（不含未烧项）。
inline constexpr char kBurnSkipDontRemind[]   = "下次不再提醒";   // QMessageBox::setCheckBox 内嵌复选框：勾选即关掉 UiPrefs::confirmBurnInOnExport 偏好，下次不再弹。
// 外观对话框「快捷键」Tab 的编辑校验（app/appearance_dialog.cpp）：组合键缺修饰 / 冲突改绑询问。
// 与菜单命令名无关（那些复用 menu::k*），本组只收编辑区的提示与弹窗文本。
inline constexpr char kShortcutNeedModifier[]  = "组合键须包含 Ctrl 或 Alt";   // 组合键类命令缺 Ctrl/Alt 时的弹提示（回退本行上一次值）。
inline constexpr char kShortcutConflictTitle[] = "快捷键冲突";                 // QMessageBox::question 标题。
inline constexpr char kShortcutConflictBody[]  = "该组合已被「%1」占用，是否改绑清除？";   // %1 为占用者的命令名（经 i18n::qs 取）。
// 「恢复默认」回落后的撞键提示（app/appearance_dialog.cpp::reportShortcutConflicts）：重置可能引入冲突、由用户自行处置，标题复用 kShortcutConflictTitle。
inline constexpr char kShortcutResetConflictBody[] = "恢复默认后检测到快捷键冲突（下列命令两两占用同一键），请到对应行自行调整：\n%1";   // %1 为冲突命令名对列表。
// 图层与会话的破坏性操作确认（app/controllers/session_controller.cpp，均为 Yes/No、默认 No）：
inline constexpr char kClearLayersTitle[] = "清空全部图层";
inline constexpr char kClearLayersAsk[]   = "确定要清空全部 %1 个图片图层吗？将仅保留底图（标注与预处理保留）。";
inline constexpr char kResetAllTitle[]    = "重置全部";   // 工具栏动作短名同字见 label::kTbResetAll（对话框标题 / 控件文字角色不同，各立一条）。
inline constexpr char kResetAllAsk[]      = "将清空：图片图层、全部标注、全部预处理、选区。\n默认保留底图；勾选下方可一并删除（清空为无图）。确定继续？";
inline constexpr char kResetAllDeleteBase[] = "同时删除底图（清空为无图）";   // 「重置全部」对话框内嵌复选框（QMessageBox::setCheckBox）：勾选则连底图一并删、文档回空白态。
// 清除全部标注的二次确认（app/controllers/annotation_coordinator.cpp::onAnnoClearAll，Yes/No、默认 No）。
// 标题与面板按钮 label::kClearAllAnnoButton 同字但角色不同（对话框标题 / 控件文字），各立一条。
inline constexpr char kClearAnnosTitle[]  = "清除全部标注";
inline constexpr char kClearAnnosAsk[]    = "确定要清除全部 %1 个标注吗？此操作可用「撤销标注」回退。";
// 内核调用桥的失败报因（src/model/engine_bridge.cpp）：本地前置校验产出的中文原因，逐条透传给状态栏 / 弹窗。
// 注：Core 自身的错误文本不在本层重复定义（那些由 Core 返回，本层只转发）。
inline constexpr char kBridgeEmptyImagePath[]   = "路径为空，无法打开图像";
inline constexpr char kBridgeReadImageFailed[]  = "无法读取图像：%1";
inline constexpr char kBridgeNoOutputPath[]     = "未指定输出路径";
inline constexpr char kBridgeWriteOutputFailed[]= "导出失败：无法写入 %1";
inline constexpr char kBridgeNoConfigPath[]     = "未指定配置文件路径";   // 两处落点：保存配置与加载配置的前置校验。
inline constexpr char kBridgeWriteConfigFailed[]= "无法写入配置文件：%1";
inline constexpr char kBridgeReadConfigFailed[] = "无法读取或解析配置文件：%1";
// Android 相册发布（MediaStore）的失败报因（src/mobile/android_media_store.cpp，仅 Q_OS_ANDROID 编译）：
// 逐条经 status 回报拼进导出结果文案（见 status::kExportFallback* 系列）。
inline constexpr char kMediaNoResolver[]     = "无法获取系统内容解析器";
inline constexpr char kMediaNoStore[]        = "无法获取系统媒体库";
inline constexpr char kMediaOpenWriteFailed[]= "无法打开目标位置写入";   // 两处落点：输出流为空与写入前异常检查。
inline constexpr char kMediaReadTempFailed[] = "无法读取临时导出文件：%1";
inline constexpr char kMediaReadTempError[]  = "读取临时导出文件失败";
inline constexpr char kMediaWriteTargetFailed[] = "写入目标位置失败";
inline constexpr char kMediaApiTooLow[]      = "相册发布需要 Android 10（API 29）及以上";
inline constexpr char kMediaInsertFailed[]   = "媒体库插入失败（格式可能不被支持）";
inline constexpr char kMediaFinalizeFailed[] = "媒体库条目完成失败";
inline constexpr char kMediaCleanupFailed[]  = "清理未完成条目失败";
// 命令行自述（app/main.cpp：--help / --version 与位置参数说明）。
inline constexpr char kCliDescription[] = "ImageDiscropper 图像切割 GUI";         // parser.setApplicationDescription。
inline constexpr char kCliImageArgHelp[] = "可选：启动时自动打开的图像路径。";   // 位置参数说明行。
// 帮助菜单正文（src/app/main_window_menus.cpp::buildHelpMenu；标题复用 menu::kUsageGuide / menu::kAbout）。
// 多段拼接待逐字保留（行首空格与空行是排版的一部分）；\n 在字节串里同一含义，经 fromUtf8 后逐字节相同。
inline constexpr char kHelpUsageBody[] =
    "1) 文件→打开图像，或文件→新建画布（尺寸/底色/程序化图案，可附加源图与两级落位；图案/图片/节点外观可加预处理与透明度）。\n"
    "2) 选择模式：标准提取 / 反向剔除 / 网格分割；极性「保留/删除框内」。\n"
    "3) 标准提取/反向剔除：在画布上拖拽出选区（反向剔除可多矩形），形状/方式在「参数」页；橙色切割线贯穿全图，绿色为保留、红色为删除。\n"
    "   拖动选区/四角手柄/切割线调整；方向键微调（Shift 大步）。\n"
    "4) 网格分割：「参数」页设定网格定义（周期/等分/权重，每轴三选一）、余量与排序；\n"
    "   周期轴的基准点可在画布上直接拖动基准手柄调整；全选/反选/清空单元控制输出。\n"
    "5) 预处理：图像菜单或「预处理」页——目标可为画布/底图/选中图层；旋转/翻转/黑白/反色/色道分离 + 独立画布尺寸（resize），队列可增删改序。\n"
    "6) 标注：标注→工具选择形状后在画布绘制（亦可用左侧工具面板）；选中标注可拖拽平移、拖手柄缩放/旋转；「标注」属性页改样式，复制/删除选中。\n"
    "7) 多层画布：图层→添加图片图层；图片拾取态可拖拽落位框，图层菜单可排序/复制/删除图层；「图层」面板控制各层显隐。\n"
    "8) 导出：文件→导出；文件→导出设置可切分离/坍缩/重排、格式与烧录开关，「导出」页有完整参数。\n"
    "9) 文件→保存配置/加载配置：整套参数（含标注与图层）可保存/加载、可复用、可撤销。\n\n"
    "快捷键（模式/极性/清除/删除、撤销重做、打开/新建/导出、缩放）均可在「视图→外观/交互…」对话框的\n"
    "「快捷键」页查看与自定义（改绑即时生效、跨会话保存），此处不再罗列以免改绑后失效。\n"
    "方向键微调选区（Shift 大步）、绘制态按 Esc 收笔为固定操作键，不在自定义之列。";
// 移动「更多」菜单的使用说明正文（src/mobile/mobile_shell_ui.cpp）：与桌面正文同构但把手势替代了快捷键；
// 标题与「关于」正文复用 menu::kUsageGuide / msg::kAboutBody（逐字同形）。
inline constexpr char kMobileHelpBody[] =
    "1) 工具条「打开」载入图像，「新建」从零建画布（尺寸/底色/图案，可附加源图与两级落位；图案/图片/节点外观可加预处理与透明度）。\n"
    "2) 抽屉「模式」页选模式 标准提取 / 反向剔除 / 网格分割与极性保留/删除；画布拖拽框选。\n"
    "3) 「参数」页设切割参数：形状 / 方式；网格分割另有网格定义（周期/等分/权重，每轴三选一）、余量与排序；周期轴基准点可在画布上拖动基准手柄调整。\n"
    "4) 「模式」页选标注工具后在画布绘制（选中标注可拖拽平移、拖手柄缩放/旋转）。\n"
    "5) 「预处理」页旋转/翻转/黑白/反色/色道分离 + 画布尺寸；目标可为画布/底图/选中图层。\n"
    "6) 「标注」页改样式/叠放次序/填充、复制/删除选中、清除全部。\n"
    "7) 「图片」页添加/排序/复制/删除图片图层、锚点/枢轴落位。\n"
    "8) 「导出」页设输出方式/格式/烧录开关，工具条「导出」出图。\n"
    "9) 「更多」：加载/保存配置、外观/交互（含选区吸附与重排前确认）、重置全部、退出。\n\n"
    "手势：双指捏合缩放、双击适应窗口、长按＝右键（绘制时长按收笔、菜单可清除切割线）；\n"
    "「步进」盘替代方向键微调选区。";
inline constexpr char kAboutBody[] =
    "ImageDiscropper GUI %1(内核 %2)\n"
    "    -- 一款图像区域提取 / 反向剔除 / 网格分割工具。\n"
    "    项目地址：https://github.com/GoldenCreeperJ/ImageDiscropper";   // %1/%2 由 IDC_GUI_VERSION / IDC_CORE_VERSION 填入。
} // namespace msg
} // namespace idc::gui::texts
