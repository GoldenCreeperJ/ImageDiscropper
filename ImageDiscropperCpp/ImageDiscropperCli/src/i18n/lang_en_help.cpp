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
    {"help::kDescHelpThis", "Show this help"},
    {"help::kDescVerbose", "Write detailed logs to stderr"},
    {"help::kDescQuiet", "Silent mode, print errors only"},
    {"help::kDescLang", "Set the language of help and error text (zh-CN / en); when unset, falls back to IDC_LANG, LC_ALL, LANG"},
    {"help::kDescTopConfig", "Read all parameters from a JSON config file (the command's geometry arguments are ignored)"},
    {"help::kDescTopSaveConfig", "Serialize the current parameters to a JSON config file (dry-run, no job executed)"},
    {"help::kDescTopBundleAssets", "When used with --save-config, copy the config's File-layer source images by relative name into <configname>.assets/ (must be explicitly enabled; not the default)"},
    {"help::kDescInput", "Input image path (choose one with --size)"},
    {"help::kDescRectKeep", "Rectangle region (keep the center rectangle)"},
    {"help::kDescHbandKeep", "Horizontal band (keep the whole band where y is in [y1,y2))"},
    {"help::kDescVbandKeep", "Vertical band (keep the whole band where x is in [x1,x2))"},
    {"help::kDescRectErase", "[repeatable] once = single-rectangle cross cut; multiple = multi-rect union remove"},
    {"help::kDescHbandErase", "Delete the whole horizontal band where y is in [y1,y2)"},
    {"help::kDescVbandErase", "Delete the whole vertical band where x is in [x1,x2)"},
    {"help::kDescOutputRequired", "[required] output image path"},
    {"help::kDescFormatInfer", "Output format png/jpeg/webp/bmp (inferred from the --output extension by default)"},
    {"help::kDescFormatPngDefault", "Output format png/jpeg/webp/bmp (default png; when merging, the --output extension takes priority)"},
    {"help::kDescFormatPlain", "Output format png/jpeg/webp/bmp"},
    {"help::kDescQuality", "Lossy quality 0~100 (default 90; only jpeg/webp take effect, png/bmp ignore it)"},
    {"help::kDescNamingDefault", "Separate naming template, default {name}_{index:03d}"},
    {"help::kDescNamingPlain", "Separate naming template"},
    {"help::kDescMerge", "collapse only; default is separate export (rearrange is exclusive to the grid command)"},
    {"help::kDescOutputMergeErase", "Merged output path (required with --merge collapse)"},
    {"help::kDescOutputMergeCompose", "Merged output path (required with --compose)"},
    {"help::kDescOutputDirErase", "Separate output directory (required for the default separate export; created automatically if missing)"},
    {"help::kDescOutputDirGrid", "Separate output directory (required for the default separate export)"},
    {"help::kDescOutputConfigMerged", "Merged output path (used when the config has emit.mode=merged)"},
    {"help::kDescOutputDirConfigSeparate", "Separate output directory (used when the config has emit.mode=separate)"},
    {"help::kDescAnnoInline", "[repeatable] inline one annotation object, or an array of annotation objects"},
    {"help::kDescAnnoFile", "[repeatable] read from a JSON file whose root is an annotation array"},
    {"help::kDescGrid", "[required] origin + cell size (the origin may be negative, cw/ch must be positive)"},
    {"help::kDescKeepCell", "[repeatable] keep the specified cell"},
    {"help::kDescRemoveCell", "[repeatable] remove the specified cell"},
    {"help::kDescSort", "row-major (default) / column-major / custom"},
    {"help::kDescDecorate", "none (default) / reverse / snake / reverse-snake / snake-reverse (not available with --sort custom)"},
    {"help::kDescOrderCustom", "Custom sequence (required with --sort custom; defines both the kept cells and their order)"},
    {"help::kDescMargin", "discard (default) / keep-partial / pad"},
    {"help::kDescCompose", "Merge into a single image (rearrange); default is separate export"},
    {"help::kDescCanvas", "Rearrange canvas cell columns x rows, e.g. 2x2 (required with --compose)"},
    {"help::kDescMergeSort", "Rearrange fill order row-major (default) / column-major (orthogonal to the --sort selection order; custom not supported)"},
    {"help::kDescMergeDecorate", "Rearrange fill decoration none (default) / reverse / snake / reverse-snake / snake-reverse"},
    {"help::kDescPadColorDefault", "#RRGGBB / #AARRGGBB, transparent by default"},
    {"help::kDescPadColorFlatten", "Background color #RRGGBB/#AARRGGBB used to flatten transparent pixels in lossy formats (jpeg/bmp) (default transparent -> black; specify it explicitly when exporting a transparent canvas to jpeg)"},
    {"help::kDescSizeGen", "Generated canvas size (positive integer pixels; each side <= 65535 and width x height <= 64,000,000 ~= 256MB; replaces --input)"},
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
    {"help::kDescNewOutput", "Path to write the generated image (may be omitted for a --save-config dry-run)"},
    {"help::kDescNewSaveConfig", "dry-run: writes no pixels; serializes this generation recipe (source size + generate section + emit) into a config so config --load can reproduce the canvas (this path does not require --output)"},
    {"help::kDescLoad", "[required] read all parameters from a JSON config file"},
    {"help::kDescConfigSaveAs", "Save the loaded config as JSON (transform / validate; dry-run, no job executed)"},
    {"help::kDescConfigBundle", "With --save-config: copy the File-layer source images in layers by relative name into <configname>.assets/ (escaping paths are rejected; missing sources raise an error)"},
    {"help::kDescConfigInput", "Base image path (choose one with --size; combinable with the config's layers section, layers stack on top of this base; both may be omitted when the config has a generate section)"},
    {"help::kLeadGenOverride", "Command-line generation options override this run's input source; if there is no --input/--size and the config has a generate section, the canvas is composed from the config."},
    {"help::kLeadAnnoAppend", "Command-line annotations are appended after the --load config's own annotations section."},
    // —— help 散文块 kText*（逐字搬移：换行、缩进、示例与末尾换行都是 --help 输出的一部分；
    //    为规避目录测试逐物理行取键，示例里的 {\"...\"} 一律用相邻字符串字面量，物理行首恒以 \" 起）——
    {"help::kTextTopIntro",
        "idc — image region extraction / reverse removal / grid splitting tool (command line)\n"
        "\n"
        "The three-tier modes share a single Grid-Selection-Emit engine\n"
        "(source image -> cut lines -> induced grid -> selection -> polarity -> arranged export); the modes are just parameter presets:\n"
        "\n"
        "Usage:\n"
        "  idc <command> [options]\n"
        "  idc --help | --version\n"
        "\n"
        "Subcommands:\n"
        "  extract   standard extraction: keep rectangle / horizontal band / vertical band, output a single image\n"
        "  erase     reverse removal: delete cross band / horizontal band / vertical band / multi-rect union, separate or merged export\n"
        "  grid      grid splitting: tile a grid by an origin and cell size, then separate or rearrange-merge the selected cells\n"
        "  new       New canvas: generate a canvas by size / background(alpha) / procedural pattern and write it to disk\n"
        "  config    Run one complete job from a JSON config file (with optional annotations / multi-layer canvas)\n"
        "\n"
        "Global options:\n"
    },
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
        "\n"
        "To see a subcommand's full options: idc <command> --help\n"
    },
    {"help::kTextAnnoHead", "\nAnnotations (optional, burned into the working image before cutting; the coordinate origin = the working-image size after preprocessing):\n"},
    {"help::kTextGenHead", "\nGenerated source (optional): when --input is not given, use --size (optionally with --pattern, etc.) to procedurally generate a canvas as the input; --input and --size are mutually exclusive - pick one.\n"},
    {"help::kTextSecCutThree", "\nCut lines (choose one of --rect / --hband / --vband):\n"},
    {"help::kTextSecOutput", "\nOutput:\n"},
    {"help::kTextSecExport", "\nExport:\n"},
    {"help::kTextSecCommonBlank", "\n\nGeneral:\n"},
    {"help::kTextSecCommon", "\nGeneral:\n"},
    {"help::kTextSecGrid", "\nGrid:\n"},
    {"help::kTextSecSelect", "\nSelection (choose one of --keep and --remove):\n"},
    {"help::kTextSecSortDecorate", "\nSorting and decoration:\n"},
    {"help::kTextSecMargin", "\nRemainder:\n"},
    {"help::kTextSecCompose", "\nMerge and rearrange:\n"},
    {"help::kTextSecIo", "\nInput and output:\n"},
    {"help::kTextSecSizeBg", "\nSize and background:\n"},
    {"help::kTextSecPattern", "\nPattern:\n"},
    {"help::kTextPatternMatrix",
        "\n"
        "Pattern x parameter applicability matrix (an unlisted parameter has no effect for that pattern, consistent with the engine's actual behavior):\n"
        "    solid            entire canvas = --bg; ignores --fg/--orientation/--period/--line-width/--dot-radius\n"
        "    linear-gradient  --bg <-> --fg linear interpolation along --orientation (horizontal varies with x, vertical with y); ignores --period/--line-width/--dot-radius\n"
        "    radial-gradient  --bg (edge) <-> --fg (center) radial interpolation; --orientation has no effect, ignores --period/--line-width/--dot-radius\n"
        "    checkerboard     --fg square edge length = --period (--orientation has no effect); ignores --line-width/--dot-radius\n"
        "    stripes          --fg stripe width = --period; --orientation horizontal=vertical bars(over x)/vertical=horizontal bars(over y)/diagonal-down=down-right(along x-y)/diagonal-up=up-right(along x+y); ignores --line-width/--dot-radius\n"
        "    grid-lines       --fg grid lines: spacing = --period, line width = --line-width; --orientation has no effect, ignores --dot-radius\n"
        "    dot-grid         --fg dots: dot spacing = --period, radius = --dot-radius; --orientation has no effect, ignores --line-width\n"
        "    concentric       --fg concentric rings: ring spacing = --period, ring width = --line-width; --orientation horizontal=circles(Euclidean)/vertical=square rings(Chebyshev maze), ignores --dot-radius\n"
        "    radial-rays      --fg radial sectors: density = --period (smaller is denser); --orientation has no effect, ignores --line-width/--dot-radius\n"
        "    hue-gradient     full-hue rainbow scanned along --orientation (H 0->1, full saturation, full value, opaque), --period = pixel width of one rainbow; ignores --bg/--fg/--line-width/--dot-radius\n"
        "    noise            deterministic pseudo-random noise: --seed fixes the seed (same seed + same size always yields the same image, for pixel-by-pixel comparison), --noise picks the palette full-color(default)/grayscale/two-color; two-color picks randomly between --bg <-> --fg, the rest ignores --bg/--fg/--orientation/--period/--line-width/--dot-radius\n"
        "  alpha: the #AARRGGBB alpha of --bg/--fg participates in interpolation and is written to disk; PNG/WebP keep transparency, JPEG/BMP flatten by the fill color on export.\n"
        "  Note: the \"image\" paste pattern is not among the pattern types (--pattern image / --pattern-image / --image-anchor / --image-scale are not accepted);\n"
        "      an image as an \"additional layer\" can only be passed via the config file's layers section (source.kind=\"file\"), composited onto the canvas by config --load.\n"
    },
    {"help::kTextAnnoFields",
        "  Fields dispatch geometry by type; the styling applies to all types:\n"
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
        "    · color/fillColor must be a valid #RRGGBB or #AARRGGBB; strokeWidth / zOrder must be integers (a decimal/string zOrder errors; negative is valid). A JSON syntax error or a root that is not an object/array also gives exit code 1.\n"
        "  Single-item examples:\n"
        "    {\"type\":\"line\",\"points\":[[10,10],[200,150]],\"color\":\"#FF0000\",\"strokeWidth\":4}\n"
        "    {\"type\":\"rectangle\",\"x1\":10,\"y1\":10,\"x2\":120,\"y2\":80,\"fill\":true,\"fillColor\":\"#80FF0000\",\"strokeWidth\":2}\n"
        "    {\"type\":\"rhombus\",\"x1\":60,\"y1\":50,\"x2\":110,\"y2\":20,\"color\":\"#FF0000\"}\n"
        "    {\"type\":\"circle\",\"x1\":70,\"y1\":70,\"x2\":100,\"y2\":70,\"color\":\"#00FF00\"}\n"
        "    {\"type\":\"text\",\"x\":20,\"y\":30,\"text\":\"HELLO\",\"fontSize\":18,\"color\":\"#0000FF\"}\n"
        "    {\"type\":\"path\",\"segments\":[{\"t\":\"M\",\"coords\":[0,0]},{\"t\":\"L\",\"coords\":[50,50]},{\"t\":\"Z\",\"coords\":[]}],\"color\":\"#00FFFF\"}\n"
        "  Multiple: repeat --annotation once each, or pass an array at once [{\"type\":\"line\",...},{\"type\":\"circle\",...}]; you can also use --annotations file to batch-read from a file.\n"
        "  Export: --save-config also writes the command-line annotations into the config file's annotations section (round-trips without loss)."},
    {"help::kTextExtractIntro",
        "idc extract - standard extraction mode\n"
        "\n"
        "Semantics: polarity is always keep, output is always a single image. The rectangle keeps the center cell; a horizontal / vertical line keeps the whole band.\n"
        "      Cut lines span the whole image (axiom); the kept regions are collapsed and stitched into a single image.\n"
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
        "Semantics: polarity is always remove, \"delete along the line\" - pull out the middle seam and butt the two sides together, shrinking the size (W-dx x H-dy).\n"
        "      A single rectangle -> cross cut (keeps the four corners); --rect repeated -> multi-rectangle union removal;\n"
        "      a horizontal / vertical line -> delete the whole band. Default is separate export (one image per kept block); --merge collapse merges into a single image.\n"
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
        "Semantics: use the origin (x0,y0) as the phase anchor and the cell size (cw,ch) as the period to generate cut lines spanning the whole image and induce\n"
        "      the grid; the row / column count is derived automatically from the image bounds (not a parameter). --keep / --remove select cells one by one,\n"
        "      which can be sorted then exported separately or rearrange-merged onto a ColxRow canvas.\n"
        "\n"
        "Input:\n"},
    {"help::kTextGridTail",
        "\n"
        "Constraints:\n"
        "  - --keep and --remove are mutually exclusive; --compose and --output-dir are mutually exclusive.\n"
        "  - --decorate is not available with --sort custom; --merge-sort does not support custom (rearrange fill is only\n"
        "    row / column major; use --sort custom for a custom block order).\n"
        "  - With --compose, --canvas cols/rows are required and must be positive.\n"
        "  - The engine applies snake first, then reverses as a whole (fixed order), so reverse-snake and snake-reverse give the same result.\n"
        "\n"
        "Examples:\n"
        "  idc grid --input photo.jpg --grid 100,100,200,150 --keep 0,0 --keep 0,2 --keep 2,0 --keep 2,2 --compose --canvas 2x2 --output result.png\n"
        "  idc grid --input photo.jpg --grid 0,0,100,100 --remove 1,1 --output-dir ./cells/\n"
        "  idc grid --input photo.jpg --grid 0,0,100,100 --sort custom --order 0,0;1,1;2,2 --compose --canvas 3x1 --output diag.png\n"
        "\n"
        "Common errors:\n"
        "  Exit code 1: missing required / mutual-exclusion conflict / r,c out of range / illegal format\n"
        "  Exit code 3: input image error\n"
        "  Exit code 4: output failed\n"},
    {"help::kTextConfigIntro",
        "idc config - config-file job\n"
        "\n"
        "Semantics: read all parameters of one complete job from a JSON config file (source/generate/layers/preprocess/cut/order/emit/select),\n"
        "      reusing the unified JSON parser (the command-line side does not re-implement it). A config file generally contains no image path, so running it usually\n"
        "      needs --input or --size to provide the **base image**;\n"
        "      the base image (canvas base, input niche) comes from one of: --input reads a file, --size generates on the command line, or the config generate section generates;\n"
        "      if none of the three is present but the config has a layers section, the overlay layers are laid directly on a fully transparent base and can still run.\n"
        "\n"
        "Config:\n"},
    {"help::kTextConfigLayers",
        "\n"
        "Multi-layer canvas (layers section, optional): carries only **overlay layers** (not the base image), stacked on top of the base by zOrder; before running the engine\n"
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
        "      fit + boxWidth/boxHeight + imageAnchor/imagePivot + x/y + backdrop (layer-box backing pattern) placement keys, so the base placement round-trips with the config and is reproducible\n"
        "      (**the config stores only placement, not file paths**; the base bitmap is always supplied at runtime by --input, so the same config can be reused across different inputs for batching).\n"
        "      --input / open image = an identity base with no placement, canvas size = image size; external usage is the same.\n"
        "\n"
        "Preprocessing (preprocess array, optional): six kinds of steps gray/split/invert/rotate/flip/resize (opacity is not a preprocess step; it is carried by the layer compositing parameter).\n"
        "\n"
        "Layer opacity (transform.opacity, optional): the degree of whole-layer fade-out (0..255, default 255 = fully opaque, 0 = fully transparent).\n"
        "      This parameter acts on the whole layer proportionally to opacity at export compositing time and does not modify the pixels themselves, so it also works on grayscale images; an opacity step written in preprocess in old configs is automatically converted to this layer's transform.opacity on load.\n"
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
        "  idc config --load canvas.json --save-config bundle.json --bundle-assets\n"
        "\n"
        "Common errors:\n"
        "  Exit code 1: missing --load / missing output / config parse failed; --input and --size given together; with --bundle-assets a File-layer name is an absolute path / contains .. (escaping)\n"
        "  Exit code 2: canvas flattening produced an empty image (illegal source size) / engine business failure\n"
        "  Exit code 3: the config file or input image does not exist / cannot be read; a layer asset path escapes; with --bundle-assets a File-layer source image is missing\n"
        "  Exit code 4: output / write failed; --bundle-assets failed to copy assets or create the directory\n"},
    {"help::kTextNewIntro",
        "idc new - new canvas (procedurally generate a canvas and write it to disk)\n"
        "\n"
        "Semantics: by size + background (with alpha) + pattern type and parameters, the pattern factory generates a brand-new RGBA canvas,\n"
        "      written to --output. Equivalent to the GUI \"New Canvas\" dialog; it does not involve cutting (to cut a generated image, use the --size generated source of extract/erase/grid instead).\n"},
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
        "  # Save the generation recipe as a config (dry-run), so config --load can reproduce the canvas from the generate section\n"
        "  idc new --size 200x150 --pattern solid --bg \"#FFFF0000\" --save-config recipe.json\n"
        "\n"
        "Common errors:\n"
        "  Exit code 1: missing --size; missing --output when not a dry-run; --size not WxH positive integers / one side over 65535 / width x height over 64,000,000; illegal format for --pattern / --orientation / --bg / --fg / --pad-color / --period / --seed / --noise etc.; --quality not an integer 0~100; --input given (new does not accept it)\n"
        "  Exit code 4: writing the output failed\n"},
};

} // namespace

namespace i18n {

void installHelpCatalog() {
    core::i18n::installLanguage(core::kLangCodeEn, core::makeI18nTable(kEntriesEnHelp));
}

} // namespace i18n
} // namespace idc::cli
