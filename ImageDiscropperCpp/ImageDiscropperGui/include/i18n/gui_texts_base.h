// ============================================================================
// 文件：include/i18n/gui_texts_base.h（GUI 常量层 · 字符串 · 前半：ident / menu / label）
// 作用：原 gui_texts.h 拆出的两半之一——收录**不可译契约组** ident（QSettings 键 / MIME / objectName /
//       快捷键 / 模板 token 等跨版本契约串）与两个**可译组** menu（菜单 / 上下文菜单项）、
//       label（控件文字：标签 / 按钮 / 分组框 / 复选框 / 对话框标题与过滤器）。
//       tooltip / status / msg 见同目录 gui_texts_detail.h；gui_texts.h 已转为聚合转发头，
//       消费方 include 路径与常量引用形态（texts::label::kXxx 等）不变。
// 架构定位：GUI 顶层 i18n 基建（与 gui_i18n.h / retranslate.h / gui_nums.h 同族，统一住 include/i18n）。
// 命名 / 类型：k + 域 + 语义驼峰；inline constexpr char[]（UTF-8 无 BOM）；「&」助记符与「…」省略号逐字保留。
// 目录与对账：menu / label 的英文表住 src/i18n/lang_en.cpp（前半）；ident 组永不进表
//       （QSettings 键名等翻译即破坏兼容）。tests/test_i18n_catalog.cpp 逐文件对账本层全部可译组。
// ============================================================================
#pragma once

namespace idc::gui::texts {

// 不可译标识串：QSettings 键 / MIME / objectName / 快捷键 / 模板 token。
namespace ident {
// ——QSettings 存取契约（改动即丢用户已有偏好；org / app 两项还决定注册表·ini·plist 的落点）——
inline constexpr char kSettingsOrg[] = "ImageDiscropper";      // QApplication::setOrganizationName（app/main.cpp）。
inline constexpr char kSettingsApp[] = "ImageDiscropperGui";   // QApplication::setApplicationName（app/main.cpp）。
// 三组互不耦合（配色 / 画布交互 / 会话记忆）。组名与键名在「载入 / 落盘 / 恢复默认」三处成对出现，
// 历史上各处各写一份字面量 —— 任一处笔误即静默读不到偏好（不报错，只回退默认值），故收此单点。
inline constexpr char kGroupPalette[] = "palette";   // app/palette.cpp
inline constexpr char kGroupCanvas[]  = "canvas";    // app/canvas_prefs.cpp
inline constexpr char kGroupUi[]      = "ui";        // app/app_prefs.cpp
inline constexpr char kGroupShortcuts[] = "shortcuts"; // app/keybindings.cpp（自定义快捷键；键名取 KeyBindings::idKey）
// canvas 组·画布交互偏好（app/canvas_prefs.cpp；默认值与钳制区间见 nums::app）。
inline constexpr char kCanvasSnap[]            = "snap";            // 选区吸附开关。
inline constexpr char kCanvasHandlePx[]        = "handlePx";         // 手柄 / 命中屏幕基准（0＝跟随平台）。
inline constexpr char kCanvasScrollMargin[]    = "scrollMargin";     // 边缘滚动余量（视口各边百分比）。
inline constexpr char kCanvasZoomStep[]        = "zoomStep";         // 缩放步进（%）。
inline constexpr char kCanvasZoomMin[]         = "zoomMin";          // 缩放下限（%）。
inline constexpr char kCanvasZoomMax[]         = "zoomMax";          // 缩放上限（%）。
inline constexpr char kCanvasNewWidth[]        = "newWidth";         // 「新建画布」记住的上次宽。
inline constexpr char kCanvasNewHeight[]       = "newHeight";        // 「新建画布」记住的上次高。
// palette 组·画布配色（app/palette.cpp；六色 + 三 alpha，默认值在 palette.cpp 成表）。
inline constexpr char kPaletteKeep[]              = "keep";               // 保留遮罩色。
inline constexpr char kPaletteDelete[]            = "delete";             // 删除遮罩色。
inline constexpr char kPaletteSelection[]         = "selection";          // 选区 / 切割线色。
inline constexpr char kPaletteAnnotation[]        = "annotation";         // 标注高亮 / 手柄色。
inline constexpr char kPaletteGrid[]              = "grid";               // 网格线色（与 cli texts::cmd::kGrid 同字纯属巧合，不是同一个值）。
inline constexpr char kPaletteBackground[]        = "background";         // 画布背景色（与 core texts::layer::kBackground 同字纯属巧合，不是同一个值）。
inline constexpr char kPaletteKeepAlpha[]         = "keepAlpha";          // 保留遮罩 alpha。
inline constexpr char kPaletteDeleteAlpha[]       = "deleteAlpha";        // 删除遮罩 alpha。
inline constexpr char kPaletteSelectionFillAlpha[] = "selectionFillAlpha"; // 选区填充 alpha。
// ui 组·会话记忆（app/app_prefs.cpp）。
inline constexpr char kUiRecentFiles[]       = "recentFiles";       // 最近打开文件 MRU（QStringList）。
inline constexpr char kUiLastOpenDir[]       = "lastOpenDir";       // 打开对话框起始目录。
inline constexpr char kUiLastExportDir[]     = "lastExportDir";     // 导出对话框起始目录（上次实际使用，自动记录）。
inline constexpr char kUiDefaultExportDir[]  = "defaultExportDir";  // 默认输出文件夹（用户预设的导出起始目录，仅桌面；空＝不预设）。
inline constexpr char kUiConfirmRearrange[]  = "confirmRearrange";  // 重排前二次确认开关。
inline constexpr char kUiLanguage[]          = "language";           // 界面语言偏好（值见下方 kLangAuto 与 core 的两个语言码）。
inline constexpr char kUiThemeMode[]         = "themeMode";          // 深浅色主题偏好（值见 util/theme.h：system/light/dark）。
// 界面语言偏好的**取值**（数据而非文案：写进 QSettings，由 app/main.cpp 与外观对话框同源读写）。
// “跟随系统”是本层独有的哨兵（无对应语言码，由 i18n/gui_i18n.cpp 解释）；
// 其余两个取值不在本层立常量——直接取 core_i18n.h 的 kSourceLanguage / kLangCodeEn（真相源在上）。
inline constexpr char kLangAuto[] = "auto";
// 相册发布路径（Android MediaStore 相对目录，只用于**拼路径**；各界面文案里的「相册 Pictures/…」随整条模板逐字保留）。
inline constexpr char kGalleryRoot[] = "Pictures";        // 系统相册根目录名（mobile/android_export_publish.cpp 拼「/」）。
inline constexpr char kGalleryAppDir[] = "ImageDiscropper"; // 本应用子目录名（app/controllers/export_controller.cpp）。与 ident::kSettingsOrg 同字不同用途（组织名 / 相册目录名），故不合并。
// 本 GUI 自己的命令行位置参数（app/main.cpp：文件关联 / 拖放启动入口，与用户脚本同 CLI 一样逐字依赖）。
inline constexpr char kCliArgImageName[]   = "image";   // 位置参数名（--help 里显示）。
inline constexpr char kCliArgImageSyntax[] = "[image]";  // 位置参数写法（可选，故带方括号）。
// Android 相册 URI 前缀（历史手输路径的识别闸：当它出现在输出目录 / 文件框里即视同「留空走相册默认」）：
// 写在四处（app/app_prefs.cpp、app/controllers/export_controller.cpp、session_controller 等），同一契约单点定形。
inline constexpr char kContentUriScheme[] = "content://";
// 配置自存目录与命名（app/controllers/config_controller.cpp，仅 Q_OS_ANDROID：移动端免 SAF 框，直接存应用文档目录）。
inline constexpr char kAppDocsConfigSubDir[] = "ImageDiscropper/Config";   // 与 DocumentsLocation 拼「/」后的相对子目录（保存 / 列表两处）。
inline constexpr char kConfigFileNamePrefix[] = "config_";                 // 落盘名前缀（后接时间戳与 .json）。
inline constexpr char kTimestampFormat[]      = "yyyyMMdd-hhmmss";         // 时间戳格式（配置落盘名与相册临时子目录共用）。
inline constexpr char kJsonGlob[]             = "*.json";                  // 枚举已存配置的过滤式（QDir::entryList）。
// 配置的资源伴随目录后缀（<配置名>.assets/）：CLI 打包、GUI 载入，跨组件同一落盘契约，两处必须同字。
inline constexpr char kAssetsDirSuffix[]      = ".assets";   // GUI 两处落点：app/controllers/config_controller.cpp（图层循环与底图分支）。
                                                              // CLI 侧因宽字面量（std::filesystem::path(L".assets")）与 char[] 类型不符
                                                              // 而就地保留，见 ImageDiscropperCli/src/job.cpp 文件头「未提取项」注。
// 分离导出命名模板（**数据而非文案**：占位符 token 由 Core 解析，改动即改输出文件名形态）。
// 两处默认值必须同字（panels/export_panel.cpp 输入框初值、model/document.h 字段初值）。
// 与 core texts::defaults::kNamingTemplate 跨层同字（char[] 之间不能转引），改那边要跟着改这边。
inline constexpr char kNamingDefaultTemplate[] = "{name}_{index:03d}";
// 注：移动骨架的部件 objectName（mobileDrawer / mobileToolBar / mobileStrip）不迁本层——
//     它们与限定其样式的 `#mobileDrawer` 选择器同文件共驻（mobile/mobile_shell_ui.h），
//     拆开会隔断「常量 ↔ 选择器」的可见耦合，已在原处单点定义，无重复可消。
} // namespace ident

// 菜单栏、菜单项、上下文菜单条目（src/app/main_window_menus.cpp、src/mobile/mobile_shell_ui.cpp）。
// 命名：k + 域 + 语义（kMenuFileExport）；「&」助记符与「…」省略号逐字保留，改动即改界面呈现。
// 有意不提取：PNG / JPG / WebP / BMP 等格式名（任何语言下同一写法，且与扩展名契约同形）。
namespace menu {
// ——文件（main_window_menus.cpp::buildFileMenu）——
inline constexpr char kMenuFile[]          = "文件(&F)";          // 菜单栏标题。
inline constexpr char kFileNewCanvas[]     = "新建画布(&N)…";
inline constexpr char kFileOpenImage[]     = "打开图像(&O)";
inline constexpr char kFileRecent[]        = "最近打开";           // 子菜单入口（条目动态生成）。
inline constexpr char kFileRecentEmpty[]   = "（无最近记录）";      // 列表空时的置灰占位项。
inline constexpr char kFileRecentClear[]   = "清除最近记录";
inline constexpr char kFileExport[]        = "导出(&S)";
inline constexpr char kFileExportSettings[]= "导出设置";           // 与导出面板镜像的子菜单（输出方式 / 格式 / 烧录）。
inline constexpr char kEmitSeparate[]      = "分离";               // EmitMode 三项，互斥勾选。
inline constexpr char kEmitCollapse[]      = "坍缩";
inline constexpr char kEmitRearrange[]     = "重排";
inline constexpr char kBurnAnnotation[]    = "烧录标注";
inline constexpr char kBurnImageLayers[]   = "烧录图片图层";
inline constexpr char kConfirmRearrange[]  = "重排前二次确认";      // 开关态存 UiPrefs（ui/confirmRearrange）。
inline constexpr char kFileLoadConfig[]    = "加载配置(&L)…";
inline constexpr char kFileSaveConfig[]    = "保存配置(&C)…";
inline constexpr char kFileExit[]          = "退出(&X)";
// ——编辑（全局撤销 / 重做与选区清理）——
inline constexpr char kMenuEdit[]        = "编辑(&E)";
inline constexpr char kEditUndo[]        = "撤销(&U)";
inline constexpr char kEditRedo[]        = "重做(&R)";
inline constexpr char kEditClearSel[]    = "清除选区(&C)";
inline constexpr char kEditCellsAll[]    = "全选单元(&A)";   // L3 选择集三项，与参数面板按钮同一 Document 入口。
inline constexpr char kEditCellsInvert[] = "反选单元(&I)";
inline constexpr char kEditCellsClear[]  = "清空单元选择(&L)";
inline constexpr char kEditResetAll[]    = "重置全部(&Z)";
// ——模式（L1/L2/L3 + 极性 + 画布四态 + 切割参数）——
inline constexpr char kMenuMode[]           = "模式(&M)";
inline constexpr char kModeL1[]             = "标准提取";   // 三项互斥，即模式预设（Tier 代号 L1/L2/L3 属内部术语，不对用户展示）。
inline constexpr char kModeL2[]             = "反向剔除";
inline constexpr char kModeL3[]             = "网格分割";
inline constexpr char kPolarityKeep[]       = "保留框内(&K)";
inline constexpr char kPolarityRemove[]     = "删除框内(&R)";
inline constexpr char kCanvasMode[]         = "画布模式";         // 四态子菜单，与工具面板顶行按钮镜像。
inline constexpr char kCanvasMarquee[]      = "框选";
inline constexpr char kCanvasImagePick[]    = "图片拾取";
inline constexpr char kCanvasAnnoSelect[]   = "标注选取";
inline constexpr char kCanvasAnnoCreate[]   = "标注绘制";
inline constexpr char kL1ShapeSub[]         = "形状";          // 以下四个子菜单入口仅当前模式启用（syncPanels 置灰）。
inline constexpr char kL2MethodSub[]        = "方式";
inline constexpr char kL3SortSub[]          = "排序";
inline constexpr char kL3RemainderSub[]     = "余量";
// 下面四组（L1 形状 / L2 方式 / 排序 / 余量）与参数面板的三个下拉框逐字同形
//（src/panels/param_panel_pages.cpp 直接复用本组常量），故只此一份；菜单与面板任一侧改词都会同时改到另一侧。
inline constexpr char kL1Rect[]             = "矩形";             // L1 形状三项。工具面板形状项同字见 label::kToolRect（菜单项 / 工具项角色不同，各立一条）。
inline constexpr char kL1HBand[]            = "横带";
inline constexpr char kL1VBand[]            = "竖带";
inline constexpr char kL2Cross[]            = "十字切割";         // L2 方式四项。
inline constexpr char kL2HLine[]            = "横线切割";
inline constexpr char kL2VLine[]            = "竖线切割";
inline constexpr char kL2MultiRect[]        = "多矩形并集剔除";
inline constexpr char kSortRowMajor[]       = "横优先";           // L3 排序三项。
inline constexpr char kSortColMajor[]       = "竖优先";
inline constexpr char kSortCustom[]         = "自定义";
inline constexpr char kSortReverse[]        = "整体逆序";         // 菜单勾选项与 L3 页复选框同字，两处共用本条。
inline constexpr char kSortSnake[]          = "蛇形排序";         // 面板复选写作「蛇形（隔行/隔列反向）」，措辞有意不同，各自一条。
inline constexpr char kRemainderDiscard[]   = "丢弃残缺";         // 余量策略三项，按枚举序入表。
inline constexpr char kRemainderKeep[]      = "保留残缺";
inline constexpr char kRemainderPad[]       = "补白";
// ——预处理（main_window_menus.cpp::buildImageMenu，菜单名带「图像」助记符而类名为 preprocess）——
inline constexpr char kMenuPreprocess[]   = "预处理(&I)";
inline constexpr char kRotLeft90[]        = "左转 90°";
inline constexpr char kRotRight90[]       = "右转 90°";
inline constexpr char kRot180[]           = "旋转 180°";
inline constexpr char kFlipHorizontal[]   = "水平翻转";
inline constexpr char kFlipVertical[]     = "垂直翻转";
inline constexpr char kGray[]             = "黑白";
inline constexpr char kInvert[]           = "反色";               // 子菜单七项（任意组合去面板）。
inline constexpr char kInvertAll[]        = "全通道";
inline constexpr char kInvertRG[]         = "红+绿";
inline constexpr char kInvertRB[]         = "红+蓝";
inline constexpr char kInvertGB[]         = "绿+蓝";
inline constexpr char kInvertR[]          = "仅红";
inline constexpr char kInvertG[]          = "仅绿";
inline constexpr char kInvertB[]          = "仅蓝";
inline constexpr char kChannelSplit[]     = "色道分离";           // 子菜单六项。预处理面板下拉项同字见 label::kColorTypeSplit（菜单项 / 下拉项角色不同，各立一条）。
inline constexpr char kSplitKeepR[]       = "仅保留红";
inline constexpr char kSplitKeepG[]       = "仅保留绿";
inline constexpr char kSplitKeepB[]       = "仅保留蓝";
inline constexpr char kSplitKeepRG[]      = "保留红+绿";
inline constexpr char kSplitKeepRB[]      = "保留红+蓝";
inline constexpr char kSplitKeepGB[]      = "保留绿+蓝";
inline constexpr char kResetPreprocess[]  = "重置预处理";
// ——图片（图层栈 CRUD）——
inline constexpr char kMenuImage[]         = "图片(&L)";
inline constexpr char kAddImageLayer[]     = "添加图片图层(&A)…";
inline constexpr char kClearAllImageLayers[]= "清空全部图片图层(&C)";
inline constexpr char kLayerUp[]           = "上移一层(&U)";   // 图片菜单与标注「叠放次序」子菜单同一写法，共用本条。
inline constexpr char kLayerDown[]         = "下移一层(&D)";   // 同上共用。
inline constexpr char kLayerToTop[]        = "移至顶层(&T)";
inline constexpr char kLayerToBottom[]     = "移至底层(&B)";
inline constexpr char kLayerRemove[]       = "删除图层(&R)";
inline constexpr char kAnchor[]            = "锚点(&N)";       // 两子菜单九项共用 kDir* 表（见 label 组）。
inline constexpr char kPivot[]             = "枢轴(&P)";
// ——标注——
inline constexpr char kMenuAnnotation[]      = "标注(&A)";
inline constexpr char kAnnoUndo[]            = "撤销标注(&U)";   // 不绑快捷键（Ctrl+Z/Y 归编辑菜单，上下文路由）。
inline constexpr char kAnnoRedo[]            = "重做标注(&R)";
inline constexpr char kAnnoDeleteSelected[]  = "删除选中标注(&D)";
inline constexpr char kAnnoClearAll[]        = "清除全部标注(&C)";
inline constexpr char kAnnoStacking[]        = "叠放次序(&Z)";
inline constexpr char kAnnoToFront[]         = "置于顶层(&T)";
inline constexpr char kAnnoToBack[]          = "置于底层(&B)";
inline constexpr char kAnnoFill[]            = "填充(&F)";
inline constexpr char kAnnoTools[]           = "工具(&T)";       // 十三项形状表见 label::kTool*。
// ——视图（缩放 + 各层显隐开关 + 偏好）——
inline constexpr char kMenuView[]        = "视图(&V)";
inline constexpr char kZoomIn[]          = "放大";
inline constexpr char kZoomOut[]         = "缩小";
inline constexpr char kFitToWindow[]     = "适应窗口";
inline constexpr char kResetView[]       = "重置视图 (1:1)";
inline constexpr char kShowBase[]        = "底图";               // 以下八项为图层显隐开关（与左侧面板逐行双向镜像）。
inline constexpr char kShowImageLayer[]  = "图片图层";   // 面板分组框标题与预处理目标三元名同字见 label::kImageLayerName。
inline constexpr char kShowAnnotation[]  = "标注";       // 属性面板分区标题同字见 label::kSecAnnotation。
inline constexpr char kShowMask[]        = "遮罩（保留/删除预览）";
inline constexpr char kShowGrid[]        = "网格线";     // 取色行标签同字见 label::kSwatchGridLine。
inline constexpr char kShowCutLine[]     = "切割线";
inline constexpr char kShowSelection[]   = "选区边框";
inline constexpr char kShowCellNumber[]  = "单元编号";
inline constexpr char kSnapToggle[]      = "选区吸附";            // 运行时开关，状态存 canvas/snap。
inline constexpr char kAppearance[]      = "外观/交互…";
// ——画布右键上下文菜单（src/canvas/canvas_desktop_input_adapter.cpp::onContextMenu）——
// 本组与上方菜单项同为「入口不同、动作相同」的写法，但措辞不一（无 & 助记符、不带 (1:1) 后缀），逐字保留各侧原文。
inline constexpr char kCtxClearCutLine[] = "清除切割线";   // 动作与 menu::kEditClearSel / shortcut Esc 同一路（requestClearCut）。
inline constexpr char kCtxResetView[]    = "重置视图";     // 与 menu::kResetView（「重置视图 (1:1)」）同动作异词。
inline constexpr char kCtxHideMask[]     = "隐藏预览遮罩";  // 同一开关的两态文字，与 menu::kShowMask（图层开关）异词。
inline constexpr char kCtxShowMask[]     = "显示预览遮罩";
// ——帮助——
inline constexpr char kMenuHelp[]    = "帮助(&H)";
inline constexpr char kUsageGuide[]  = "使用说明";   // 菜单项与弹窗标题共用本条。
inline constexpr char kAbout[]       = "关于";       // 同上。
} // namespace menu

// 控件文字：标签、按钮、分组框、复选框、数值框前后缀，以及**文件对话框标题与过滤器**
//（src/panels/**、src/app/*_dialog.cpp、src/app/forms/**、src/app/controllers/**）。
namespace label {
// 主窗口标题（桌面 app/main_window.cpp 与移动 mobile/mobile_shell.cpp 两处同源）：
// 前半 "ImageDiscropper GUI" 是产品名 + 形态名（与 msg::kAboutBody 首行同字），%1 由 CMake 注入的 IDC_GUI_VERSION 填入。
inline constexpr char kWindowTitleFmt[]      = "ImageDiscropper GUI %1";
// 文件对话框标题（桌面分支；同一标题在控制器与面板两处各写一份的已归一）。
inline constexpr char kDlgTitleSaveConfig[]   = "保存配置";      // app/controllers/config_controller.cpp。与工具栏短名 label::kTbSaveConfig 同字不同角色（对话框标题 / 动作按钮），各立一条。
inline constexpr char kDlgTitleLoadConfig[]   = "加载配置";      // app/controllers/config_controller.cpp。同上（label::kTbLoadConfig / kTbLoadConfigEllipsis）。
inline constexpr char kDlgTitleOpenImage[]    = "打开图像";      // app/controllers/session_controller.cpp。
inline constexpr char kDlgTitleExportAs[]     = "导出为";        // app/controllers/export_controller.cpp 与 panels/export_panel.cpp（同一动作两处入口）。
inline constexpr char kDlgTitleChooseOutDir[] = "选择输出目录";   // 同上两处。
inline constexpr char kDlgTitlePickSourceImage[] = "选择源图";   // app/new_canvas_dialog.cpp（附加图片选源）。
inline constexpr char kDlgLabelChooseConfig[]  = "选择配置";     // app/controllers/config_controller.cpp——Android 加载列表的 QInputDialog 标签。
// 过滤器：前半段是人看的描述、后半段是扩展名契约（两边须同步改），故整串集中一份。
inline constexpr char kFilterExportImage[]  = "图像 (*.png *.jpg *.jpeg *.bmp *.webp)";                       // 导出另存为（不含 Qt 未支持的 gif/tga）。
inline constexpr char kFilterOpenImage[]    = "图像 (*.png *.jpg *.jpeg *.bmp *.webp *.gif *.tga);;所有文件 (*)";   // 打开图像。
inline constexpr char kFilterSourceImage[]  = "图像文件 (*.png *.jpg *.jpeg *.bmp *.webp *.gif *.tga);;所有文件 (*)"; // 新建画布的附加源图（措辞与上行有意不同，各自逐字保留）。
inline constexpr char kFilterConfigJson[]   = "ImageDiscropper 配置 (*.json);;所有文件 (*)";  // 保存 / 加载配置同一过滤器（两处入口）。
// 绘制工具形状名（菜单「标注→工具」十三项与工具面板形状行共用；面板为真相源）。
inline constexpr char kToolRect[]          = "矩形";    // 与模式菜单 menu::kL1Rect 同字（选取形状与绘制工具语义不同，各立一条）。
inline constexpr char kToolSquare[]        = "正方形";
inline constexpr char kToolRhombus[]       = "菱形";
inline constexpr char kToolCircle[]        = "圆形";
inline constexpr char kToolEllipse[]       = "椭圆";
inline constexpr char kToolIsoTriangle[]   = "等腰三角形";
inline constexpr char kToolEquTriangle[]   = "等边三角形";
inline constexpr char kToolRightTriangle[] = "直角三角形";
inline constexpr char kToolIsoRightTri[]   = "等腰直角三角形";
inline constexpr char kToolLine[]          = "直线";
inline constexpr char kToolPolyline[]      = "多线段";
inline constexpr char kToolText[]          = "文字";    // 工具面板形状项；属性面板的输入行标签同名见 label::kTextFieldLabel（角色不同，各立一条）。
inline constexpr char kToolBrush[]         = "画笔";
// 九向名称（锚点 / 枢轴两个子菜单共用一张表；图层编辑面板两组下拉同字）。
inline constexpr char kDirTopLeft[]     = "左上";
inline constexpr char kDirTopCenter[]   = "上中";
inline constexpr char kDirTopRight[]    = "右上";
inline constexpr char kDirMiddleLeft[]  = "左中";
inline constexpr char kDirCenter[]      = "居中";
inline constexpr char kDirMiddleRight[] = "右中";
inline constexpr char kDirBottomLeft[]  = "左下";
inline constexpr char kDirBottomCenter[]= "下中";
inline constexpr char kDirBottomRight[] = "右下";
// 导出面板（src/panels/export_panel.cpp）：行标签 / 下拉项 / 按钮。
// 与文件菜单「导出设置」子菜单同义但措辞不同（如「合并坍缩」 vs 菜单项「坍缩」），**不合并**，各自逐字保留。
inline constexpr char kExportBoxTitle[]    = "导出";        // 四处共用：导出面板分组框标题、面板导出按钮、
                                                           // 桌面工具栏与移动条带的导出动作、折叠栏/抽屉「导出」页签。
inline constexpr char kOutModeLabel[]      = "输出模式";
inline constexpr char kEmitSeparateItem[]  = "分离导出";     // 下拉三项（与 menu::kEmit* 同义异词）。
inline constexpr char kEmitCollapseItem[]  = "合并坍缩";
inline constexpr char kEmitRearrangeItem[] = "合并重排";
inline constexpr char kBrowseButton[]      = "浏览…";       // 目录行与文件行两处共用。
inline constexpr char kOutDirLabel[]       = "目录";
inline constexpr char kNamingLabel[]       = "命名";
inline constexpr char kOutFileLabel[]      = "文件";
inline constexpr char kAutoSpecialValue[]  = "自动";         // 数值框最小值 0 的显示文本（列/行/单元宽高四项与外观对话框的手柄尺寸框共用）。
inline constexpr char kMergeColsLabel[]    = "列数";
inline constexpr char kMergeRowsLabel[]    = "行数";
inline constexpr char kAutoGridButton[]    = "按格数自动";
inline constexpr char kMergeCellWLabel[]   = "单元宽";
inline constexpr char kMergeCellHLabel[]   = "单元高";
inline constexpr char kMergeRowMajor[]     = "行优先";       // 重排填充顺序下拉（与菜单「横优先/竖优先」异词）。
inline constexpr char kMergeColMajor[]     = "列优先";
inline constexpr char kMergeSnakeLabel[]   = "蛇形";
inline constexpr char kMergeReverseLabel[] = "倒序";
inline constexpr char kMergeOrderLabel[]   = "填充顺序";
inline constexpr char kPadColorLabel[]     = "填充色";
inline constexpr char kFormatLabel[]       = "格式";
inline constexpr char kQualityLabel[]      = "质量";
inline constexpr char kOutputPreviewLabel[] = "输出预览";
inline constexpr char kNoPreviewThumb[]    = "（无输出预览）";   // 缩略图位空时的占位文字。
inline constexpr char kNoPreviewInfo[]     = "暂无输出预览。";   // 预览信息行初始文案（与上行有意不同）。
inline constexpr char kBurnInCheck[]       = "导出时烧录标注";
inline constexpr char kLayersBurnInCheck[] = "导出时烧录图片";
// 输入框占位符（提示「怎么填」，与标签不同层）。
inline constexpr char kPhOutDir[]  = "手动输入或点「浏览…」选择输出目录";
inline constexpr char kPhOutFile[] = "手动输入或点「浏览…」选择输出文件";
inline constexpr char kPhOutFileAndroid[] = "输入文件名（留空自动命名），导出到相册 Pictures/ImageDiscropper";  // 仅 Q_OS_ANDROID。
// 保存对话框过滤器（按当前格式逐条一份；「所有文件」兼作无文档时的兜底，两处共用）。
inline constexpr char kFilterAll[]      = "所有文件 (*)";
inline constexpr char kFilterSavePng[]  = "PNG 图像 (*.png)";
inline constexpr char kFilterSaveJpeg[] = "JPEG 图像 (*.jpg *.jpeg)";
inline constexpr char kFilterSaveWebp[] = "WebP 图像 (*.webp)";
inline constexpr char kFilterSaveBmp[]  = "BMP 图像 (*.bmp)";
// 右侧参数面板（src/panels/param_panel_pages.cpp 的 L1/L2/L3 三页 + param_panel.cpp 的回显联动）。
// 分组框标题用模式中文名（Tier 代号 L1/L2/L3 属引擎内部术语，不对用户展示）。
inline constexpr char kParamL1Box[]      = "标准提取";
inline constexpr char kParamL2Box[]      = "反向剔除";
inline constexpr char kParamL3Box[]      = "网格定义";
inline constexpr char kMultiRectBox[]    = "多矩形列表";
inline constexpr char kSelectSetBox[]    = "选择集";
inline constexpr char kSortSetBox[]      = "排序";
// 行标签（QFormLayout 的字段名）。
inline constexpr char kShapeFieldLabel[]     = "形状";
inline constexpr char kSubFeatureFieldLabel[]= "子功能";
inline constexpr char kGridOriginXLabel[]    = "基准点 x0";
inline constexpr char kGridOriginYLabel[]    = "基准点 y0";
inline constexpr char kCellWidthLabel[]      = "单元宽 cw";
inline constexpr char kCellHeightLabel[]     = "单元高 ch";
inline constexpr char kRemainderFieldLabel[] = "余量策略";
inline constexpr char kDerivedScaleLabel[]   = "派生规模";
inline constexpr char kStrategyFieldLabel[]  = "策略";
// 注：x1 / y1 / x2 / y2 四个坐标行标签**不上提**——纯技术记号，任何语言同一写法，且在 L1/L2 两页各出现一次属「表单字段名」而非文案。
// 按钮与复选框。
inline constexpr char kSelectAllButton[]    = "全选";
inline constexpr char kInvertButton[]       = "反选";
inline constexpr char kClearButton[]        = "清空";   // 多矩形列表与 L3 选择集两处共用。
inline constexpr char kDeleteSelectedButton[] = "删除选中";
inline constexpr char kToGridButton[]       = "转为网格模式编辑…";
inline constexpr char kSnakeCheck[]         = "蛇形";
// 页面说明正文（QLabel 静态提示，wordWrap）。
inline constexpr char kL1PageNote[]      = "保留选框内区域；切割线贯穿全图，保留/删除可在左侧切换。";
inline constexpr char kMultiRectNote[]   = "空白处拖拽追加矩形，可拖动/缩放；删除区为各矩形十字带并集。";
inline constexpr char kL2OutModeNote[]   = "输出模式见右侧「导出」页。";
inline constexpr char kCellSelectNote[]  = "画布上：单击单元切换选中、拖拽框选批量选中。";
// 图层编辑面板（src/panels/layer_edit_panel.cpp）：分组框标题 / 行标签 / 按钮。
inline constexpr char kImageLayerName[]    = "图片图层";   // 两处落点：图层编辑面板分组框标题（layer_edit_panel.cpp）、预处理目标三元名（image_panel.cpp）；
                                                           // 与视图菜单开关 menu::kShowImageLayer 同字（显隐开关与面板条目语义不同，各立一条）。
inline constexpr char kLayerPlaceBox[]     = "图层落位";
inline constexpr char kCanvasSizeBox[]     = "画布尺寸";   // 两处落点：图层编辑面板的画布尺寸区与预处理面板的 resize 区（同一物理量，两处共用）。
inline constexpr char kAddImageButton[]    = "添加图片…";
inline constexpr char kMoveUpButton[]      = "上移";       // 面板按钮短词（图层编辑与预处理队列两处共用）；菜单项带助记符与「一层」后缀，见 menu::kLayerUp。
inline constexpr char kMoveDownButton[]    = "下移";
inline constexpr char kMoveTopButton[]     = "移至顶层";   // 菜单侧同字但带助记符 menu::kLayerToTop（“移至顶层(&T)”），两形各一条。
inline constexpr char kMoveBottomButton[]  = "移至底层";   // 同上见 menu::kLayerToBottom（“移至底层(&B)”）。
inline constexpr char kRemoveButton[]      = "删除";
inline constexpr char kClearLayersButton[] = "清空图层";
inline constexpr char kAnchorFieldLabel[]     = "画布锚点";
inline constexpr char kPivotFieldLabel[]      = "图片枢轴";
inline constexpr char kOffsetFieldLabel[]     = "偏移 X / Y";
inline constexpr char kTargetSizeFieldLabel[] = "目标宽 / 高";
inline constexpr char kOpacityFieldLabel[]    = "不透明度";
inline constexpr char kApplyPlaceButton[]     = "应用落位";
inline constexpr char kApplyButton[]          = "应用";
inline constexpr char kNativeSpecialValue[]   = "原生";     // 目标宽/高最小值 0 的显示文本（两项共用；与导出段的 label::kAutoSpecialValue 同类用法）。
                                                             // 摘要行短名同字见 label::kFitNameNative（specialValueText 与摘要名角色不同，各立一条）。
// 注：数值框后缀 " px" 与 " ×" **不上提**（属单字符排版碎片，与千位分隔符同级，提取反而隔开「_SUFFIX ↔ 单位」的可见耦合）。
// 预处理面板（src/panels/image_panel.cpp 与 image_panel_sections.cpp）：分区标题 / 行标签 / 下拉项 / 按钮。
// resize 区标题复用上方图层编辑面板的 kCanvasSizeBox（同字同义，不立第二条）。
inline constexpr char kGeoSectionBox[]     = "几何操作";
inline constexpr char kColorSectionBox[]   = "色彩操作";
inline constexpr char kKeepRatioCheck[]    = "保持宽高比";
inline constexpr char kWidthHeightLabel[]  = "宽 / 高";
inline constexpr char kScaleFieldLabel[]   = "比例";
inline constexpr char kApplySizeButton[]   = "应用尺寸";
inline constexpr char kScaleSizeButton[]   = "按比例缩放";
inline constexpr char kClearSizeButton[]   = "清除尺寸";
inline constexpr char kResetPreprocessButton[] = "重置预处理";
inline constexpr char kOpTypeFieldLabel[]  = "操作类型";   // 几何区与色彩区两处共用。
inline constexpr char kGeoTypeRotate[]     = "旋转";       // 几何操作类型下拉两项。标注属性面板的变换行标签同字见 label::kRotateFieldLabel（下拉项 / 行标签角色不同，各立一条）。
inline constexpr char kGeoTypeFlip[]       = "翻转";
inline constexpr char kAngleFieldLabel[]   = "角度";
inline constexpr char kDirectionFieldLabel[] = "方向";
inline constexpr char kFlipHorizontalPair[] = "水平（左右）";   // 下拉项带括号释文；列表文本用 menu::kFlipHorizontal（「水平翻转」）。
inline constexpr char kFlipVerticalPair[]   = "垂直（上下）";
inline constexpr char kGeoAddButton[]      = "添加几何步骤";
inline constexpr char kColorAddButton[]    = "添加色彩步骤";
inline constexpr char kUpdateSelectedButton[] = "更新所选";   // 几何区与色彩区两处共用。
inline constexpr char kChannelFieldLabel[] = "作用通道";
inline constexpr char kColorTypeGray[]     = "黑白";   // 色彩操作类型下拉三项；预处理队列的黑白行文本直接复用本条（同字同义，status 组未立第二条，见其 kStepRotateDesc 下方的注）。
inline constexpr char kColorTypeInvert[]   = "色道反色";
inline constexpr char kColorTypeSplit[]    = "色道分离";   // 菜单同名项见 menu::kChannelSplit（下拉项与菜单项角色不同，各立一条）。
// 预处理目标三元名（当前作用对象：图层 / 底图 / 画布）。
inline constexpr char kTargetCanvasName[]  = "画布";   // 底图一名复用 menu::kShowBase，图片图层一名用上方 kImageLayerName。
// 注：R / G / B 三个通道勾选框不上提（单字母技术记号，与色彩遮码的 "1"/"0" 位位同源）。
// 标注属性页（src/panels/annotation_prop_panel.cpp）：行标签 / 按钮 / 分组框。
// 与标注菜单同义但措辞不同（菜单项带 & 助记符），**不合并**：本组是面板无助记符版本。
inline constexpr char kPickColorButton[]     = "选择颜色…";   // 边框色与填充色两个色块按钮的初始文本（取色后换为 rgba 回显）。
inline constexpr char kStrokeColorFieldLabel[]= "边框颜色";
inline constexpr char kStrokeWidthFieldLabel[]= "粗细";
inline constexpr char kFillCheckLabel[]      = "填充内部";
inline constexpr char kFillFieldLabel[]      = "填充";
inline constexpr char kFillColorFieldLabel[] = "填充颜色";
inline constexpr char kTextFieldLabel[]      = "文字";   // 标注属性页的输入行标签；工具面板形状项同字见 label::kToolText（行标签 / 工具项角色不同，各立一条）。
inline constexpr char kFontSizeFieldLabel[]  = "字号";
inline constexpr char kCornerRadiusFieldLabel[] = "圆角";
inline constexpr char kTransformGroupBox[]   = "变换（选中标注）";
inline constexpr char kStackOrderGroupBox[]  = "叠放次序（选中标注）";
inline constexpr char kScaleXFieldLabel[]    = "水平缩放";
inline constexpr char kScaleYFieldLabel[]    = "垂直缩放";
inline constexpr char kRotateFieldLabel[]    = "旋转";   // 标注属性页变换区行标签；预处理几何操作下拉项同字见 label::kGeoTypeRotate（行标签 / 下拉项角色不同，各立一条）。
inline constexpr char kRaiseOneButton[]      = "上移一层";   // 菜单同名项带助记符，见 menu::kLayerUp。
inline constexpr char kLowerOneButton[]      = "下移一层";
inline constexpr char kToFrontButton[]       = "置于顶层";   // 菜单同名项见 menu::kAnnoToFront。
inline constexpr char kToBackButton[]        = "置于底层";   // 菜单同名项带助记符见 menu::kAnnoToBack。
inline constexpr char kDeleteSelectedAnnoButton[] = "删除选中标注";   // 菜单同名项带助记符见 menu::kAnnoDeleteSelected。
inline constexpr char kClearAllAnnoButton[]  = "清除全部标注";   // 确认框标题同字见 msg::kClearAnnosTitle（控件文字 / 对话框标题角色不同，各立一条）。
// 标注默认文字初值（与 model/annotation_editor 的 currentText 初值同字：新文本标注起笔时的预置内容）。
inline constexpr char kDefaultAnnotationText[] = "A";   // Core 读不到 text 时的占位值同字另见 core texts::defaults::kAnnotationText（跨层 char[] 不能转引，两处必须同字）。
// 取色器窗口标题。
inline constexpr char kDlgTitlePickStrokeColor[] = "选择边框颜色";
inline constexpr char kDlgTitlePickFillColor[]   = "选择填充颜色";
// 忙碌对话框（QProgressDialog）窗口标题（app/controllers/preprocess_controller_commit.cpp::runInBusyDialog）。
inline constexpr char kDlgTitleBusy[]          = "正在处理图像";
// 文字标注录入框（QInputDialog::getText，app/controllers/annotation_coordinator.cpp::onAnnoDragStart）。
inline constexpr char kDlgTitleAnnoText[]      = "文字标注";
inline constexpr char kDlgLabelAnnoText[]      = "请输入标注文字：";
// 导出面板的填充色取色框标题（panels/export_panel_merge.cpp）：与标注属性页的「选择填充颜色」（kDlgTitlePickFillColor）措辞有意不同。
inline constexpr char kDlgTitlePickPadColor[]  = "选择填充色";
inline constexpr char kPadColorTransparent[]   = "透明";   // 色块按钮在 alpha==0 时的替代表述（非 rgba 回显）。
// 工具面板（src/panels/tool_panel.cpp）：分组框标题。四态模式名与形状名不本组重复定义：
// 模式名复用 menu::kCanvas*（同字），形状名复用 label::kTool*（同字）。
inline constexpr char kInteractionModeBox[]  = "交互模式";   // 与菜单入口 menu::kCanvasMode（「画布模式」）措辞不同，各自逐字保留。
// 图层面板（src/panels/layer_panel.cpp）：分组框与八行显示开关（名称与「显示」中间为两个空格，原文如此）。
inline constexpr char kLayerPanelBox[]       = "图层";
inline constexpr char kShowBaseCheck[]       = "底图  显示";
inline constexpr char kShowImageCheck[]      = "图片  显示";
inline constexpr char kShowAnnoCheck[]       = "标注  显示";
inline constexpr char kShowMaskCheck[]       = "遮罩  显示";
inline constexpr char kShowGridCheck[]       = "网格线  显示";
inline constexpr char kShowCutLineCheck[]    = "切割线  显示";
inline constexpr char kShowSelectionCheck[]  = "选取边框  显示";
inline constexpr char kShowNumberCheck[]     = "单元编号  显示";
// 左侧模式面板分组框与移动抽屉页签的同名区（src/panels/left_panel.cpp、src/mobile/mobile_shell_ui.cpp）。
inline constexpr char kModeSection[]         = "模式";     // 左栏分组框标题与移动抽屉「模式」页签同字，两处共用。
inline constexpr char kSecImage[]            = "图片";     // 以下五节名：桌面折叠栏（app/main_window_ui.cpp）与移动抽屉页签（mobile/mobile_shell_ui.cpp）两处同源；
inline constexpr char kSecPreprocess[]       = "预处理";   // 「导出」一节复用上方 kExportBoxTitle。
inline constexpr char kSecAnnotation[]       = "标注";   // 菜单显隐项同字见 menu::kShowAnnotation（分组标题与开关项角色不同，各立一条）。
inline constexpr char kSecParam[]            = "参数";
// 工具栏 / 条带动作短名（桌面 app/main_window_ui.cpp 与移动 mobile/mobile_shell_ui.cpp 同源）。
// 同字的菜单项带 & 助记符与「…」后缀，不改写措辞，本组只收无助记符的短形；
// 「放大」/「缩小」直接复用 menu::kZoomIn / kZoomOut（逐字同形）。
inline constexpr char kMainToolBarTitle[]    = "主工具栏";   // QToolBar 窗口标题（可浮动时显示）。
inline constexpr char kMobileToolBarTitle[]  = "触控工具条"; // 同上，移动侧。
inline constexpr char kTbNew[]               = "新建";
inline constexpr char kTbOpen[]              = "打开";
inline constexpr char kTbFit[]               = "适应";     // 与 menu::kFitToWindow（「适应窗口」）异词，各自逐字保留。
inline constexpr char kTbUndo[]              = "撤销";     // 移动条带常驻项（无键盘时唯一的撤销入口）。
inline constexpr char kTbRedo[]              = "重做";
inline constexpr char kTbLoadConfig[]        = "加载配置";   // 桌面工具栏无省略号。对话框标题同字见 label::kDlgTitleLoadConfig（动作按钮 / 窗口标题角色不同，各立一条）。
inline constexpr char kTbSaveConfig[]        = "保存配置";   // 同上见 label::kDlgTitleSaveConfig。
inline constexpr char kTbLoadConfigEllipsis[] = "加载配置…";   // 移动「更多」菜单带省略号（弹窗入口）。
inline constexpr char kTbSaveConfigEllipsis[] = "保存配置…";
inline constexpr char kTbResetAll[]          = "重置全部";   // 与 msg::kResetAllTitle（确认框标题）同字但角色不同，各自一条。
inline constexpr char kTbStepPad[]           = "步进";     // 仅移动：8 向步进盘弹出按钮。
inline constexpr char kTbMore[]              = "更多";     // 仅移动：低频项集合菜单。
inline constexpr char kTbExit[]              = "退出";     // 仅移动「更多」菜单（桌面见 menu::kFileExit，带助记符）。
inline constexpr char kPanelToggle[]         = "面板";     // 仅移动：抽屉窗口标题与开合按钮两处共用（标题因空标题栏隐藏而不可见，仍需一个名字）。
// 落位行（src/app/forms/placement_row.cpp，新建对话框与图层落位面板共用组件）的行标签（带全角冒号）。
inline constexpr char kAnchorColonLabel[]    = "锚点：";
inline constexpr char kPivotColonLabel[]     = "枢轴：";
inline constexpr char kPositionColonLabel[]  = "位置：";
// 左侧面板的极性开关（src/panels/left_panel.cpp）：菜单同名项带助记符（menu::kPolarity*），本组为按钮无助记符版本。
inline constexpr char kPolarityGroupBox[]     = "保留 / 删除";
inline constexpr char kPolarityKeepButton[]   = "保留框内";
inline constexpr char kPolarityRemoveButton[] = "删除框内";   // 与按钮同组的模式一项复用 menu::kModeL1 / kModeL2 / kModeL3（逐字同形）。
// 外观 / 交互对话框（src/app/appearance_dialog.cpp）：窗口标题 / 分区 / 行标签 / 底部提示 / 按钮。
// 窗口标题与菜单入口 menu::kAppearance（「外观/交互…」）字形不同（本条两侧带空格），各立一条。
inline constexpr char kDlgTitleAppearance[]   = "外观 / 交互";
inline constexpr char kPaletteBoxTitle[]      = "画布配色";
inline constexpr char kInteractionBoxTitle[]  = "画布交互";
// 上区六项语义色的行标签（按钮背色即直观示色）。
inline constexpr char kSwatchKeepMask[]      = "保留块遮罩";
inline constexpr char kSwatchDeleteMask[]    = "删除块遮罩";
inline constexpr char kSwatchSelectionCut[]  = "选区 / 切割线";
inline constexpr char kSwatchAnnoSelected[]  = "标注选中";
inline constexpr char kSwatchGridLine[]      = "网格线";   // 与显隐开关 menu::kShowGrid 同字不同角色（颜色行标签 / 开关项），各立一条。
inline constexpr char kSwatchCanvasBg[]      = "画布背景";
// 三项填充 alpha（0–255）的行标签。
inline constexpr char kAlphaKeepSpin[]       = "保留遮罩浓度";
inline constexpr char kAlphaDeleteSpin[]     = "删除遮罩浓度";
inline constexpr char kAlphaSelectionSpin[]  = "选区填充浓度";
// 下区交互偏好的行标签（数值框后缀 " px" / " %" 不上提，与全仓口径一致）。
inline constexpr char kHandleSizeField[]     = "手柄 / 命中尺寸";
inline constexpr char kScrollMarginField[]   = "边缘滚动余量";
inline constexpr char kZoomStepField[]       = "缩放步进";
inline constexpr char kZoomMinField[]        = "缩放下限";
inline constexpr char kZoomMaxField[]        = "缩放上限";
inline constexpr char kNewWidthField[]       = "新建默认宽度";
inline constexpr char kNewHeightField[]      = "新建默认高度";
inline constexpr char kNoMarginSpecialValue[] = "不留";   // 边缘滚动余量框 0 的显示文本（手柄框的「自动」复用上方 kAutoSpecialValue）。
inline constexpr char kAppearanceHint[]      = "改动即时生效并跨会话记住。";
inline constexpr char kRestoreDefaultsButton[] = "恢复默认";
// 第三区「应用语言」（同一对话框）：分区标题 / 行标签 / 三个下拉项 / 即时生效提示。
// 热切换已实现：下拉选中即落盘并由 app/main.cpp 于模态关闭后重建窗口（会话无损），故行尾说明即时生效。
inline constexpr char kLangBoxTitle[]         = "应用语言";
inline constexpr char kLangField[]            = "界面语言";
inline constexpr char kLangFollowSystem[]     = "跟随系统";   // 下拉首项，项数据存 ident::kLangAuto。
inline constexpr char kLangZhCn[]             = "简体中文";   // 项数据存 core::kSourceLanguage。
inline constexpr char kLangEn[]               = "English";    // 项数据存 core::kLangCodeEn。
                                                            // 两条语言名**永不翻译**：以其自身书写展示是通行惯例
                                                            // （把 “English” 译成 “英语” 会让不识该语言的人认不出它）。
inline constexpr char kLangApplyHint[]        = "选定后立即生效，不影响正在编辑的内容。";
// 第五区「外观主题」（同一对话框，写 UiPrefs，**深浅三态**）：分区标题 / 行标签 / 三个下拉项 / 即时生效提示。
// 与画布配色（Palette 六色）正交：本区只切应用外壳控件的深/浅，画布内容色不受影响。
inline constexpr char kThemeBoxTitle[]        = "外观主题";
inline constexpr char kThemeField[]           = "深浅色";
inline constexpr char kThemeSystem[]          = "跟随系统";   // 下拉首项，项数据存 "system"（theme::Mode::System）。
inline constexpr char kThemeLight[]           = "浅色";       // 项数据存 "light"。
inline constexpr char kThemeDark[]            = "深色";       // 项数据存 "dark"。
inline constexpr char kThemeApplyHint[]       = "选定后立即切换深浅，不影响画布配色与导出。";
// 第四区「默认输出文件夹」（同一对话框，写 UiPrefs，**仅桌面**）：分区标题 / 行标签 / 空值占位 / 行尾说明。
// 只译措辞：目录路径本身是用户手输数据，不进表也不受语言影响。
inline constexpr char kExportDirBoxTitle[]    = "输出";
inline constexpr char kExportDirField[]       = "默认输出文件夹";
inline constexpr char kExportDirPlaceholder[] = "留空＝每次弹框选择";
inline constexpr char kExportDirHint[]        = "导出起始目录：本次手输 > 上次 > 此默认（仅桌面）。";
// 「外观/交互…」对话框改多 Tab（app/appearance_dialog.cpp）：三页签标题 + 快捷键编辑区（列头 / 行内恢复 / 全量恢复 / 说明 / 未绑定占位）。
// 快捷键页仅在桌面构建出现（无键盘的移动形态不建本页）。
inline constexpr char kTabColors[]           = "配色";
inline constexpr char kTabOther[]            = "其他";
inline constexpr char kTabShortcuts[]        = "快捷键";
inline constexpr char kShortcutColCommand[]  = "命令";
inline constexpr char kShortcutColKey[]      = "快捷键";
inline constexpr char kShortcutResetRowTip[] = "恢复此项默认";
inline constexpr char kShortcutResetAllButton[] = "全部恢复默认";
inline constexpr char kShortcutDisabled[]    = "（未绑定）";   // 某命令已解绑（空序列）时单元格回显。
inline constexpr char kShortcutHint[]        = "点击快捷键框后按新组合改绑；退格＝解绑；冲突时询问是否抢占。";
// 六色按钮共用的取色对话框标题：与按钮初值 label::kPickColorButton（「选择颜色…」带省略号）字形异。
inline constexpr char kDlgTitlePickColor[]   = "选择颜色";
// 图案参数表单（src/app/forms/pattern_form.cpp，新建画布对话框与图层「添加图片」复用同一组件）：行标签 / 下拉项 / 取色标题。
// 行标签统一带全角冒号（本表单是 QGridLayout 手工排布，不经 QFormLayout 自动补冒号）。
inline constexpr char kPatternFieldLabel[]     = "图案：";
inline constexpr char kOrientationColonLabel[] = "方向：";   // 预处理翻转区的「方向」无冒号（label::kDirectionFieldLabel），各立一条。
inline constexpr char kBgColorFieldLabel[]     = "底色：";
inline constexpr char kFgColorFieldLabel[]     = "前景色：";
inline constexpr char kPeriodFieldLabel[]      = "周期/格径：";   // 建表单时的初值，运行时按图案类型改写（见下方五条 kPeriodTag*）。
inline constexpr char kLineWidthColonLabel[]   = "线宽：";
inline constexpr char kDotRadiusColonLabel[]   = "点半径：";
inline constexpr char kSeedFieldLabel[]        = "种子：";
inline constexpr char kNoiseModeFieldLabel[]   = "噪声模式：";
// 周期标签随图案类型微调的五种措辞（updateAvailability 逐类型改写同一 QLabel 的文本）。
inline constexpr char kPeriodTagCell[]         = "格子边长：";   // 棋盘
inline constexpr char kPeriodTagRing[]         = "环间距：";     // 同心
inline constexpr char kPeriodTagDensity[]      = "疏密：";       // 放射线
inline constexpr char kPeriodTagHue[]          = "色相波长：";   // 色相渐变
inline constexpr char kPeriodTagPlain[]        = "周期：";       // 其余（条纹 / 网格线 / 点阵）
// 噪声配色模式下拉三项。
inline constexpr char kNoiseFullColor[]        = "全彩";
inline constexpr char kNoiseGrayscale[]        = "灰度";
inline constexpr char kNoiseTwoColor[]         = "双色";
// 方向下拉条目（rebuildOrientationCombo 随图案类型重建；条纹 / 同心 / 渐变三组措辞互异，逐项一条）。
inline constexpr char kDirStripeH[]            = "水平（竖条）";
inline constexpr char kDirStripeV[]            = "垂直（横条）";
inline constexpr char kDirDiagDown[]           = "斜 ↘";
inline constexpr char kDirDiagUp[]             = "斜 ↗";
inline constexpr char kDirRingItem[]           = "圆环";
inline constexpr char kDirSquareItem[]         = "方环";
inline constexpr char kDirHorizontal[]         = "水平";   // 线性渐变与色相渐变的简式两项（与 label::kFlipHorizontalPair 的括号释文异词）。
inline constexpr char kDirVertical[]           = "垂直";
// 底色块与前景块的取色对话框标题：与 kDlgTitlePickColor「选择颜色」有意不同，本组点明取哪一路颜色。
inline constexpr char kDlgTitlePickBgColor[]   = "选择底色";
inline constexpr char kDlgTitlePickFgColor[]   = "选择前景色";
// 新建画布 / 添加图片图层对话框（src/app/new_canvas_dialog.cpp）：窗口标题 / 行标签 / 按钮 / 预览占位 / 摘要模板。
// 窗口标题「新建画布」与菜单项 menu::kFileNewCanvas（「新建画布(&N)…」）字形不同，各立一条。
inline constexpr char kDlgTitleNewCanvas[]     = "新建画布";
inline constexpr char kDlgTitleAddLayer[]      = "添加图片图层";   // 同一对话框切到「添加图层」语境后改写标题（setAddLayerMode）。
inline constexpr char kWidthColonLabel[]       = "宽度：";
inline constexpr char kHeightColonLabel[]      = "高度：";   // 与外观对话框的「新建默认宽度 / 新建默认高度」异词：本组是对话框内实际输入行。
// 两级落位组的标题：「图层落位」复用上方 kLayerPlaceBox（同字同义，不立第二条），本组只缺另一级。
inline constexpr char kImagePlaceBox[]         = "图片落位";
inline constexpr char kAttachImageCheck[]      = "附加图片";
inline constexpr char kPickSourceImageButton[] = "选择源图…";   // 与取色对话框标题 kDlgTitlePickSourceImage（「选择源图」无省略号）字形异。
inline constexpr char kNoSourceImageChosen[]   = "未选择";   // 源图路径行的初值。
inline constexpr char kImageFitFieldLabel[]    = "适应方式：";
// 适应方式下拉三项：只留短名（释文不再占用行面）；与下方摘要短名 kFitName* 同字但角色不同（下拉项 / 摘要），各立一条。
inline constexpr char kFitCropItem[]           = "裁剪";
inline constexpr char kFitStretchItem[]        = "拉伸";
inline constexpr char kFitNativeItem[]         = "原生";
inline constexpr char kFitNameCrop[]           = "裁剪";     // 摘要行短名三项（与上方三条 kFit*Item 现同字，但角色不同：摘要短名 / 下拉项，各立一条）。
inline constexpr char kFitNameStretch[]        = "拉伸";
inline constexpr char kFitNameNative[]         = "原生";   // 落位面板 specialValueText 同字见 label::kNativeSpecialValue（摘要短名 / 数值框回显角色不同，各立一条）。
inline constexpr char kPreviewPlaceholder[]    = "预览";     // 预览 QLabel 的初值（被位图替换前）。
inline constexpr char kDialogOkButton[]        = "确定";     // QDialogButtonBox 两按钮的改写文本。
inline constexpr char kDialogCancelButton[]    = "取消";
// 预览标签的内联回显（校验未过时占住预览位，不进状态栏）。
inline constexpr char kNeedSourceImage[]       = "请先选择源图";   // 两处落点：refreshPreview 与 accept 的同句早退校验。
inline constexpr char kBadCanvasSize[]         = "尺寸非法";   // 两处落点：合成回空图时的预览回显；状态栏全句见 status::kSessionNewFailedBadSize。
inline constexpr char kReadSourceFailedFmt[]   = "读取失败：%1";   // 源图路径行改为报因。
// 定稿摘要行（状态栏 status::kSessionNewOk 等以 %1 携入本串）。
inline constexpr char kSummaryBaseFmt[]        = "%1×%2 · %3 · 底色(%4,%5,%6,%7)";
inline constexpr char kSummaryFgPartFmt[]      = " · 前景(%1,%2,%3,%4)";   // 非纯色图案追加。
inline constexpr char kSummaryAttachPartFmt[]  = " · 附图(%1, %2)";        // 附加图片路追加（短名 + 源图名）。
} // namespace label
} // namespace idc::gui::texts
