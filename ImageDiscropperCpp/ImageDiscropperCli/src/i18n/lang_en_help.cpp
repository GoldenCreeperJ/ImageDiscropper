// ============================================================================
// 文件：src/i18n/lang_en_help.cpp（CLI 英文译文表 · 后半：help 组）
// 作用：CLI help 组的英文目录——帮助散文块 kText*（多行 R"IDCH(...)" 整段一键）、描述列 kDesc* /
//       小节 lead kLead* 的译文。与 msg 半（lang_en.cpp）分两个 TU，各自一张 constexpr 表，
//       经 installHelpCatalog() 在 installCatalogs() 内被先后登记（installLanguage 同语言累加）。
// 架构定位：CLI 顶层 i18n 基建；help 译文体量与 msg 相当且形态迥异（长散文），独立成文件便于核对。
// 键的写法：`help::常量名`，必须与 include/i18n/cli_texts_help.h 里的定义**逐字同名**。
//       **kUse*（用法列：选项名 + 占位符）属脚本契约，永不进表**——与 texts::opt 逐字配对。
// 装配时机：由 lang_en.cpp 的 installCatalogs() 调用，故必须与 Core / msg 半同在 applyLanguage 之前完成。
// 缺项不补——查表未命中自动回退源语言（zh-CN 常量层原文），译文可逐批改、逐批合。
// ============================================================================
#include "i18n/core_i18n.h"

namespace idc::cli {
namespace {

// 英文目录（后半：help 组，与 cli_texts_help.h 同序分块罗列）。
constexpr core::I18nEntry kEntriesEnHelp[] = {
    // —— help 描述列 kDesc* / 小节 lead kLead*（kUse* 用法列为脚本契约不译）——
    {"help::kDescHelp", "Show help"},
    {"help::kDescVersion", "Show version"},
    {"help::kTextSeeThisHelp", "  Show this help: idc --help "},
    {"help::kDescVerbose", "Write progress diagnostics to stderr (silent by default; result lines always go to stdout)"},
    {"help::kDescLang", "Set the language of help and error text (zh-CN / en); when unset, falls back to IDC_LANG, LC_ALL, LANG"},
    {"help::kDescTopSaveConfig", "Serialize the current parameters to a JSON config file (dry-run, no job executed)"},
        {"help::kDescTopAnnoPtr", "[repeatable] command-line annotations (fields and syntax: idc --help annotations)"},
    {"help::kDescTopLayerPtr", "[repeatable] command-line layers (fields and syntax: idc --help layers)"},
    {"help::kDescTopGenPtr", "procedural generated source (applicability matrix: idc help generate)"},
{"help::kDescTopAssetsDir", "Asset-dir three states: absent = JSON only; bare flag = bundle into <configname>.assets/ when used with --save-config; =path = custom directory (prepended to the lookup chain on load, bundle output on save; the path must use the '=' form)"},
    {"help::kDescTopStrict", "Strict mode: upgrades all three default degradations -- the missing-layer-asset placeholder fallback, the silent crop on size mismatch, and the empty output -- to error exits (exit code 3/2, so unattended scripts can abort on a non-zero exit code)"},
    {"help::kDescInput", "Input image path (with --size also given: --size fixes the canvas size and the bitmap is centered)"},
    {"help::kDescRectKeep", "Rectangle region (keep the center rectangle)"},
    {"help::kDescHbandKeep", "Horizontal band (keep the whole band where y is in [y1,y2))"},
    {"help::kDescVbandKeep", "Vertical band (keep the whole band where x is in [x1,x2))"},
    {"help::kDescRectErase", "[repeatable] once = single-rectangle cross cut; multiple = multi-rect union remove"},
    {"help::kDescHbandErase", "Delete the whole horizontal band where y is in [y1,y2)"},
    {"help::kDescVbandErase", "Delete the whole vertical band where x is in [x1,x2)"},
    {"help::kDescOutputRequired", "Output image path (give at least one of this and --save-config; may be omitted for a dry-run)"},
    {"help::kDescFormatInfer", "Output format png/jpeg/webp/bmp (inferred from the --output extension by default)"},
    {"help::kDescFormatPngDefault", "Output format png/jpeg/webp/bmp (default png; when merging, the --output extension takes priority)"},
    {"help::kDescFormatPlain", "Output format png/jpeg/webp/bmp"},
    {"help::kDescQuality", "Lossy quality 0~100 (default 90; only jpeg/webp take effect, png/bmp ignore it)"},
    {"help::kDescNaming", "File name template for separated export: {name} = output folder name (falls back to region), {index} = index, {index:03d} = zero-padded index, {row}/{col} = source grid row/column number (0 for non-grid sources); default {name}_{index:03d}; applies to separated export only (not merged single image), must be non-empty and contain no path separators"},
    {"help::kDescMerge", "collapse only; default is separate export (rearrange is exclusive to the grid command)"},
    {"help::kDescOutputMergeErase", "Merged output path (used with --merge collapse; give at least one of this and --save-config)"},
    {"help::kDescOutputMergeCompose", "Merged output path (used with --compose; give at least one of this and --save-config)"},
    {"help::kDescOutputDirErase", "Separate output directory (used for the default separate export; created automatically if missing; give at least one of this and --save-config)"},
    {"help::kDescOutputDirGrid", "Separate output directory (used for the default separate export; give at least one of this and --save-config)"},
    {"help::kDescOutputConfigMerged", "Merged output path (used when the config has export.mode=merged)"},
    {"help::kDescOutputDirConfigSeparate", "Separate output directory (used when the config has export.mode=separate)"},
    {"help::kDescAnnoInline", "[repeatable] inline one annotation object, or an array of annotation objects"},
    {"help::kDescAnnoFile", "[repeatable] read from a JSON file whose root is an annotation array"},
    {"help::kDescLayerInline", R"([repeatable] inline one layer (v2 JSON object / array), or the two equivalent shorthands: flat {file=a.png,imageOpacity=200,backdrop={"kind":"solid"}} / block image={...} backdrop={...})"},
    {"help::kDescLayerFile", "[repeatable] read from a JSON file whose root is a layer array"},
    {"help::kDescGrid", "Whole-canvas period: origin + cell size (the origin may be negative, cw/ch must be positive after conversion; each value may be a percentage such as \"50%\")"},
    {"help::kDescGridX", "Horizontal period: origin x0 + cell width cw (percentages convert against the image width); an undefined vertical axis = one whole span"},
    {"help::kDescGridY", "Vertical period: origin y0 + cell height ch (percentages convert against the image height); an undefined horizontal axis = one whole span"},
    {"help::kDescDivide", "Divide counts ColxRow for both axes (e.g. 3x4 = 3 columns by 4 rows): origin always 0, split as evenly as possible, the last piece absorbs the rounding error"},
    {"help::kDescDivideX", "Horizontal divide count (origin always 0, split as evenly as possible); an undefined vertical axis = one whole span"},
    {"help::kDescDivideY", "Vertical divide count (origin always 0, split as evenly as possible); an undefined horizontal axis = one whole span"},
    {"help::kDescWeightsX", "Horizontal weights (positive numbers or percentage strings, e.g. 1,2,1 or 25%,50%,25%): only the relative ratio matters, the last piece absorbs the rounding error"},
    {"help::kDescWeightsY", "Vertical weights (positive numbers or percentage strings): only the relative ratio matters, the last piece absorbs the rounding error"},
    {"help::kDescKeepCell", "[repeatable] keep the specified cell(s): one r,c per flag, or a semicolon list r,c;r,c (row, column from 0)"},
    {"help::kDescRemoveCell", "[repeatable] remove the specified cell(s): one r,c per flag, or a semicolon list r,c;r,c (row, column from 0)"},
    {"help::kDescSort", "Sorting strategy: row-major traverse by rows (number row by row, left to right within each row, default) / column-major traverse by columns (number column by column, top to bottom within each column) / custom (the picked order is the sequence, requires --order)"},
    {"help::kDescDecorate", "Order decoration (not available with --sort custom): none (default) / reverse the whole order / snake reverses every other row/column / reverse-snake and snake-reverse are equivalent (fixed order: snake first, then reverse)"},
    {"help::kDescOrderCustom", "Custom sequence (required with --sort custom): defines both the kept cells and their output order (any permutation), mutually exclusive with --keep/--remove/--decorate - --keep/--remove only pick the set while --sort fixes the order, this option sets both"},
    {"help::kDescMargin", "Remainder policy (partial cells where the grid crosses the image boundary): discard them (keep only complete cells, default) / keep-partial keeps them cropped to the image / pad keeps them at full cell size, the out-of-bounds part filled with --pad-color"},
    {"help::kDescCompose", "Merge into a single image (rearrange); default is separate export"},
    {"help::kDescCanvas", "Rearrange canvas cell columns x rows, e.g. 2x2 (required with --compose)"},
    {"help::kDescMergeSort", "Rearrange fill order row-major (default) / column-major (orthogonal to the --sort selection order; custom not supported)"},
    {"help::kDescMergeDecorate", "Rearrange fill decoration none (default) / reverse / snake / reverse-snake / snake-reverse"},
    {"help::kDescPadColorDefault", "Padding fill color #RRGGBB / #AARRGGBB (fills the out-of-bounds part of --margin pad cells and the gaps of the rearrange-merged canvas, transparent by default)"},
    {"help::kDescPadColorFlatten", "Background color #RRGGBB/#AARRGGBB used to flatten transparent pixels in lossy formats (jpeg/bmp) (default transparent -> black; specify it explicitly when exporting a transparent canvas to jpeg)"},
        {"help::kDescFit", "Bitmap-to-canvas adaptation: crop fill / stretch / native (applies when --input and --size are both given, default native centered)"},
    {"help::kDescFitOverride", "Bitmap-to-canvas adaptation override crop / stretch / native (applies when --input and --size are both given; overrides the config placement's fit when present)"},
{"help::kDescSizeGen", "Generated canvas size (positive integer pixels; each side <= 65535 and width x height <= 64,000,000 ~= 256MB; fixes the canvas size when --input is also given)"},
    {"help::kDescSizeGenRequired", "[required] generated canvas size (positive integer pixels; each side <= 65535 and width x height <= 64,000,000 ~= 256MB)"},
    {"help::kDescPatternKinds", "solid(default)/linear-gradient/radial-gradient/checkerboard/stripes/grid-lines/dot-grid/concentric/radial-rays/hue-gradient/noise"},
    {"help::kDescOrientationDirs", "horizontal(default)/vertical/diagonal-down/diagonal-up -- linear-gradient axis / stripe direction (incl. diagonal) / concentric-ring shape / hue-scan axis"},
    {"help::kDescBg", "Background color #RRGGBB/#AARRGGBB (alpha applies; default opaque white #FFFFFFFF)"},
    {"help::kDescFg", "Foreground/pattern color #RRGGBB/#AARRGGBB (default black #FF000000)"},
    {"help::kDescPeriod", "Period / cell edge / ring spacing / hue wavelength in pixels (positive integer, default 32)"},
    {"help::kDescLineWidth", "Grid-line / concentric-ring (round or square) line width in pixels (positive integer, default 1)"},
    {"help::kDescDotRadius", "Dot-grid dot radius in pixels (positive integer, default 2)"},
    {"help::kDescSeed", "Noise random seed (non-negative integer, default 0; only --pattern noise takes effect; the same seed always yields the same image)"},
    {"help::kDescNoise", "Noise color mode full-color(default)/grayscale/two-color (only --pattern noise takes effect)"},
    {"help::kDescNewOutput", "Path to write the generated image (give at least one of this and --save-config)"},
        {"help::kDescLoad", "[required] read all parameters from a JSON config file"},
    {"help::kDescTierOverride", "Tier override extract / erase / grid"},
    {"help::kDescPolarityOverride", "Polarity override keep / remove"},
    {"help::kDescCutRectOverride", "Cut rectangle override (choose one with --hband / --vband)"},
    {"help::kDescHbandOverride", "Horizontal band override y1,y2 (turned into a full-width band at execution; cannot be combined with --save-config)"},
    {"help::kDescVbandOverride", "Vertical band override x1,x2 (same as above)"},
    {"help::kDescExportModeOverride", "Export mode override merged / separate"},
    {"help::kDescExportLayoutOverride", "Merge layout override collapse / rearrange"},
    {"help::kDescExportFormatOverride", "Output format override png/jpeg/webp/bmp"},
    {"help::kDescCanvasOverride", "Rearrange canvas override colsxrows (used with --export-layout rearrange)"},
            {"help::kDescConfigInput", "Base image path (with --size also given: --size fixes the canvas size and the bitmap is placed per the config's placement or centered; combinable with the config's layers section, layers stack on top of this base; both may be omitted when the config has a generate section)"},
    {"help::kLeadGenOverride", "Command-line generation options may be combined with --input (--size fixes the canvas size, the bitmap is placed per the config's placement or centered); if there is no --input/--size and the config has a generate section, the canvas is composed from the config."},
    {"help::kLeadAnnoAppend", "Command-line annotations are appended after the --load config's own annotations section; --replace-annotations replaces it instead (with no command-line annotations the section is cleared)."},
    {"help::kLeadLayerAppend", "Command-line layers are appended after the --load config's own layers section (missing zOrder auto-increments, stacking on top); --replace-layers replaces it instead (with no command-line layers the section is removed)."},
    {"help::kLeadLayerOverlay", "Command-line layers are composited as overlays on top of the input (mirroring --annotation; file assets resolve relative to the current working directory)."},
    {"help::kLeadAnnoNew", "Coordinates are relative to the generated canvas size; burned in after layer compositing (annotations are always on top)."},
    {"help::kLeadLayerNew", "Command-line layers are composited as overlays on top of the generated canvas (mirroring --annotation; file assets resolve relative to the current working directory)."},
    // —— 小节标题（kTextSec*：与 zh 版逐段对齐的标题行；缺条目会漏出中文标题）——
    {"help::kTextSecCutThree", "\nCut lines (choose one of --rect / --hband / --vband):\n"},
    {"help::kTextSecOutput", "\nOutput:\n"},
    {"help::kTextSecExport", "\nExport:\n"},
    {"help::kTextSecCommonBlank", "\n\nCommon:\n"},
    {"help::kTextSecCommon", "\nCommon:\n"},
    {"help::kTextSecGrid", "\nGrid:\n"},
    {"help::kTextSecSelect", "\nSelection (choose one of --keep / --remove):\n"},
    {"help::kTextSecSortDecorate", "\nSorting and decoration:\n"},
    {"help::kTextSecMargin", "\nMargin:\n"},
    {"help::kTextSecCompose", "\nRearrange-merge:\n"},
    {"help::kTextSecOverride", "\nField overrides (optional, override the config's corresponding fields; applied both to the --save-config output and to execution):\n"},
    {"help::kTextSecIo", "\nInput / output:\n"},
    {"help::kTextSecSizeBg", "\nSize and background:\n"},
    {"help::kTextSecPattern", "\nPattern:\n"},
    {"help::kTextCommonOptsPtr", "Job-level common options (--save-config / --assets-dir): see idc --help\n"},
    {"help::kTextPatternMatrix",
        "\n"
        "Pattern x parameter applicability matrix (parameters not listed have no effect on that pattern, matching actual engine behavior):\n"
        "    solid            the whole canvas = --bg; ignores --fg/--orientation/--period/--line-width/--dot-radius\n"
        "    linear-gradient  --bg --fg linearly interpolated along --orientation (horizontal along x, vertical along y); ignores --period/--line-width/--dot-radius\n"
        "    radial-gradient  --bg (edge) --fg (center) radial interpolation; --orientation has no effect; ignores --period/--line-width/--dot-radius\n"
        "    checkerboard     --fg cell side = --period (--orientation has no effect); ignores --line-width/--dot-radius\n"
        "    stripes          --fg stripe width = --period; --orientation horizontal=vertical stripes (along x)/vertical=horizontal stripes (along y)/diagonal-down (along x-y)/diagonal-up (along x+y); ignores --line-width/--dot-radius\n"
        "    grid-lines       --fg grid lines: spacing = --period, line width = --line-width; --orientation has no effect; ignores --dot-radius\n"
        "    dot-grid         --fg dots: spacing = --period, radius = --dot-radius; --orientation has no effect; ignores --line-width\n"
        "    concentric       --fg concentric rings: ring spacing = --period, ring width = --line-width; --orientation horizontal=circle rings (Euclidean)/vertical=square rings (Chebyshev); ignores --dot-radius\n"
        "    radial-rays      --fg radial sectors: density = --period (smaller is denser); --orientation has no effect; ignores --line-width/--dot-radius\n"
        "    hue-gradient     full-hue rainbow scanned along --orientation (H 0 to 1, fully saturated, fully bright, opaque), --period = one rainbow's pixel width; ignores --bg/--fg/--line-width/--dot-radius\n"
        "    noise            deterministic pseudo-random noise: --seed sets the seed (the same seed + the same size always yield the same image, for pixel-by-pixel comparison), --noise picks full-color (default)/grayscale/two-color; two-color picks randomly between --bg and --fg, the rest ignore --bg/--fg/--orientation/--period/--line-width/--dot-radius\n"
        "  alpha: the #AARRGGBB alpha of --bg/--fg takes part in interpolation and is written; PNG/WebP keep transparency, JPEG/BMP export flattens with the pad color.\n"
        "  Note: no pattern type includes an image sticker; images are introduced via the config's layers section (file layers) or generate-section placement declarations, composed in by config --load.\n"},
    {"help::kTextGenTail",
        "\n"
        "  Examples:\n"
        "    # a radial-gradient canvas as extract's generated source, keeping the center 100x100\n"
        "    idc extract --size 200x200 --pattern radial-gradient --bg \"#FF000000\" --fg \"#FFFFFFFF\" --rect 50,50,150,150 --output grad.png\n"
        "    # checkerboard input for erase: cross cut then collapse-merge\n"
        "    idc erase --size 300x200 --pattern checkerboard --fg \"#FF000000\" --period 50 --rect 100,100,200,150 --merge collapse --output out.png\n"
        "    # deterministic noise (the same --seed always yields the same image, for pixel-by-pixel comparison)\n"
        "    idc extract --size 256x256 --pattern noise --seed 7 --rect 0,0,256,256 --output n.png\n"
        "  Constraints and exit codes:\n"
        "    - give at least one of --input and --size; --size each side <= 65535 and width x height <= 64,000,000.\n"
        "    - Illegal arguments (pattern kind / color / direction / period etc.) -> exit code 1, reasons listed one by one.\n"
        "    - With --save-config only the source size and the cut / annotations / layers are written; the pattern arguments are not written as a generate section - use idc new --save-config to persist a recipe.\n"},
    // —— help 散文块 kText*（逐字搬移：换行、缩进、示例与末尾换行都是 --help 输出的一部分；
    //    为规避目录测试逐物理行取键，示例里的 {\"...\"} 一律用相邻字符串字面量，物理行首恒以 \" 起）——
    {"help::kTextTopIntro",
        "idc — image region extraction / reverse removal / grid splitting tool\n"
        "\n"
        "Usage:\n"
        "  idc <command> [options]\n"
        "  idc --version\n"
        "  idc --help [<subcommand> | <topic>]    command help / topic help\n"
        "\n"
        "Subcommands:\n"
        "  extract   standard extraction: keep rectangle / horizontal band / vertical band, output a single image\n"
        "  erase     reverse removal: delete cross band / horizontal band / vertical band / multi-rect union, separate or merged export\n"
        "  grid      grid splitting: tile a grid by an origin and cell size, then separate or rearrange-merge the selected cells\n"
        "  new       New canvas: generate a canvas by size / background(alpha) / procedural pattern and write it to disk\n"
        "  config    Run one complete job from a JSON config file (with optional annotations / multi-layer canvas)\n"
        "\n"
    },
    {"help::kTextTopModsHead", "\nRun modifiers:\n"},
    {"help::kTextTopJobHead", "\nJob-level common options:\n"},
        {"help::kTextTopTail",
        "\n"
        "Exit codes:\n"
        "  0 success | 1 argument error | 2 runtime error | 3 input file error | 4 output failure | 5 internal error\n"
        "\n"
        "Examples:\n"
        "  # keep a rectangle (the center cell)\n"
        "  idc extract --input photo.jpg --rect 100,100,300,250 --output out.png\n"
        "  # cross cut, collapse-merged into a single image\n"
        "  idc erase --input photo.jpg --rect 100,100,300,250 --merge collapse --output out.png\n"
        "  # cross cut, separate-export the four corners to a folder\n"
        "  idc erase --input photo.jpg --rect 100,100,300,250 --output-dir ./out/ --format png\n"
        "  # grid keep the four corners and rearrange-merge onto a 2x2 canvas\n"
        "  idc grid --input photo.jpg --grid 100,100,200,150 --keep 0,0 --keep 0,2 --keep 2,0 --keep 2,2 --compose --canvas 2x2 --output result.png\n"
        "  # Burn in annotations: extract also burns in one red-stroked rectangle\n"
        "  idc extract --input photo.jpg --rect 100,100,300,250 --annotation '{\"type\":\"rectangle\",\"x1\":10,\"y1\":10,\"x2\":80,\"y2\":50,\"color\":\"#FFFF0000\",\"strokeWidth\":3}' --output out.png\n"
        "  # New: generate a 1024x768 checkerboard (white base / semi-transparent blue) and write it\n"
        "  idc new --size 1024x768 --pattern checkerboard --bg \"#FFFFFFFF\" --fg \"#800000FF\" --period 64 --output board.png\n"
        "  # Cut a generated source: use a procedurally generated canvas directly as the input (--size replaces --input)\n"
        "  idc extract --size 200x200 --pattern radial-gradient --bg \"#FF000000\" --fg \"#FFFFFFFF\" --rect 50,50,150,150 --output grad.png\n"
    },
        {"help::kTextAnnoSeeTopic", "\nCommand-line annotations: --annotation / --annotations (repeatable); fields and syntax: idc --help annotations\n"},
    {"help::kTextLayerSeeTopic", "\nCommand-line layers: --layer / --layers (repeatable); fields and syntax: idc --help layers\n"},
    {"help::kTextGenSeeTopic", "\nGenerated source: --size (with --pattern, etc.) may be combined with --input - --size fixes the canvas size, the bitmap is centered (native size by default; --fit changes the adaptation); the full applicability matrix: idc --help generate\n"},
    {"help::kTextGenParamsPtr", "\nPattern arguments (--pattern / --orientation / --bg / --fg / --period / --line-width / --dot-radius / --seed / --noise) and the applicability matrix are the same as the new command: idc --help new\n"},
    {"help::kTextGenTopicHead", "\nProcedural generated source (shared by extract / erase / grid / config; the same pattern parameter set as the new command): use --size (optionally with --pattern, etc.) to procedurally generate a canvas; it may be combined with --input - --size fixes the canvas size, the bitmap is centered (native size by default; --fit changes the adaptation; clipped when over, exposing the base when under):\n"},
{"help::kTextAnnoHead", "\nAnnotations (optional, burned into the working image before cutting; the coordinate origin = the working-image size after preprocessing; geometry coordinates also accept percentage strings, e.g. \"50%\", horizontal against the working width and vertical against the height):\n"},
    {"help::kTextLayerHead", "\nLayers (optional: flattened into the engine as overlay layers, mirroring annotations; transform x/y/width/height also accept percentage strings, basis = canvas size; the config command appends them after the --load config's own layers section):\n"},
    {"help::kTextLayerFields", "  Fields are grouped by element / placement / stacking (defaults in parentheses; assembly order = image source processing (adaptation -> element preprocess) -> region sizing -> backdrop element -> image placement composite -> region scale -> node appearance):\n"
        "    image element (optional; absent = pure-pattern layer):\n"
        "      file: a relative asset name (e.g. \"sub/a.png\") - looked up in [custom dir (--assets-dir) -> <configname>.assets/ -> config directory];\n"
        "             a missing asset degrades to an empty placeholder (skipped when compositing, no error)\n"
        "      fit source-to-box adaptation: crop fill (default) / stretch / native\n"
        "      boxWidth / boxHeight layer-box size (<=0 = no box, the in-box placement degrades - the source is placed directly by transform)\n"
        "      anchor / pivot in-box anchor / pivot (9-way enum as above, default center); x / y free in-box offsets (may be negative, default 0)\n"
        "      preprocess element-appearance preprocess queue (same steps as the node preprocess; geometric steps allowed - applied to the source before placement, and without a box the region follows the post-preprocess source size); opacity element-appearance opacity 0..255 (default 255)\n"
        "    backdrop element (optional; absent = transparent base):\n"
        "      the pattern keys match --pattern (kind / background / foreground / period / line-width / dot-radius /\n"
        "        orientation / seed / noise) + preprocess element-appearance preprocess queue (color steps only, geometric steps ignored)\n"
        "        + opacity element-appearance opacity 0..255 (default 255)\n"
        "    transform node placement (all optional):\n"
        "      inCanvasAnchor canvas anchor / layerPivot layer pivot - 9-way enum: top-left / top-center / top-right /\n"
        "        middle-left / center (default) / middle-right / bottom-left / bottom-center / bottom-right\n"
        "      x / y: free pixel offsets (may be negative, default 0; also accept percentage strings, horizontal against the canvas width and vertical against the height)\n"
        "      width / height: target size (<=0 = source / pattern native size, no scaling; default 0; also accept percentage strings against the canvas width / height)\n"
        "      opacity: overall layer opacity 0..255 (default 255 = opaque; applied per pixel at composite time)\n"
        "    zOrder stacking order: integer, ascending (larger is on top; equal values keep array order, later on top); default 0 -\n"
        "      command-line appends auto-increment and stack on top of the config's own layers\n"
        "    preprocess node-appearance preprocess array (materialized before placement; same as the config preprocess section):\n"
        "      {\"op\":\"gray\"} grayscale; {\"op\":\"split\",\"mask\":\"111\"} channel split / {\"op\":\"invert\",\"mask\":\"111\"} invert (mask = 3-char RGB mask, '1' enables)\n"
        "      {\"op\":\"rotate\",\"deg\":90} rotate (any integer degrees); {\"op\":\"flip\",\"axis\":\"horizontal\"|\"vertical\"} flip\n"
        "      {\"op\":\"resize\",\"width\":W,\"height\":H} resample to W x H\n"
    },
    {"help::kTextLayerTail",
        "\n"
        "  Validation and exit codes (mandatory per-item validation like annotations, reasons reported one by one):\n"
        "    - a misspelled anchor / fit / pattern key, an illegal color, a numeric field that is neither an integer nor a percentage string, or an opacity outside 0..255 -> exit code 1, the reason pinpoints the field (e.g. \"Config field '[0].transform.inCanvasAnchor' has unknown value 'bogus'\").\n"
        "    - zOrder must be an integer (negative is legal, treated as the bottom); image.file must be a string (empty = no image element); a JSON syntax error, a root that is not an object/array, or an array with no valid layer object -> exit code 1.\n"
        "    - an escaping file name (absolute path / containing ..) -> exit code 3 at execution; a missing file degrades to an empty placeholder with a warning, --strict turns it into exit code 3.\n"
        "  Single-item examples (JSON form; see the --layer option description for the flat / block shorthands):\n"
        "    {\"image\":{\"file\":\"badge.png\"},\"transform\":{\"inCanvasAnchor\":\"top-right\",\"layerPivot\":\"top-right\"}}\n"
        "    {\"backdrop\":{\"kind\":\"stripes\",\"foreground\":\"#FF000000\"},\"transform\":{\"width\":320,\"height\":240}}\n"
        "    {\"image\":{\"file\":\"badge.png\",\"fit\":\"stretch\",\"boxWidth\":160,\"boxHeight\":120,\"opacity\":200},\n"
        "     \"backdrop\":{\"kind\":\"solid\",\"background\":\"#FFFFFFFF\"},\"transform\":{\"x\":0,\"y\":0}}\n"
        "  Export: --save-config writes the command-line layers into the config file's layers section (the config command appends them after the --load config's own section; the other commands carry them through).\n"
    },
    {"help::kTextAnnoFields",
        "  Fields dispatch geometry by type; the styling applies to all types:\n"
        "    Geometry coordinates (x/y, x1..y2, points, segments[].coords) accept integers or percentage strings (e.g. \"50%\", >100% and negatives are legal; coordinates only - fontSize / cornerRadius / strokeWidth / zOrder still require integers):\n"
        "    · line: points exactly 2 vertices [[x1,y1],[x2,y2]]\n"
        "    · polyline: points >= 2 vertices (open segment)\n"
        "    · constrained primitives - write only the two anchors x1,y1,x2,y2 (the geometry is built by the engine from the anchors and is always valid; hand-written vertices are not accepted); all may take an optional cornerRadius (rounded-corner pixels):\n"
        "        rectangle: (x1,y1) and (x2,y2) are the two opposite corners\n"
        "        square: (x1,y1) is one vertex, side length = max(|dx|,|dy|), growing toward the quadrant of (x2,y2)\n"
        "        rhombus: (x1,y1) is the center, (|dx|,|dy|) are the two half-diagonals\n"
        "        isos-triangle / equ-triangle: (x1,y1) is the midpoint of the base, (|dx|,|dy|) set the half-base and the height (equilateral takes the side length by the main direction)\n"
        "        rect-triangle / equ-rect-triangle: (x1,y1) is the right-angle vertex, (|dx|,|dy|) set the two legs (isosceles-right takes the equal max)\n"
        "    · ellipse / circle - likewise write only the two anchors x1,y1,x2,y2 (the geometry is built by the engine from the anchors; bounding-box width/height are not accepted):\n"
        "        circle: (x1,y1) is the center, radius = |(x1,y1)->(x2,y2)|\n"
        "        ellipse: (x1,y1) is the center, (|dx|,|dy|) are the two semi-axes (both must be > 0)\n"
        "    · path free path: segments=[{\"t\":type,\"coords\":numeric array},...] (must be non-empty and contain at least one valid segment)\n"
        "        t=M/L -> coords=[x,y]; t=Q -> coords=[controlX,controlY,x,y]; t=C -> coords=[c1x,c1y,c2x,c2y,x,y]; t=Z -> coords=[]\n"
        "    · text: x, y (start point), text (content), fontSize (pixel font size, must be > 0 if given)\n"
        "  Style fields (all optional, take defaults):\n"
        "    color stroke color, fillColor fill color - both support #RRGGBB (opaque) or #AARRGGBB (AA is alpha: 00 fully transparent ~ FF fully opaque)\n"
        "    strokeWidth line width in pixels (default 2); fill whether to fill true/false (default false)\n"
        "    zOrder stacking order (integer, 0 = bottom, larger = higher; may be omitted, default falls back to the element's index in the array) - later-drawn top layer wins hit-testing, --save-config round-trips it with the annotations section; a non-integer (e.g. a decimal) is invalid, a negative is valid (treated as the bottom order, normalized to a non-negative index on load)\n"
        "    transform (the GUI's scale/rotate snapshot) is usually omitted on the command line; the geometry fields always carry local coordinates\n"
        "  Validation and exit codes (mandatory validation, per-item cause reported):\n"
        "    · Any single illegal annotation fails the whole --annotation/--annotations (exit code 1, stderr lists \"item N: reason\" per entry); nothing is silently dropped.\n"
        "    · A missing type or an unknown string always errors (does not default to line).\n"
        "    · A degenerate anchor reports \"degenerate anchor\": rectangle/rhombus need |dx|>0 and |dy|>0; square/isosceles-right need max(|dx|,|dy|)>0; isosceles/equilateral need the half-base and height, right need both legs non-degenerate; circle needs radius > 0, ellipse needs both semi-axes > 0.\n"
        "    · Free primitives: line must have exactly 2 points; text's fontSize must be > 0 if given; path must have non-empty and valid segments.\n"
        "    · A geometry field that is neither an integer nor a percentage string errors (e.g. \"wide%\"); a percentage requires a known basis (converted after reading the image), otherwise it errors instead of collapsing to 0.\n"
        "    · color/fillColor must be a valid #RRGGBB or #AARRGGBB; strokeWidth / zOrder must be integers (a decimal/string zOrder errors; negative is valid). A JSON syntax error or a root that is not an object/array also gives exit code 1.\n"
        "  Single-item examples:\n"
        "    {\"type\":\"line\",\"points\":[[10,10],[200,150]],\"color\":\"#FF0000\",\"strokeWidth\":4}\n"
        "    {\"type\":\"rectangle\",\"x1\":10,\"y1\":10,\"x2\":120,\"y2\":80,\"fill\":true,\"fillColor\":\"#80FF0000\",\"strokeWidth\":2}\n"
        "    {\"type\":\"rhombus\",\"x1\":60,\"y1\":50,\"x2\":110,\"y2\":20,\"color\":\"#FF0000\"}\n"
        "    {\"type\":\"circle\",\"x1\":70,\"y1\":70,\"x2\":100,\"y2\":70,\"color\":\"#00FF00\"}\n"
        "    {\"type\":\"text\",\"x\":20,\"y\":30,\"text\":\"HELLO\",\"fontSize\":18,\"color\":\"#0000FF\"}\n"
        "    {\"type\":\"path\",\"segments\":[{\"t\":\"M\",\"coords\":[0,0]},{\"t\":\"L\",\"coords\":[50,50]},{\"t\":\"Z\",\"coords\":[]}],\"color\":\"#00FFFF\"}\n"
        "  Multiple: repeat --annotation once each, or pass an array at once [{\"type\":\"line\",...},{\"type\":\"circle\",...}]; you can also use --annotations file to batch-read from a file.\n"
        "  Export: --save-config also writes the command-line annotations into the config file's annotations section (round-trips without loss).\n"},
    {"help::kTextExtractIntro",
        "idc extract - standard extraction mode\n"
        "\n"
        "Coordinates: pixel integers, or percentage strings (e.g. \"50%\", converted against the input image width / height).\n"
        "\n"
        "Input:\n"},
    {"help::kTextExtractTail",
        "\n"
        "Examples:\n"
        "  idc extract --input photo.jpg --rect 100,100,300,250 --output out.png\n"
        "  idc extract --input photo.jpg --hband 100,250 --output band.png\n"
        "  idc extract --input photo.jpg --vband 100,300 --output band.png\n"
        "  # Keep the rectangle and also burn in one rectangle annotation\n"
        "  idc extract --input photo.jpg --rect 100,100,300,250 --annotation '{\"type\":\"rectangle\",\"x1\":50,\"y1\":50,\"x2\":120,\"y2\":120,\"fill\":true,\"fillColor\":\"#FFFF0000\"}' --output out.png\n"
        "\n"
        "Common errors:\n"
        "  Exit code 1: missing input image (--input or --size) / missing --output; the choose-one-of-three is not satisfied (no cut line, or several given at once); illegal or degenerate coordinates (x1>=x2 / y1>=y2; for a degenerate selection box use --hband / --vband instead)\n"
        "  Exit code 3: the input image does not exist or cannot be read\n"
        "  Exit code 4: writing the output failed\n"},
    {"help::kTextEraseIntro",
        "idc erase - reverse removal mode (differentiated kernel)\n"
        "\n"
        "Coordinates: pixel integers, or percentage strings (e.g. \"50%\", converted against the input image width / height).\n"
        "\n"
        "Input:\n"},
    {"help::kTextEraseTail",
        "\n"
        "Constraints:\n"
        "  - --rect / --hband / --vband are mutually exclusive; --merge and --output-dir are mutually exclusive.\n"
        "  - --merge collapse but the selection set cannot be collapsed -> exit code 2 (no rearrange available; adjust the cut lines so\n"
        "    the removed region covers whole rows / columns, or switch to separate export).\n"
        "\n"
        "Examples:\n"
        "  idc erase --input photo.jpg --rect 100,100,300,250 --output-dir ./out/ --format png\n"
        "  idc erase --input photo.jpg --rect 100,100,300,250 --merge collapse --output out.png\n"
        "  idc erase --input photo.jpg --rect 100,100,200,150 --rect 400,300,500,400 --merge collapse --output out.png\n"
        "\n"
        "Common errors:\n"
        "  Exit code 1: missing required / mutual-exclusion conflict / illegal or degenerate coordinates (use --hband / --vband for a degenerate selection box) / --merge rearrange (does not support rearranging)\n"
        "  Exit code 2: collapse not feasible (see Constraints)\n"
        "  Exit code 3: input image error\n"
        "  Exit code 4: output directory / file write failed\n"},
    {"help::kTextGridIntro",
        "idc grid - grid splitting mode (complete-expressiveness tier)\n"
        "\n"
        "Coordinates: pixel integers, or percentage strings (e.g. \"50%\", converted against the input image width / height).\n"
        "\n"
        "Grid definition: pick one of three forms (give it for the whole canvas, or per axis with the -x / -y variants; each axis takes exactly one form):\n"
        "  · period   --grid / --grid-x / --grid-y: origin + cell size, boundary-crossing partial cells handled by --margin;\n"
        "  · divide   --divide / --divide-x / --divide-y: origin always 0, split as evenly as possible, the last piece absorbs the rounding error;\n"
        "  · weights  --weights-x / --weights-y: origin always 0, allocated by weight, the last piece absorbs the rounding error.\n"
        "  Divide counts and weights always tile the canvas exactly with no partial cells, so --margin is not needed; an undefined axis counts as one whole span.\n"
        "\n"
        "Input:\n"},
    {"help::kTextGridTail",
        "\n"
        "Constraints:\n"
        "  - --keep and --remove are mutually exclusive; --compose and --output-dir are mutually exclusive.\n"
        "  - Grid definition: --grid and --divide are mutually exclusive, and neither may be combined with the per-axis options (--grid-x/--grid-y, --divide-x/--divide-y, --weights-x/--weights-y); each axis takes exactly one form.\n"
        "  - The remainder policy --margin only applies to a period axis (divide counts / weights have no partial cells).\n"
        "  - --decorate is not available with --sort custom; --merge-sort does not support custom (rearrange fill is only\n"
        "    row / column major; use --sort custom for a custom block order).\n"
        "  - With --compose, --canvas cols/rows are required and must be positive.\n"
        "  - The engine applies snake first, then reverses as a whole (fixed order), so reverse-snake and snake-reverse give the same result.\n"
        "\n"
        "Examples:\n"
        "  idc grid --input photo.jpg --grid 100,100,200,150 --keep 0,0 --keep 0,2 --keep 2,0 --keep 2,2 --compose --canvas 2x2 --output result.png\n"
        "  idc grid --input photo.jpg --grid 0,0,100,100 --remove 1,1 --output-dir ./cells/\n"
        "  idc grid --input photo.jpg --grid 0,0,100,100 --sort custom --order 0,0;1,1;2,2 --compose --canvas 3x1 --output diag.png\n"
        "  # 3 columns by 4 rows, split evenly (the last piece absorbs the rounding error), export the bottom-left cell\n"
        "  idc grid --input photo.jpg --divide 3x4 --keep 3,0 --output-dir ./cells/\n"
        "  # 3 even columns horizontally + a fixed 200px period vertically (mixed per-axis forms)\n"
        "  idc grid --input photo.jpg --divide-x 3 --grid-y 0,200 --keep 0,0 --output-dir ./cells/\n"
        "  # split horizontally by the ratio 1:2:1\n"
        "  idc grid --input photo.jpg --weights-x 1,2,1 --keep 0,0 --output-dir ./cells/\n"
        "\n"
        "Common errors:\n"
        "  Exit code 1: missing required / mutual-exclusion conflict / r,c out of range / illegal format\n"
        "  Exit code 3: input image error\n"
        "  Exit code 4: output failed\n"},
    {"help::kTextConfigIntro",
        "idc config - config-file job\n"
        "\n"
        "Geometry fields in the config and the --rect override accept percentage strings (e.g. \"50%\", converted against the canvas source width / height).\n"
        "\n"
        "Config:\n"},
    {"help::kTextConfigLayers",
        "\n"
        "Multi-layer canvas (layers section, optional): carries only overlay layers (not the base image), stacked on top of the base by zOrder; before running the engine\n"
        "      flattens them into a source-sized canvas (order: fully transparent base -> base image -> overlay layers; the engine still receives just one flattened canvas).\n"
        "      The base image comes from --input / --size / the config generate section (combinable, not mutually exclusive); when none is present the overlay layers are laid directly on a fully transparent base. Each layer source is one of three:\n"
        "        · recipe (pattern parameters isomorphic to the generate section, byte-for-byte reproducible, no bitmap stored);\n"
        "        · file  (stores only a relative name, resolved against the config directory or <configname>.assets/; decode injected; escaping paths (absolute / ..) rejected, a missing file degrades to a placeholder);\n"
        "        · empty placeholder (missing file / unspecified, skipped when compositing).\n"
        "      Each layer may carry transform (canvas point inCanvasAnchor + layer pivot layerPivot + free x/y + target width/height) and per-layer preprocess;\n"
        "      pixels outside the canvas are naturally clipped on write (discarded on export).\n"
        "\n"
        "Base image (generate section, optional): the base image = one layer record isomorphic to the layers, only its role is pinned to the bottom, undraggable / its placement unchangeable.\n"
        "      Besides the pattern fields (kind/background/foreground/..., isomorphic to the generate section's pattern params), an image base attached to a new canvas may also carry\n"
        "      fit + imageAnchor/imagePivot + x/y + backdrop (layer-box backing pattern) placement keys (the placement box size always equals the canvas, derived from source and not stored separately), so the base placement round-trips with the config and is reproducible\n"
        "      (the config stores only placement, not file paths; the base bitmap is always supplied at runtime by --input, so the same config can be reused across different inputs for batching).\n"
        "      --input / open image = an identity base with no placement, canvas size = image size; external usage is the same.\n"
        "\n"
        "Preprocessing (preprocess array, optional): six kinds of steps gray/split/invert/rotate/flip/resize (opacity is not a preprocess step; it is carried by the layer compositing parameter).\n"
        "\n"
        "Layer opacity (transform.opacity, optional): the degree of whole-layer fade-out (0..255, default 255 = fully opaque, 0 = fully transparent).\n"
        "      This parameter acts on the whole layer proportionally to opacity at export compositing time and does not modify the pixels themselves, so it also works on grayscale images.\n"
    },
    {"help::kTextConfigTail",
        "\n"
        "Examples:\n"
        "  idc config --load my-config.json --input photo.jpg --output result.png\n"
        "  idc extract --input photo.jpg --rect 100,100,300,250 --output out.png --save-config out.json\n"
        "  # Multi-layer canvas config (only layers overlays) run directly: with no base image the overlays are laid on a fully transparent base\n"
        "  idc config --load canvas.json --output out.png\n"
        "  # Base image + overlay layers combined: --input provides the base, the config layers section stacks on it (flatten order: fully transparent base -> base -> overlays)\n"
        "  idc config --load canvas.json --input base.jpg --output out.png\n"
        "  # Save the config and carry File-layer assets into <configname>.assets/ (for distribution / reproduction alongside the config)\n"
        "  idc config --load canvas.json --save-config bundle.json --assets-dir\n"
        "\n"
        "Common errors:\n"
        "  Exit code 1: missing --load / missing output / config parse failed; --hband/--vband combined with --save-config; with --assets-dir a File-layer name is an absolute path / contains .. (escaping)\n"
        "  Exit code 2: canvas flattening produced an empty image (illegal source size) / engine business failure\n"
        "  Exit code 3: the config file or input image does not exist / cannot be read; a layer asset path escapes; with --assets-dir a File-layer source image is missing\n"
        "  Exit code 4: output / write failed; --assets-dir failed to copy assets or create the directory\n"},
    {"help::kTextNewIntro",
        "idc new - new canvas (procedurally generate a canvas and write it to disk)\n"
        "\n"},
    {"help::kTextNewTail",
        "\n"
        "Examples:\n"
        "  idc new --size 800x600 --bg \"#FFFFFFFF\" --output white.png\n"
        "  idc new --size 1024x1024 --pattern checkerboard --bg \"#FFFFFFFF\" --fg \"#FF000000\" --period 64 --output board.png\n"
        "  idc new --size 1920x1080 --pattern radial-gradient --bg \"#FF000000\" --fg \"#FFFFFFFF\" --output glow.png\n"
        "  # Diagonal stripes (--orientation diagonal-down / diagonal-up) and full-hue rainbow\n"
        "  idc new --size 512x512 --pattern stripes --orientation diagonal-down --fg \"#FF000000\" --period 24 --output diag.png\n"
        "  idc new --size 512x256 --pattern hue-gradient --period 128 --output rainbow.png\n"
        "  # Concentric squares (--orientation vertical = square rings)\n"
        "  idc new --size 512x512 --pattern concentric --orientation vertical --fg \"#FF000000\" --period 32 --line-width 2 --output rings.png\n"
        "  # Deterministic noise (the same --seed always yields the same image, for pixel-by-pixel comparison)\n"
        "  idc new --size 256x256 --pattern noise --seed 1234 --noise full-color --output noise.png\n"
        "  # Transparent base: background alpha=00 gives a fully transparent canvas\n"
        "  idc new --size 512x512 --bg \"#00FFFFFF\" --output transparent.png\n"
        "  # Exporting a transparent canvas to JPEG: give an explicit flatten color to avoid a silent black background\n"
        "  idc new --size 512x512 --bg \"#00FFFFFF\" --format jpeg --pad-color \"#FFFFFFFF\" --output flat.jpg\n"
        "  # Save the generation recipe as a config (dry-run; carries a keep-the-whole-canvas cut section, so config --load reproduces the canvas directly)\n"
        "  idc new --size 200x150 --pattern solid --bg \"#FFFF0000\" --save-config recipe.json\n"
        "  # Generate a canvas while burning in an annotation and stacking a layer (layers first, annotations always on top)\n"
        "  idc new --size 200x150 --bg \"#FFFFFFFF\" --annotation '{\"type\":\"circle\",\"x1\":100,\"y1\":75,\"x2\":130,\"y2\":75,\"color\":\"#FF00FF00\",\"strokeWidth\":3}' --layer '{\"source\":{\"recipe\":{\"kind\":\"stripes\",\"foreground\":\"#40000000\"}},\"transform\":{\"width\":200,\"height\":150}}' --output done.png\n"
        "\n"
        "Common errors:\n"
        "  Exit code 1: missing --size; missing --output when not a dry-run; --size not WxH positive integers / one side over 65535 / width x height over 64,000,000; illegal format for --pattern / --orientation / --bg / --fg / --pad-color / --period / --seed / --noise etc.; --quality not an integer 0~100; --input given (new does not accept it); illegal --annotation / --layer arguments; with --assets-dir bundling, an escaping File-layer name\n"
        "  Exit code 3: a File-layer asset path escapes; with --strict a File source image is missing; with --assets-dir bundling a File source image is missing\n"
        "  Exit code 4: writing the output failed; --assets-dir failed to copy assets or create the directory\n"},
};

} // namespace

namespace i18n {

void installHelpCatalog() {
    core::i18n::installLanguage(core::kLangCodeEn, core::makeI18nTable(kEntriesEnHelp));
}

} // namespace i18n
} // namespace idc::cli
