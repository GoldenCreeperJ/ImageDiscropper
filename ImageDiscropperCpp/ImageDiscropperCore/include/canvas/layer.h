// ============================================================================
// 文件：include/canvas/layer.h
// 作用：多层画布的数据模型与像素合成（纯逻辑、零 GUI 依赖）——ImageLayer（叠加层的来源 +
//       落位变换 + 逐层声明式预处理）与 composeCanvas（先落**底图 base**、再按 zOrder 逐层物化 →
//       缩放 → source-over 落位，写入画布尺寸缓冲，越界自然裁剪）。叠加层集合直接以
//       std::vector<ImageLayer> 承载（无 LayerStack 包装；画布恒铺透明底，无画布级背景概念）。
// 分块依据：Core 只承载「无头侧也成立」的数据与像素能力；每层的**交互会话态**（选中/编辑/
//       base_ 缓存编排）归 GUI 的 PreprocessSession，Core 绝不引用 GUI 类型（分层纪律）。
// 说明：图片只有一个概念——本模块的 ImageLayer「图片图层」（可指文件 / 配方 / 空占位）；
//       旧 generate::PatternKind::Image「贴图图案」已彻底移除。适应模式（ImageFit）与两级落位
//       （图层框→底图 = transform，源图→图层框 = image* 内层字段）已升为**持久字段**，使 config
//       仅凭 fileName（原始源）+ 内/外落位 + 图层框底色即可完整复现，reload 一致。
//       **底图（画布基座）不是图层**：它走既有输入生态位（file＝外部输入图 / recipe＝generate 段），
//       由调用方作为 base 传入 composeCanvas，恒铺满画布、无 transform、不入 layers、不可拖拽；
//       layers 只装「叠加其上的图片图层」。引擎仍只收一张展平画布图：composeCanvas 的产物即喂给
//       runEngine 的工作图，引擎零改动。
// ============================================================================
#pragma once

#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "core/color.h"
#include "core/image.h"
#include "generate/pattern.h"
#include "preprocess/preprocess_pipeline.h"

namespace idc::canvas {

// 图层来源类型：配方（可序列化 PatternParams）/ 文件（相对 assets 名 + 上层解码像素）/ 空占位。
enum class LayerSourceKind {
    Empty,   // 空占位（缺文件降级 / 新建未指定）——物化为空图，合成时跳过
    Recipe,  // 配方来源：由 generate::makePattern 依 recipe + 目标尺寸现算像素（可逐字节复现）
    File,    // 文件来源：decodedPixels 由上层解码注入（配方不外传位图）
};

// ---------------------------------------------------------------------------
// ImageFit：内层源图→图层框（boxW×boxH）的适应方式（仅 hasImagePlacement 为真时参与合成）。
//   Crop    —— 源图按原生尺寸贴入图层框、按内层锚点/枢轴/偏移落位，超出图层框部分自然裁掉。
//   Stretch —— 源图重采样拉伸铺满图层框（忽略宽高比）。
//   Native  —— 图层框尺寸已等于源图原生尺寸（调用方保证），不缩放、不变形。
// ---------------------------------------------------------------------------
enum class ImageFit { Crop, Stretch, Native };

// ---------------------------------------------------------------------------
// ImageTransform：图层落位变换（合成期参数，拖动/改尺寸/改不透明度只重组、不重跑逐层预处理）。
//   画布锚点 inCanvasAnchor 定「画布上的对齐基准点」；图片枢轴 layerPivot 定「图层自身
//   对齐到该基准点的部位」；(x,y) 为自由偏移（像素）。宽高 > 0 时把图层缩放到该尺寸，
//   否则用源图 / 配方的原生尺寸。opacity 为合成期 alpha 乘子（0..255，255＝不透明不变）：
//   它是图层合成概念而非像素变换，故灰度图层（GRAY 无 alpha）的不透明度也能生效。
// ---------------------------------------------------------------------------
struct ImageTransform {
    generate::ImageAnchor inCanvasAnchor{generate::ImageAnchor::Center};  // 画布锚点（对齐到画布的哪个位置）
    generate::ImageAnchor layerPivot{generate::ImageAnchor::Center};      // 图片枢轴（图层对齐到锚点的部位）
    int x{0};        // 自由位置偏移（像素，可正可负；叠加在锚点/枢轴结果之上）
    int y{0};
    int width{0};    // 目标宽（<=0 → 用原生尺寸，不缩放）
    int height{0};   // 目标高（<=0 → 用原生尺寸，不缩放）
    int opacity{255}; // 合成期 alpha 乘子（0..255；逐像素 a' = (a·opacity + 127) / 255，255＝不透明）

    bool operator==(const ImageTransform& o) const {
        return inCanvasAnchor == o.inCanvasAnchor && layerPivot == o.layerPivot &&
               x == o.x && y == o.y && width == o.width && height == o.height && opacity == o.opacity;
    }
};

// ---------------------------------------------------------------------------
// ImageLayer：单条图层记录（来源 + 落位 + z 序 + 逐层预处理）。
//   decodedPixels 仅在 sourceKind==File 且上层已解码时非空；配方 / 占位时为空。
//   id 为跨会话的稳定标识（GUI 引用 / 缓存键），Core 只存取不解释。
// ---------------------------------------------------------------------------
struct ImageLayer {
    int id{0};
    LayerSourceKind sourceKind{LayerSourceKind::Empty};
    std::optional<generate::PatternParams> recipe;  // 配方来源（provenance，可序列化；不含位图）
    std::string fileName;                            // 文件来源（相对 assets 目录的文件名）
    core::Image decodedPixels;                       // 上层解码注入的源像素（仅 File 用；不入配置）
    ImageTransform transform;
    // ---- 内层：源图 → 图层框（仅 hasImagePlacement 为真时生效；供对话框附加图片图层两级落位的 config 往返）----
    //   composeCanvas 先将 decodedPixels（原始源）按 imageFit + 内层锚点/枢轴/偏移合成进 boxWidth×boxHeight
    //   的图层框（以 boxBackdrop 图案为底、越框裁掉），再将图层框按外层 transform 落到画布。
    bool hasImagePlacement{false};
    ImageFit imageFit{ImageFit::Crop};
    int boxWidth{0};                                        // 图层框宽（＝对话框顶部宽）
    int boxHeight{0};                                       // 图层框高（＝对话框顶部高）
    generate::ImageAnchor imageAnchor{generate::ImageAnchor::Center};   // 内层源图锚点（对齐到图层框的哪个位置）
    generate::ImageAnchor imagePivot{generate::ImageAnchor::Center};    // 内层源图枢轴（源图对齐到锚点的部位）
    int imageX{0};                                          // 内层自由偏移 X
    int imageY{0};                                          // 内层自由偏移 Y
    std::optional<generate::PatternParams> boxBackdrop;     // 图层框底色/图案（空＝透明底；solid 亦可用本字段）
    int zOrder{0};                                   // 叠放次序：0＝最底、越大越靠顶
    preprocess::PreprocessPipeline preprocess;       // 逐层声明式预处理（进入合成前物化）
};

// ---------------------------------------------------------------------------
// materializeLayer：物化单层的「预处理后位图」（配方→makePattern / 文件→解码像素 / 空→空图，
//   再跑该层 preprocess.applyExec）。不含缩放与落位（那是 composeCanvas 的合成期职责），
//   故 GUI 可按 (layerId, preprocess 签名) 缓存本结果，改 transform 时不重跑预处理。
//   不读取磁盘：File 源依赖 decodedPixels 已被上层注入；未注入则视为空图（占位）。
// ---------------------------------------------------------------------------
core::Image materializeLayer(const ImageLayer& layer);

// ---------------------------------------------------------------------------
// composeCanvas：产出 width×height 的 RGBA 画布图。画布先铺满全透明底，再落**底图 base**
//   （非空则 source-over 铺于 (0,0)、按画布尺寸自然裁剪/留空），最后按 zOrder 升序逐层叠加
//   layers（std::vector<ImageLayer>，只装叠加层、**不含底图**）：物化 → 按 transform 缩放 → 逐像素施 transform.opacity 作 alpha 乘子
//   → 依锚点/枢轴/自由偏移定位 → source-over 落位；越出画布的像素写入 w×h 缓冲时自然裁剪。
//   base 为空（默认）＝无底图，仅透明底 + 叠加层。整体显隐由调用方（GUI）以「传空 layers」实现，Core 不持显隐态。
//   materialized（可选）：预物化位图表（key＝layer.id，value＝materializeLayer 的**缩放前**产物）；
//   命中则直接取用、跳过本层 materializeLayer（GUI 会话层据此缓存避免拖动时重跑全层预处理），未命中回退现算。
//   非法尺寸（<=0 或超护栏）→ 返回空图（与 makePattern 同口径）。
// ---------------------------------------------------------------------------
core::Image composeCanvas(const std::vector<ImageLayer>& layers, int width, int height, const core::Image& base = {},
                          const std::unordered_map<int, core::Image>* materialized = nullptr);

// ---------------------------------------------------------------------------
// applyAlpha：对 **RGBA** 图逐像素施 alpha 乘子 a' = (a·opacity + 127) / 255（整数四舍五入，
//   与 composeCanvas 合成期逐层不透明度同口径）。供合成边界「整体淡出底图」复用同一公式，
//   避免调用方各写一份造成漂移。opacity >= 255 或空图 → 恒等早退（不改像素）；非 RGBA（RGB/GRAY
//   无 alpha 通道）→ 无操作（不改变像素格式，避免把灰度/彩色底误转为带 alpha 语义）。
// ---------------------------------------------------------------------------
void applyAlpha(core::Image& image, std::uint8_t opacity);

// ---------------------------------------------------------------------------
// composeBase：把「底图」（同构 canvas::ImageLayer，钉住画布最底）展平为 width×height 像素。
//   纯图案底图（sourceKind==Recipe 且无内层落位）：按画布(source)尺寸现算图案，与旧「generate」段
//   口径逐字节一致（不走 materializeLayer 的 transform 尺寸，避免回归）；如有逐层预处理则同口径 applyExec。
//   文件底图 / 带内层落位：当作单元素 layers、空底、box=画布、transform 恒等，转调 composeCanvas 复用两段合成。
//   **不烘焙底图整体透明度**：placement 路转调 composeCanvas 前把 transform.opacity 归一 255（合成期恒不施乘），
//   Recipe 路 makePattern 亦不施——底图整体不透明度一律由调用方在合成边界经 applyAlpha 施加（单一应用点，防双淡）。
//   File 源依赖 decodedPixels 已由上层注入（未注入→空图）。
// ---------------------------------------------------------------------------
core::Image composeBase(const ImageLayer& base, int width, int height,
                        const std::unordered_map<int, core::Image>* materialized = nullptr);

// ---------------------------------------------------------------------------
// LayerPlacement：单层在画布坐标系（左上原点）下的落位矩形。供上层（如 GUI 画布）做
//   图片图层拾取（包围盒命中、重叠取最顶）与拖拽定位——与 composeCanvas 完全同口径
//   （物化 → 合成期缩放 → 锚点/枢轴/自由偏移定位），是落位几何的单一真相源。
//   x/y 可负、矩形可越出画布（未裁剪，与合成期裁剪前的落位包围盒一致）。
// ---------------------------------------------------------------------------
struct LayerPlacement {
    int x{0};       // 落位左上 x（画布坐标，可负）
    int y{0};       // 落位左上 y（画布坐标，可负）
    int width{0};   // 落位后宽
    int height{0};  // 落位后高
};

// ---------------------------------------------------------------------------
// layerPlacement：算单层在 (canvasW×canvasH) 画布上的落位矩形。无可呈现位图（空/缺/降级
//   占位，或缩放后尺寸非正）时返回 std::nullopt（合成与拾取均跳过该层）。
// ---------------------------------------------------------------------------
std::optional<LayerPlacement> layerPlacement(const ImageLayer& layer, int canvasWidth, int canvasHeight);

} // namespace idc::canvas
