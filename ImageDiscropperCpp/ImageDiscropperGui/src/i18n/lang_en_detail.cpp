// ============================================================================
// 文件：i18n/lang_en_detail.cpp（GUI 英文译文表 · 后半：tooltip / status / msg）
// 作用：GUI 层 tooltip、status、msg 三个可译组的英文目录。与 menu / label 半（lang_en.cpp）分两个 TU，
//       各自一张 constexpr 表，经 installDetailCatalog() 在 installCatalogs() 内被先后登记（同语言累加）。
// 架构定位：GUI 顶层 i18n 基建；本半面向「运行时反馈与人读说明」（悬停 / 状态栏 / 弹窗正文），
//       译文体量与 menu+label 相当且形态各异（含多行 \n 长正文），独立成文件便于核对。
// 键的写法：`分组::常量名`，必须与 include/i18n/gui_texts_detail.h 里的定义**逐字同名**。
//       键写错不报错、只是永不命中（文本静默留在中文），故由对账用例以「无孤儿键 / 无重复键」断言兜住。
// 译文纪律：占位符 %N 的**编号**是契约、逐字保留（语序可自由调整）；`\n` 与 `…` 逐字保留；
//       格式名 PNG / JPG 等、相册路径 Pictures/…、§7 术语 token 不译。
// 装配时机：由 lang_en.cpp 的 installCatalogs() 调用，须与 Core / menu-label 半同在 setupFromPreference 之前完成。
// ============================================================================
#include "i18n/core_i18n.h"

namespace idc::gui {
namespace {

// 英文目录（后半：tooltip、status、msg，与 gui_texts_detail.h 分组对应）。
constexpr core::I18nEntry kEntriesEnDetail[] = {
    // —— status（gui_texts.h::status 全量 107 条，与常量层同序）——
    // 占位符 %1..%5 的**编号**是契约（formatPattern / QString::arg 按编号取参），语序可自由调整；
    // 尺寸乘号 ×、破折占位 —、度数 °、菜单符号 → 等一律逐字沿用源语言写法。
    // 导出回报（export_controller 桌面分支 + android_export_publish 相册发布）
    {"status::kExportNoImage", "No image to export; open an image first."},
    {"status::kExportNoSelection", "Create a selection on the canvas first"},
    {"status::kExportOk", "Export succeeded: %1"},
    {"status::kExportFailed", "Export failed: %1"},
    {"status::kExportNoOutputFiles", "Export failed: no output files were produced"},
    {"status::kExportUnknownError", "Unknown error"},
    // Android 相册发布专属（「兜底」= 复制到应用文档目录）
    {"status::kExportFallbackCopyFailed", "Export failed: %1 (fallback save also failed; temp file: %2)"},
    {"status::kExportFallbackSaved", "Export to the gallery/picked location failed (%1); saved to the app directory: %2"},
    {"status::kExportGalleryFailedAll", "Export failed (%1); files saved to the app directory: %2"},
    {"status::kExportGalleryOkSingle", "Export succeeded: gallery Pictures/%1/%2"},
    {"status::kExportGalleryOkSingleDup", "Export succeeded: gallery Pictures/%1/%2 (same name already existed, actual file name %3)"},
    {"status::kExportGalleryOkMulti", "Export succeeded: gallery Pictures/%1 (%2 file(s))"},
    {"status::kExportGalleryPartial", "Partial success: exported %1/%2 file(s); failed files saved to the app directory: %3"},
    // 会话回报（打开 / 新建 / 图层增删 / 全重置）
    {"status::kSessionOpened", "Opened: %1 (%2×%3)"},
    {"status::kSessionOpenFailed", "Open failed: %1"},
    {"status::kSessionOpenContentFailed", "Open failed: cannot read that content provider address"},
    {"status::kSessionNewFailedBadSize", "New canvas failed: invalid size"},
    {"status::kSessionNewOk", "Created: %1 (keeps layers/annotations/color preprocess)"},
    {"status::kSessionNeedImageForLayer", "Open or create an image before adding an image layer."},
    {"status::kSessionLayerAdded", "Image layer added (switched to Image Pick; drag on the canvas to place the frame)"},
    {"status::kSessionLayerCopied", "Image layer duplicated (placed on top)"},
    {"status::kSessionNoLayersToClear", "There are no image layers to clear"},
    {"status::kSessionLayersCleared", "Cleared all image layers (keeps base image/annotations/preprocess)"},
    {"status::kSessionNothingToReset", "No image to reset"},
    {"status::kSessionAllReset", "Reset everything (base image kept)"},
    {"status::kSessionAllResetNoBase", "Reset everything (base image deleted too; canvas cleared) - use Open/New to load an image again"},
    // 配置存取回报
    {"status::kConfigSaved", "Configuration saved: %1"},
    {"status::kConfigSaveFailed", "Failed to save the configuration: %1"},
    {"status::kConfigLoaded", "Configuration loaded: %1"},
    {"status::kConfigLoadFailed", "Failed to load the configuration: %1"},
    {"status::kDegradedBase", ", the base image's bitmap could not be loaded (shown as a white/transparent backdrop)"},
    {"status::kDegradedLayersFmt", ", %1 layer asset(s) could not be loaded (degraded to empty placeholders)"},
    {"status::kConfigBundledFmt", ", layer assets bundled to %1"},
    {"status::kConfigBundleFailed", "Failed to bundle layer assets: %1"},
    {"status::kConfigNoneSaved", "No saved configuration yet; save one from the More page first."},
    // 右侧参数面板的只读回显
    {"status::kGridDerivedInfo", "Grid: %1 row(s) × %2 column(s) · %3 cell(s) total"},
    {"status::kGridPendingInfo", "Grid: pending"},
    {"status::kGridSelectedInfo", "Selected: %1 cell(s)"},   // 面板初值与同步回显均取本模板（初值传 0）。
    {"status::kGridNoPitchHint", "Both axes are divide counts / weights: the grid tiles exactly with no partial cells, so the remainder policy has no effect."},
    {"status::kRectListItem", "Rect %1: (%2,%3)-(%4,%5)"},
    {"status::kCollapseHintInit", "Collapse feasibility: draw a selection first."},
    {"status::kCollapseHintOk", "Collapse possible: the remaining blocks tile tightly into one image."},
    {"status::kCollapseHintFail", "Collapse not possible: %1 (export will switch to Separate automatically)"},
    // 预处理面板的实时回显与队列行文本
    {"status::kTargetLabelFmt", "Current target: %1"},
    {"status::kTargetLabelInit", "Current target: Canvas"},
    {"status::kCurrentSizeFmt", "Current output size: %1×%2"},
    {"status::kCurrentSizeUnknown", "Current output size: —×—"},
    {"status::kStepInvertDesc", "Channel Invert [%1]"},
    {"status::kStepSplitDesc", "Channel Split [%1]"},
    {"status::kStepRotateDesc", "Rotate %1°"},
    {"status::kStepUnknown", "Unknown step"},
    // 色块按钮的实时回显
    {"status::kSwatchColorText", "Color(%1,%2,%3,%4)"},
    // 预处理回报（「已…」= 状态栏一句话，「正在…」= 忙碌框正文）
    {"status::kPrepQueueLoaded", "Preprocess queue loaded"},
    {"status::kPrepQueueReplayBusy", "Replaying the image with the loaded queue, please wait…"},
    {"status::kPrepCanvasApplied", "Canvas preprocess applied"},
    {"status::kPrepStepAdded", "Preprocess step added"},
    {"status::kPrepStepUpdated", "Preprocess step updated"},
    {"status::kPrepStepRemoved", "Preprocess step removed"},
    {"status::kPrepStepMoved", "Step order adjusted"},
    {"status::kPrepBaseReset", "Base image preprocess reset"},
    {"status::kPrepLayerReset", "Layer preprocess reset"},
    {"status::kPrepReset", "Preprocess reset, original image restored"},
    {"status::kPrepResizeSet", "Canvas size set to %1×%2"},
    {"status::kPrepResizeBusy", "Scaling to the target size, please wait…"},
    {"status::kPrepResizeScaled", "Scaled proportionally to %1×%2"},
    {"status::kPrepResizeScaleBusy", "Scaling the image proportionally, please wait…"},
    {"status::kPrepResizeCleared", "Canvas size cleared"},
    {"status::kPrepRotateAdded", "Rotate %1° added"},
    {"status::kPrepFlipHAdded", "Horizontal flip added"},
    {"status::kPrepFlipVAdded", "Vertical flip added"},
    {"status::kPrepGrayAdded", "Grayscale added"},
    {"status::kPrepInvertAdded", "Per-channel invert added"},
    {"status::kPrepSplitAdded", "Per-channel split added"},
    // 提交链与图层重组
    {"status::kPrepFailedEmpty", "Preprocess failed: the result is an empty image"},
    {"status::kPrepReplayBusy", "Replaying the preprocess queue, please wait…"},
    {"status::kLayerComposeFailed", "Layer compose failed (empty image)"},
    {"status::kLayerRecomposed", "Multi-layer canvas recomposed"},
    {"status::kLayerRecomposeBusy", "Recomposing the canvas, please wait…"},
    // 状态栏常驻回显（初值与逐帧刷新模板成对）
    {"status::kStatusReady", "Ready"},
    {"status::kCoordInit", "Coord: (-, -)"},
    {"status::kCoordFmt", "Coord: (%1, %2)"},
    {"status::kColorInit", "RGBA(-,-,-,-)"},
    {"status::kColorFmt", "RGBA(%1,%2,%3,%4)"},
    {"status::kZoomInit", "Zoom: 100%"},
    {"status::kZoomFmt", "Zoom: %1%"},
    // 参数撤销 / 重做回报
    {"status::kUndone", "Undone"},
    {"status::kRedone", "Redone"},
    {"status::kUndoNone", "Nothing to undo"},
    {"status::kRedoNone", "Nothing to redo"},
    // 导出面板重排区的内联警告（前缀拼在多条警告之前，英语下补一个空格）
    {"status::kWarnPrefix", "Warning: "},
    {"status::kRearrangeColsShortFmt", "Canvas %1×%2=%3 cell(s) < kept block count %4; rows will be added automatically to fit all blocks (does not match the configured canvas size)."},
    {"status::kRearrangeCellWShortFmt", "Cell width %1 is smaller than the grid cell width %2; adjacent blocks will overlap or be clipped out of bounds."},
    {"status::kRearrangeCellHShortFmt", "Cell height %1 is smaller than the grid cell height %2; adjacent blocks will overlap or be clipped out of bounds."},
    // 画布协调器的常驻提示（分隔符两侧各两个空格，与原文同形）
    {"status::kModeTargetHintFmt", "Interaction mode: %1  |  Preprocess target: %2"},
    {"status::kCellsCleared", "Cell selection cleared"},
    {"status::kSelectionCleared", "Selection and cut lines cleared"},
    // 预览刷新的状态提示与输出预览信息行
    {"status::kMaskShown", "Preview mask shown"},
    {"status::kMaskHidden", "Preview mask hidden"},
    {"status::kNeedImage", "Open an image"},
    {"status::kNoImageLoaded", "No image loaded."},
    {"status::kDragAppendRect", "Drag on the canvas to append a rectangle"},
    {"status::kDragCreateSel", "Drag on the canvas to create a selection"},
    {"status::kWaitingRect", "Waiting for rectangle…"},
    {"status::kWaitingSelection", "Waiting for selection…"},
    {"status::kPreviewNoResultFmt", "No valid result: %1"},
    {"status::kGridPreviewFmt", "Grid: %1×%2 · output canvas %3×%4 · %5 block(s) kept"},
    {"status::kMultiRectPreviewFmt", "Multi-rect: %1 · output canvas %2×%3 · %4 block(s) kept"},
    {"status::kOutPreviewFmt", "Output canvas: %1×%2 · %3 block(s) kept · %4"},
    {"status::kCollapseYes", "Collapsible"},
    {"status::kCollapseNo", "Rearrange needed"},
    // 新建画布 / 添加图层的定稿忙碌框正文
    {"status::kNewCanvasGenerateBusy", "Generating the image, please wait…"},
    // —— msg（gui_texts.h::msg 全量 30 条，与常量层同序）——
    // 弹窗正文：`\n` 与 `%1..%5` 逐字保留；长正文按「整段一个键」纳入，行首空格与空行是排版的一部分。
    {"msg::kRearrangeWarnTitle", "Rearrange Parameter Warning"},
    {"msg::kRearrangeAskContinue", "\n\nContinue with the export anyway?"},
    {"msg::kRearrangeDontRemind", "Don't remind me again"},
    {"msg::kBurnSkipTitle", "Export Excludes Annotations/Layers"},
    {"msg::kBurnSkipBodyAnno", "The canvas has annotations, but \"Burn in annotations on export\" is not checked; the export will not include them."},
    {"msg::kBurnSkipBodyLayers", "The canvas has image layers, but \"Burn in images on export\" is not checked; the export will not include them."},
    {"msg::kBurnSkipBodyBoth", "The canvas has both annotations and image layers, but neither burn-in is checked; the export will include neither."},
    {"msg::kBurnSkipEnableExport", "Burn in and export"},
    {"msg::kBurnSkipExportAsIs", "Export as-is"},
    {"msg::kBurnSkipDontRemind", "Don't remind me again"},
    {"msg::kShortcutNeedModifier", "Combo keys need Ctrl or Alt"},
    {"msg::kShortcutConflictTitle", "Shortcut Conflict"},
    {"msg::kShortcutConflictBody", "Already used by \"%1\". Rebind and clear?"},
    {"msg::kShortcutResetConflictBody", "Reset to defaults left these commands sharing one key; adjust them on their rows:\n%1"},
    {"msg::kClearLayersTitle", "Clear All Layers"},
    {"msg::kClearLayersAsk", "Clear all %1 image layer(s)? Only the base image will be kept (annotations and preprocess are kept)."},
    {"msg::kResetAllTitle", "Reset All"},
    {"msg::kResetAllAsk", "Will clear: image layers, all annotations, all preprocess steps, selection.\nThe base image is kept by default; tick the box below to delete it too (clear to no image). Continue?"},
    {"msg::kResetAllDeleteBase", "Also delete the base image (clear to no image)"},
    {"msg::kClearAnnosTitle", "Clear All Annotations"},
    {"msg::kClearAnnosAsk", "Clear all %1 annotation(s)? This can be reverted with Undo Annotation."},
    // 内核调用桥的失败报因（Core 自身的错误文本不在本层重复定义）
    {"msg::kBridgeEmptyImagePath", "The path is empty; cannot open an image"},
    {"msg::kBridgeReadImageFailed", "Cannot read the image: %1"},
    {"msg::kBridgeNoOutputPath", "No output path specified"},
    {"msg::kBridgeWriteOutputFailed", "Export failed: cannot write %1"},
    {"msg::kBridgeNoConfigPath", "No configuration file path specified"},
    {"msg::kBridgeWriteConfigFailed", "Cannot write the configuration file: %1"},
    {"msg::kBridgeReadConfigFailed", "Cannot read or parse the configuration file: %1"},
    // Android 相册发布（MediaStore）的失败报因
    {"msg::kMediaNoResolver", "Cannot obtain the system content resolver"},
    {"msg::kMediaNoStore", "Cannot obtain the system media store"},
    {"msg::kMediaOpenWriteFailed", "Cannot open the target location for writing"},
    {"msg::kMediaReadTempFailed", "Cannot read the temporary export file: %1"},
    {"msg::kMediaReadTempError", "Failed to read the temporary export file"},
    {"msg::kMediaWriteTargetFailed", "Failed to write the target location"},
    {"msg::kMediaApiTooLow", "Publishing to the gallery needs Android 10 (API 29) or above"},
    {"msg::kMediaInsertFailed", "Media store insert failed (the format may be unsupported)"},
    {"msg::kMediaFinalizeFailed", "Failed to finalize the media store entry"},
    {"msg::kMediaCleanupFailed", "Failed to clean up the unfinished entry"},
    // 命令行自述
    {"msg::kCliDescription", "ImageDiscropper image slicing GUI"},
    {"msg::kCliImageArgHelp", "Optional: image path to open at startup."},
    // 帮助菜单正文（桌面 / 移动 / 关于）
    {"msg::kHelpUsageBody",
        "1) File -> Open Image, or File -> New Canvas (size / background color / procedural pattern).\n"
        "2) Pick a mode: Standard Extract / Reverse Remove / Grid Split; polarity Keep Inside / Remove Inside.\n"
        "3) Standard Extract / Reverse Remove: drag on the canvas to make a selection (Reverse Remove accepts multiple rectangles); orange cut lines span the whole image, green keeps and red removes.\n"
        "   Drag the selection / corner handles / cut lines to adjust; arrow keys nudge (Shift for a larger step).\n"
        "4) Grid Split: the Parameters page on the right sets the origin, cell size and remainder policy; Select All / Invert / Clear Cell Selection and the order policy control the output (the Edit / Mode menus share these entries).\n"
        "5) Preprocess: the Image menu or the Preprocess page - rotate / flip / grayscale / invert / channel split + a separate canvas size (resize); the queue can be added to, trimmed and reordered.\n"
        "6) Annotations: Annotation -> Tools picks a shape, then draw on the canvas (the left tool panel works too); the Annotation property page edits styles and duplicates / removes the selected annotation.\n"
        "7) Multi-layer canvas: Image -> Add Image Layer; in Image Pick mode drag to place the frame, the Image menu reorders, duplicates and removes layers; the Layers panel toggles each layer's visibility.\n"
        "8) Export: File -> Export; File -> Export Settings switches Separate / Collapse / Rearrange, the format and the burn-in flags; the Export page carries every parameter.\n"
        "9) File -> Save Configuration / Load Configuration: the whole parameter set (annotations and layers included) can be saved/loaded, reused and undone.\n\n"
        "Shortcuts (mode/polarity/clear/delete, undo/redo, open/new/export, zoom) can be viewed and rebound in the\n"
        "View menu's Appearance/Interaction dialog, on the Shortcuts page (effective immediately, saved across sessions);\n"
        "they are not listed here so this text stays accurate after you rebind one.\n"
        "Arrow-key nudging (Shift for a larger step) and Esc-to-finish-a-drawing are fixed keys, not customizable."},
    {"msg::kMobileHelpBody",
        "1) Load an image with Open on the tool bar, or build a canvas from scratch with New (size / background / pattern).\n"
        "2) The Mode drawer page picks Standard Extract / Reverse Remove / Grid Split and the Keep/Remove polarity; drag on the canvas to select.\n"
        "3) The Parameters page sets the cut parameters: shape / method / grid quadruple, remainder policy and order.\n"
        "4) The Preprocess page: rotate / flip / grayscale / invert / channel split + canvas size.\n"
        "5) Pick an annotation tool on the Mode page, then draw on the canvas; the Annotation page edits style / stacking / fill, deletes the selection and clears all.\n"
        "6) The Image page adds, reorders and removes image layers and places the anchor / pivot.\n"
        "7) The Export page sets the output mode / format / burn-in flags; produce the files with Export on the tool bar.\n"
        "8) More: load/save configuration, appearance/interaction (selection snap and confirm before rearrange live here), reset all, exit.\n\n"
        "Gestures: pinch to zoom, double-tap to fit the window, long press = right click (long press finishes a drawing; the menu can clear cut lines);\n"
        "the Step pad replaces the arrow keys for nudging the selection."},
    {"msg::kAboutBody",
        "ImageDiscropper GUI %1 (core %2)\n"
        "    -- An image region extraction / reverse removal / grid splitting tool.\n"
        "    Project: https://github.com/GoldenCreeperJ/ImageDiscropper"},
    // —— tooltip（gui_texts.h::tooltip 全量 150 条，与常量层同序）——
    // 「」/“” 圆括号释文一律以曲引号 “ ” 呈现；× ° → 等符号逐字沿用；、。等中文标点改为 ASCII 句读。
    {"tooltip::kNewCanvas", "New blank or procedural canvas (size, background, pattern)."},
    {"tooltip::kExportRearrange", "Rearrange output onto a cols×rows canvas; adjust rows/cols and cell size."},
    {"tooltip::kBurnAnnotation", "On export, composite the annotations into the pixels (annotations are cut along with the image)."},
    {"tooltip::kBurnImageLayers", "On export, merge in the image layers (independent from the Layers panel’s “Show”, which is preview-only)."},
    {"tooltip::kConfirmRearrange", "Confirm before export when rearrange is risky; off exports directly."},
    {"tooltip::kConfirmBurnIn", "Remind before export when annotations or image layers exist but their burn-in is unchecked; off exports directly."},
    {"tooltip::kLoadConfig", "Restore cut/sort/export parameters from a config file; undoable."},
    {"tooltip::kSaveConfig", "Save current parameters to a config file, reusable for same-size images."},
    {"tooltip::kUndo", "Undo the last step (annotation, or selection/sort/export parameters)."},
    {"tooltip::kRedo", "Redo: reapply the last undone operation."},
    {"tooltip::kClearSelection", "Clear the selection and cut lines (grid / multi-rect / single-rect)."},
    {"tooltip::kResetAll", "Clear layers / annotations / preprocessing / selection, keeping only the base."},
    {"tooltip::kModeL3", "Grid splitting: a grid tiled over the whole image + cell selection + ordering."},
    {"tooltip::kPolarityKeep", "Keep the cells inside the selection (green mask)."},
    {"tooltip::kPolarityRemove", "Remove the cells inside the selection (red mask), keep the rest."},
    {"tooltip::kCanvasMarquee", "Marquee to create / drag a selection; annotation and image boxes aren’t pickable."},
    {"tooltip::kCanvasImagePick", "Click / drag an image layer’s placement box to adjust its position."},
    {"tooltip::kCanvasAnnoSelect", "Click / drag / use handles to transform an existing annotation."},
    {"tooltip::kCanvasAnnoCreate", "Draw a new annotation on the canvas using the shape chosen in “Annotation → Tools”."},
    {"tooltip::kSortReverse", "Reverse the entire fill path (can combine with row/column-major)."},
    {"tooltip::kSortSnake", "Reverse every other row (row-major) / column (column-major)."},
    {"tooltip::kAddImageLayer", "Add an image layer to the canvas from a file."},
    {"tooltip::kClearAllImageLayers", "Clear all image layers, keeping only the base."},
    {"tooltip::kLayerUp", "Raise the currently picked image layer by one (toward the top)."},
    {"tooltip::kLayerDown", "Lower the currently picked image layer by one (toward the bottom)."},
    {"tooltip::kLayerRemove", "Delete the currently picked image layer."},
    {"tooltip::kAnnoDeleteSelected", "Delete the currently selected annotation."},
    {"tooltip::kAnnoFill", "Toggle the selected annotation’s fill (sets the default when none selected)."},
    {"tooltip::kSnap", "Snap to image edges and midlines when dragging, remembered across sessions."},
    {"tooltip::kAppearance", "Customize the canvas palette and interaction preferences; remembered across sessions."},
    {"tooltip::kExportModeCombo", "Separate: one image per block; collapse: butt the rest into one after dropping whole rows/columns; rearrange: fill blocks into a new canvas in order."},
    {"tooltip::kNamingTemplate", "File-name template: {name}=file name, {index}=number ({index:03d} zero-padded), {row}/{col}=row/column."},
    {"tooltip::kMergeCols", "Column count of the rearrange canvas; “Auto” derives it from the kept-block count."},
    {"tooltip::kMergeRows", "Row count of the rearrange canvas; “Auto” derives it from the kept-block count."},
    {"tooltip::kMergeCellW", "Width of the rearranged cell; “Auto” uses the kept blocks’ original size."},
    {"tooltip::kMergeCellH", "Height of the rearranged cell; “Auto” uses the kept blocks’ original size."},
    {"tooltip::kAutoGrid", "Auto-derive and fill rows/cols from the kept-block count (near-square)."},
    {"tooltip::kMergeOrder", "Rearrange fill order: row-major or column-major."},
    {"tooltip::kMergeSnake", "Snake layout: fill every other row (row-major) or column (column-major) in reverse."},
    {"tooltip::kMergeReverse", "Reverse: fill backward starting from the last block."},
    {"tooltip::kPadColor", "Background color for the merged canvas, pad-remainder padding (same for separate and merged export) and for flattening transparency in lossy formats (JPEG/WebP/BMP); transparent by default. Click to choose."},
    {"tooltip::kPreviewThumb", "Low-res preview of the output (illustrative; export stays at original resolution)."},
    {"tooltip::kBurnInHint", "When checked, annotations are drawn into the exported image; otherwise omitted."},
    {"tooltip::kLayersBurnInHint", "Merge image layers into pixels on export; independent of the panel’s “Show” (preview-only)."},
    {"tooltip::kExportButton", "Export with the current parameters."},
    {"tooltip::kParamL1ShapeCombo", "Choose the shape of the kept region: rectangular selection / whole horizontal band / whole vertical band."},
    {"tooltip::kParamL2SubCombo", "Each rectangle deletes a cross band spanning the image; multiple rectangles take the union."},
    {"tooltip::kParamRectList", "Drag on the canvas to add / scale a rectangle; select an item to fine-tune or delete it above."},
    {"tooltip::kParamToGrid", "Use the selection’s top-left as origin and its size as the cell, switch to grid mode for refinement."},
    {"tooltip::kGridOriginXSpin", "Origin x0: the starting position of the vertical cut lines."},
    {"tooltip::kGridOriginYSpin", "Origin y0: the starting position of the horizontal cut lines."},
    {"tooltip::kCellWidthSpin", "Cell width cw: the spacing between adjacent vertical cut lines (pixels)."},
    {"tooltip::kCellHeightSpin", "Cell height ch: the spacing between adjacent horizontal cut lines (pixels)."},
    {"tooltip::kRemainderCombo", "Partial edge cells: discard, keep, or pad to full size."},
    {"tooltip::kGridModeCombo", "How this axis is defined: period (origin + cell size) / divide count (equal parts) / weights; the two axes can each choose their own."},
    {"tooltip::kGridCountSpin", "Parts: split this direction into equal parts (origin is always 0); the last part absorbs the rounding error and must not exceed the pixel count."},
    {"tooltip::kGridWeightsEdit", "Weights: comma-separated positive numbers or percentages, e.g. 1,2,1 or 25%,50%,25%; only the relative ratio matters and the last part absorbs the rounding error."},
    {"tooltip::kSortCombo", "Output order: row-major / column-major / custom."},
    {"tooltip::kLayerAddImage", "Add an image layer (generated pattern or imported image); overflow is clipped at compositing."},
    {"tooltip::kLayerMoveUp", "Move the selected layer one step toward the top (more in front)."},
    {"tooltip::kLayerMoveDown", "Move the selected layer one step toward the bottom (more behind)."},
    {"tooltip::kLayerMoveTop", "Move the selected layer to the very top (covering all other image layers)."},
    {"tooltip::kLayerMoveBottom", "Move the selected layer to the very bottom (below all image layers, above the base)."},
    {"tooltip::kLayerEditRemove", "Delete the selected layer."},
    {"tooltip::kLayerCopy", "Duplicate the selected layer to the top (same declaration, independent pixels)."},
    {"tooltip::kLayerAnchorCombo", "Canvas anchor: which position of the canvas the layer aligns to."},
    {"tooltip::kLayerPivotCombo", "Image pivot: which part of the layer itself aligns to the anchor."},
    {"tooltip::kLayerOffsetXSpin", "Free offset X (added on top of the anchor/pivot result)."},
    {"tooltip::kLayerOffsetYSpin", "Free offset Y."},
    {"tooltip::kLayerWidthSpin", "Target width (0 = use the native size, no scaling)."},
    {"tooltip::kLayerHeightSpin", "Target height (0 = use the native size, no scaling)."},
    {"tooltip::kLayerOpacitySpin", "Opacity 0–255 (255 = opaque); affects preview and export."},
    {"tooltip::kPreprocessTarget", "Preprocessing targets the current object: selected layer / base (color only) / canvas."},
    {"tooltip::kResizeEdgeSpin", "Target edge (px); with aspect lock, changing width updates height."},
    {"tooltip::kResizeScaleSpin", "Scale by a percentage of the current target canvas size (or the original size if none)."},
    {"tooltip::kApplySizeBtn", "Set the working image to the absolute size entered above."},
    {"tooltip::kScaleSizeBtn", "Scale the canvas size by the percentage above."},
    {"tooltip::kClearSizeBtn", "Cancel the canvas size, return to the original size (keeps the operation queue)."},
    {"tooltip::kResetPreprocessBtn", "Discard the canvas size and all preprocessing, restoring the original image."},
    {"tooltip::kGeoAddBtn", "Append the geometry operation above to the end of the geometry list."},
    {"tooltip::kGeoReplaceBtn", "Replace the selected step in the geometry list in place with the current configuration."},
    {"tooltip::kColorAddBtn", "Append the color operation above to the end of the color list."},
    {"tooltip::kColorAddBlocked", "The queue already yields grayscale; this is redundant or a no-op, so it can’t be added."},
    {"tooltip::kColorReplaceBtn", "Replace the selected step in the color list in place with the current configuration."},
    {"tooltip::kGeoList", "Geometry queue (rotate / flip); select to delete / move / update."},
    {"tooltip::kColorList", "Color queue (grayscale / invert / split); select to delete / move / update."},
    {"tooltip::kGrayOutItem", "The queue already yields grayscale: this operation is redundant or a no-op on grayscale"},
    {"tooltip::kStrokeColorBtn", "The annotation’s stroke color (RGBA)."},
    {"tooltip::kStrokeWidthSpin", "Stroke line width (original-image pixels); burned in at this thickness"},
    {"tooltip::kFillCheckTip", "Whether the shape fills its interior; line / text cannot be filled (greyed out). For the fill color see the next row."},
    {"tooltip::kFillColorBtn", "The annotation’s fill color (independent of the border; fillable shapes only)."},
    {"tooltip::kTextEditField", "The content of a text annotation (use after selecting the text tool and clicking a placement point)"},
    {"tooltip::kFontSizeSpin", "The font size of a text annotation (pixels)"},
    {"tooltip::kCornerRadiusSpin", "Corner radius (0 = sharp); only polygon / polyline vertices (others grey out)."},
    {"tooltip::kScaleXSpin", "Horizontal scale (100% original, negative flips); live-linked with canvas handles."},
    {"tooltip::kScaleYSpin", "Vertical scale (100% original, negative flips); live-linked with canvas handles."},
    {"tooltip::kRotateSpin", "Rotation angle (cumulative); live-linked with canvas handles."},
    {"tooltip::kStackRaiseBtn", "Raise the selected annotation by one layer (toward the top)."},
    {"tooltip::kStackLowerBtn", "Lower the selected annotation by one layer (toward the bottom)."},
    {"tooltip::kStackToFrontBtn", "Move the selected annotation to the very top."},
    {"tooltip::kStackToBackBtn", "Move the selected annotation to the very bottom."},
    {"tooltip::kDeleteSelectedAnnoBtn", "Delete the selected annotation (greyed out if none; undoable)."},
    {"tooltip::kCopySelectedAnnoBtn", "Duplicate the selected annotation to the top (slightly offset so it can be dragged away)."},
    {"tooltip::kAnnoCopySelected", "Duplicate the selected annotation to the top (slightly offset so it can be dragged away)."},
    {"tooltip::kClearAllAnnoBtn", "Clear all annotations (base/layers/preprocessing untouched; undoable)."},
    {"tooltip::kModeMarqueeBtn", "Marquee to create / drag a selection; annotation and image boxes aren’t pickable"},
    {"tooltip::kModeImagePickBtn", "Click / drag an image layer’s placement box to reposition it"},
    {"tooltip::kModeAnnoSelectBtn", "Click / drag / transform an existing annotation; marquee disabled"},
    {"tooltip::kModeAnnoCreateBtn", "Choose a shape below, then draw a new annotation on the canvas"},
    {"tooltip::kToolRectBtn", "Drag two points to draw a rectangle (corner radius > 0 rounds it)"},
    {"tooltip::kToolSquareBtn", "Placement is one vertex, drag to set the side (corner radius > 0 rounds it)"},
    {"tooltip::kToolRhombusBtn", "The start point is the center, drag to draw a rhombus"},
    {"tooltip::kToolCircleBtn", "The start point is the center, drag to draw a circle"},
    {"tooltip::kToolEllipseBtn", "The placement point is the center, drag to set the two semi-axes and draw an ellipse"},
    {"tooltip::kToolIsoTriBtn", "The start point is the midpoint of the base, drag to draw an isosceles triangle"},
    {"tooltip::kToolEquTriBtn", "Placement is the base midpoint, drag to draw an equilateral triangle (apex toward cursor)"},
    {"tooltip::kToolRightTriBtn", "The placement point is the right-angle vertex, drag along the two axes to set the two leg lengths"},
    {"tooltip::kToolIsoRightTriBtn", "Placement is the right-angle vertex, drag to draw an isosceles right triangle"},
    {"tooltip::kToolLineBtn", "Drag between two points to draw a straight line segment"},
    {"tooltip::kToolPolylineBtn", "Click repeatedly to append vertices, press Esc or switch tools to finish"},
    {"tooltip::kToolTextBtn", "Click a placement point, enter the text content, then add"},
    {"tooltip::kToolBrushBtn", "Hold and drag to draw a free path, press Esc or switch tools to finish"},
    {"tooltip::kLayerShowBase", "Show / hide the base image (affects only the canvas preview, not the export)"},
    {"tooltip::kLayerShowImage", "Show / hide image layers (hidden → not composited or exported)"},
    {"tooltip::kLayerShowAnno", "Show / hide the annotation layer (vector overlay, base pixels unchanged)"},
    {"tooltip::kLayerShowMask", "Show / hide the keep(green)/delete(red) preview masks (not the export)"},
    {"tooltip::kLayerShowGrid", "Show / hide grid lines and cut lines (one switch: grid lines in Grid Split, the orange cut lines spanning the whole image in the other modes)"},
    {"tooltip::kLayerShowSelection", "Show / hide the selection border and handles; when hidden it can’t be edited"},
    {"tooltip::kLayerShowNumber", "Show / hide the cell-number badges (appears only with custom ordering)"},
    {"tooltip::kStepFactorBtn", "Toggle the step factor (equivalent to the desktop Shift large step)"},
    {"tooltip::kModeL1Btn", "Keep the region inside the box for a cropped result; the default mode."},
    {"tooltip::kModeL2Btn", "Each rectangle deletes a cross band spanning the image; multiple take the union."},
    {"tooltip::kModeL3Btn", "Tile the whole image with an origin + cell size, select cells, and order them for export."},
    {"tooltip::kPolarityKeepBtn", "Keep the cells inside the selection (green mask)."},
    {"tooltip::kPolarityRemoveBtn", "Remove the cells inside the selection (red mask), keep the rest."},
    {"tooltip::kPlaceOffsetXTip", "Free offset X (added on top of the anchor/pivot)"},
    {"tooltip::kPlaceOffsetYTip", "Free offset Y"},
    {"tooltip::kTbResetAllTip", "One-click reset: clear layers/annotations/preprocessing/selection, keeping only the base."},
    {"tooltip::kHandleSizeSpin", "Screen px for handles / edge strips / pick threshold; “Auto” uses the platform default."},
    {"tooltip::kScrollMarginSpin", "How far the canvas can pan beyond its edge (viewport %); “None” stops at the image edge."},
    {"tooltip::kZoomStepSpin", "The zoom-factor increment per zoom-in/zoom-out button press (the wheel step is fixed and does not follow this)."},
    {"tooltip::kZoomMinSpin", "The minimum allowed zoom factor (below this it auto-converges)."},
    {"tooltip::kZoomMaxSpin", "The maximum allowed zoom factor (above this it auto-converges)."},
    {"tooltip::kNewWidthSpin", "Default width of the “New Canvas” dialog (automatically remembered after each confirmed new canvas)."},
    {"tooltip::kNewHeightSpin", "Default height of the “New Canvas” dialog (automatically remembered after each confirmed new canvas)."},
    {"tooltip::kThemeCombo", "Light/Dark of the app chrome: follow the OS or fix Light/Dark; independent of the canvas colors."},
    {"tooltip::kPatternBgSwatch", "Choose the background color (transparency settable, 0 = fully transparent)"},
    {"tooltip::kPatternFgSwatch", "Choose the foreground / pattern color (transparency settable, non-solid only)"},
    {"tooltip::kPatternSeedSpin", "Noise seed: same seed + size always gives the same image"},
    {"tooltip::kLayerPlaceRowTip", "Placement of the layer box relative to the base (segment 2: where the layer box is placed on the base)."},
    {"tooltip::kImagePlaceRowTip", "Placement of the image relative to the layer box (segment 1: where the source is placed within the layer box)."},
    {"tooltip::kAttachImageTip", "When checked, place a source image into the layer box by the fit mode, then set it on the base by the layer placement;\nwhen unchecked, you can export a pure-pattern canvas directly."},
    {"tooltip::kPickSourceImageBtn", "Choose the source image to attach (PNG/JPG/BMP/WebP, etc.), decoded by the engine"},
};

} // namespace

namespace i18n {

void installDetailCatalog() {
    core::i18n::installLanguage(core::kLangCodeEn, core::makeI18nTable(kEntriesEnDetail));
}

} // namespace i18n
} // namespace idc::gui
