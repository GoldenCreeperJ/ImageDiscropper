// ============================================================================
// 文件：src/i18n/lang_en.cpp
// 作用：Core 层英文译文表——一行一条「键 → 文本」，并经 installCoreCatalogs() 登记给本层内核。
// 架构定位：与 GUI / CLI 的表**方向相反**：那两层的表住在各自层，免得纯逻辑静态库为无关文案膨胀；
//       本表的内容就是 Core 自己的报因与图案名，住在这里才不重复——GUI 弹窗与 CLI stderr 显示的是
//       同一批句子，各存一份迟早漂移。Core 静态库被三层共用，故这张表在进程里也只有一份。
// 谁装配：GUI 的 i18n::installCatalogs() 与 CLI 的 i18n::installCatalogs() 各调用本函数一次
//       （installLanguage 对同一语言是累加的，两次调用落到同一目录，不冲突也不重复）。
//       显式调用而非静态对象自注册：CLI 侧是静态库，未被引用的翻译单元会被链接器
//       整块丢弃，自注册的登记动作会随调用点变化而静默失效。
// 键的写法：`分组::常量名`，必须与 include/i18n/core_texts.h 里的定义**逐字同名**。
//       键写错不报错、只是永不命中（文本静默留在中文），故由
//       ImageDiscropperCore/tests/test_i18n_catalog.cpp 以「无孤儿键 / 无重复键」断言在 CI 兜住。
// 可译范围：**只有 msg 组**。jsonkey（§7 JSON 键名）与 defaults（落盘 token 与缺省值）是文件契约，
//       翻译即破坏配置向后兼容，永不进表。
// 译文纪律：
//       · 句式里的 `%1` / `%2` 占位符**逐字保留且编号不变**（填值由 i18n::formatPattern 按编号取，
//         故可自由调整语序——"Annotation item #%1: %2" 与 "第 %1 个标注元素：%2" 共用一份实参）；
//       · 面向人的引擎报因不携带 E-x / SPEC §x 内部标记；字段名（points / fontSize / type）仍属术语，不译；
//       · 图案显示名对应 CLI --pattern 的 solid / stripes 等 token 的**面向人**译名，token 本身不变。
// 加一门语言：新增 src/i18n/lang_xx.cpp（照本文件形状写表）+ 在下方 installCoreCatalogs() 加一行
//       + 在 core_i18n.h 补语言码常量。重编即可，无构建期工具、无资源文件。
// ============================================================================
#include "i18n/core_i18n.h"

namespace idc::core::i18n {
namespace {

// 英文目录：按 core_texts.h msg 组的分组顺序罗列，方便与常量层逐行对账。
constexpr I18nEntry kEntriesCoreEn[] = {
    // —— 引擎执行失败（engine.cpp）——
    {"msg::kEngineSourceEmpty", "Source image is empty; nothing to cut"},
    {"msg::kEngineRectDegenerate", "Invalid selection: the rectangle width and height must be positive; no cut line can be generated"},
    {"msg::kEngineMultiRectEmpty", "Multi-rect is empty: no rectangle is available for cutting"},
    {"msg::kEngineNoGrid", "Cut produced no valid grid: the cell size or image size must be positive"},
    {"msg::kEngineGridCountInvalid", "Invalid grid split count: the horizontal / vertical count must be positive and must not exceed that axis's pixel size"},
    {"msg::kEngineGridWeightsInvalid", "Invalid grid ratio: every weight must be positive and the number of weights must not exceed that axis's pixel size"},
    {"msg::kEngineGridCellInvalid", "Invalid grid cell: the cell size must be positive and the split must not produce zero-width cells"},
    {"msg::kEngineDeleteSetEmpty", "Delete set is empty: no cell was selected for removal"},
    {"msg::kEngineKeepSetEmpty", "Keep set is empty: the delete area covers the whole image, or nothing is selected"},
    {"msg::kEngineRearrangeNeedsL3", "Merged rearrange is only supported in grid mode: use merged collapse or separate export instead"},
    {"msg::kEngineNotCollapsible", "The keep set cannot collapse, and standard extraction / reverse removal do not support merged rearrange: "
                                    "adjust the cut lines so the delete area covers a full row/column, or use separate export"},
    {"msg::kEngineSourceEmptyAfterPreprocess", "The image is empty after preprocess; nothing to cut"},
    // —— 标注 JSON 逐条校验报因（annotation_json.cpp）：以「'」收尾的片段保留尾引号，后接原值就地拼接 ——
    {"msg::kErrMissingNumField", "Missing or invalid numeric field '%1'"},
    {"msg::kErrAnchorDegenerate", "Degenerate anchors: "},
    {"msg::kErrRectSize", "Rectangle width and height must be > 0"},
    {"msg::kErrSquareSize", "Square side length must be > 0"},
    {"msg::kErrRhombusSize", "Both rhombus half-diagonals must be > 0"},
    {"msg::kErrIsosTriangleSize", "Isosceles triangle half-base and height must be > 0"},
    {"msg::kErrEquTriangleSize", "Equilateral triangle size must be > 0"},
    {"msg::kErrRectTriangleSize", "Both legs of the right triangle must be > 0"},
    {"msg::kErrEquRectTriangleSize", "The leg of the isosceles right triangle must be > 0"},
    {"msg::kErrCircleDegenerate", "Degenerate anchors: the circle radius must be > 0"},
    {"msg::kErrEllipseDegenerate", "Degenerate anchors: both ellipse semi-axes must be > 0"},
    {"msg::kErrNoPoints", "Missing points array"},
    {"msg::kErrLineNeeds2Points", "Line points must be exactly 2 vertices"},
    {"msg::kErrPolylineNeedsPoints", "Polyline points must have at least 2 vertices"},
    {"msg::kErrPathNoSegments", "path is missing a non-empty segments array"},
    {"msg::kErrPathNoSegment", "path has no valid segment"},
    {"msg::kErrFontSizeNotPositive", "Text fontSize must be positive"},
    {"msg::kErrFontSizeNotNumber", "Text fontSize must be a number"},
    {"msg::kErrNoTypeField", "Missing type field"},
    {"msg::kErrUnknownType", "Unknown type: '"},
    {"msg::kErrBadColor", "color is not valid hex: '"},
    {"msg::kErrBadFillColor", "fillColor is not valid hex: '"},
    {"msg::kErrStrokeWidthNotInt", "strokeWidth must be an integer"},
    {"msg::kErrZOrderNotInt", "zOrder must be an integer"},
    {"msg::kErrNotAnObject", "entry is not an object"},
    {"msg::kErrAnnoSectionInvalid", "The config annotations section has invalid entries: "},
    {"msg::kErrAnnoTextEmpty", "annotation JSON is empty"},
    {"msg::kErrAnnoRootType", "The annotation JSON root must be an object (single annotation) or an array (multiple annotations)"},
    {"msg::kErrAnnoValidationFailed", "Annotation validation failed: "},
    {"msg::kErrAnnoNoValidEntry", "The annotation JSON has no valid entry"},
    {"msg::kErrLayerTextEmpty", "layer JSON is empty"},
    {"msg::kErrLayerRootType", "The layer JSON root must be an object (single layer) or an array (multiple layers)"},
    {"msg::kErrLayerNoValidEntry", "The layer JSON has no valid entry"},
    {"msg::kErrLayerPercentNoBasis", "Layer field '%1' uses a percentage, but the canvas size is unknown (the canvas width/height must be determined first)"},
    {"msg::kErrAnnoParseFailed", "Annotation JSON parse failed: "},
    {"msg::kErrAnnoPercentNoBasis", "Annotation geometry '%1' uses a percentage, but the coordinate reference size is unknown (the work-image width/height must be determined first)"},
    // —— §7 配置 schema 严格校验报因（schema_validate.cpp::validateSchema）：%1＝JSON 路径、%2＝原值，皆数据不译 ——
    {"msg::kErrConfigNotObject", "Config field '%1' must be an object"},
    {"msg::kErrConfigNotArray", "Config field '%1' must be an array"},
    {"msg::kErrConfigNotInteger", "Config field '%1' must be an integer"},
    {"msg::kErrConfigIllegalKey", "Config field '%1' is not a legal key in this section"},
    {"msg::kErrConfigNotCoord", "Config field '%1' must be an integer or a percentage string such as \"50%\", got %2"},
    {"msg::kErrConfigPercentNoBasis", "Config field '%1' uses a percentage, but the reference size is unknown (source width/height must be positive integers)"},
    {"msg::kErrConfigRange", "Config field '%1' must be between %2 and %3"},
    {"msg::kErrConfigNotBool", "Config field '%1' must be a boolean"},
    {"msg::kErrConfigNotString", "Config field '%1' must be a string"},
    {"msg::kErrConfigBadEnum", "Unrecognized value '%2' for config field '%1'"},
    {"msg::kErrConfigBadColor", "Config field '%1' is not a valid hex color '%2'"},
    {"msg::kErrConfigGridMixed", "Config field '%1' cannot be combined with '%2': use either originX/originY/cellWidth/cellHeight or the per-axis x/y form for grid params"},
    {"msg::kErrConfigGridAxisMode", "Config field '%1' has conflicting keys: choose exactly one of origin/cell/remainder, count, or weights"},
    {"msg::kErrConfigGridWeight", "Weight '%2' of config field '%1' must be a positive number or a percentage string"},
    // —— 逐条报因裹句（%N 编号不变，译文可自由调序）——
    {"msg::kErrAnnoItemPrefix", "Annotation item #%1: %2"},
    {"msg::kErrAnnoElementPrefix", "Item #%1: %2"},
    // —— 图案类型显示名（pattern.cpp::patternKindName，GUI 下拉与提示直接显示）——
    {"msg::kPatternSolid", "Solid"},
    {"msg::kPatternLinearGradient", "Linear gradient"},
    {"msg::kPatternRadialGradient", "Radial gradient"},
    {"msg::kPatternCheckerboard", "Checkerboard"},
    {"msg::kPatternStripes", "Stripes"},
    {"msg::kPatternGridLines", "Grid lines"},
    {"msg::kPatternDotGrid", "Dot grid"},
    {"msg::kPatternConcentric", "Concentric rings"},
    {"msg::kPatternRadialRays", "Radial rays"},
    {"msg::kPatternHueGradient", "Hue gradient"},
    {"msg::kPatternNoise", "Noise"},
};

} // namespace

void installCoreCatalogs() {
    installLanguage(kLangCodeEn, makeI18nTable(kEntriesCoreEn));
}

} // namespace idc::core::i18n
