// ============================================================================
// 文件：include/canvas/layer.h
// 作用：多层画布的数据模型与像素合成（纯逻辑、零 GUI 依赖）——ImageLayer（图层框节点：
//       图案元素 + 图片元素 + 节点外观）与 composeCanvas（先落**底图 base**、再按 zOrder 逐层物化
//       （组装+缩放+预处理）→ source-over 落位，写入画布尺寸缓冲，越界自然裁剪）。叠加层集合直接以
//       std::vector<ImageLayer> 承载（无 LayerStack 包装；画布恒铺透明底，无画布级背景概念）。
// 分块依据：Core 只承载「无头侧也成立」的数据与像素能力；每层的**交互会话态**（选中/编辑/
//       base_ 缓存编排）归 GUI 的 PreprocessSession，Core 绝不引用 GUI 类型（分层纪律）。
// 说明：SPEC §2.2a「合成模型总纲」——一切皆合成的两级树。图层 = 图层框节点：
//       · 图案元素 backdrop（可选：图案配方 + 元素外观 backdropPreprocess/backdropOpacity；缺省透明底）；
//       · 图片元素 image（可选：文件源 + 内层落位 + 元素外观 imagePreprocess/imageOpacity；缺省 = 纯图案层）；
//       · 节点外观 preprocess + transform.opacity（= 画布节点该内容元素的外观）。
//       来源判定＝有无图片元素：fileName / decodedPixels / 内层落位任一非空即图片层；无图片元素而有
//       backdrop 即纯图案层。装配序（materializeLayer / composeBase 唯一口径）：图案元素（生成→其预处理→
//       其透明度）→ 图片元素（源→适应重采样→其预处理→落位合成施其透明度）→ 区域缩放 → 节点外观。
//       **底图（画布基座）不是图层**：它走既有输入生态位（file＝外部输入图 / backdrop＝generate 段），
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

// ---------------------------------------------------------------------------
// ImageFit：内层源图→图层框（boxW×boxH）的适应方式（仅图片元素参与合成时生效）。
//   Crop    —— 源图按原生尺寸贴入图层框、按内层锚点/枢轴/偏移落位，超出图层框部分自然裁掉。
//   Stretch —— 源图重采样拉伸铺满图层框（忽略宽高比）。
//   Native  —— 图层框恒等于源图原生尺寸：**Core 不校验调用方声明**，有框路径按源尺寸收缩区域
//              收敛（框≠源尺寸时以源为准），不缩放、不变形。
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
// ImageLayer：图层框节点（SPEC §2.2a）——图案元素 + 图片元素 + 节点落位与外观。
//   来源态无独立枚举字段：有图片元素（fileName / decodedPixels / hasImagePlacement 任一非空）
//   → 图片层（可再叠图案元素）；无图片元素而 backdrop 有值 → 纯图案层。
//   decodedPixels 仅在文件源且上层已解码时非空；id 为跨会话稳定标识（GUI 引用/缓存键），Core 只存取不解释。
// ---------------------------------------------------------------------------
struct ImageLayer {
    int id{0};
    // ---- 图片元素（可选：三者皆空 = 无图片元素，纯图案层）----
    std::string fileName;                            // 文件来源（相对 assets 目录的文件名；layers 段合法、generate 段不落路径）
    core::Image decodedPixels;                       // 上层解码注入的源像素（文件源用；不入配置）
    bool hasImagePlacement{false};
    ImageFit imageFit{ImageFit::Crop};
    int boxWidth{0};                                        // 图层框宽（节点区域）
    int boxHeight{0};                                       // 图层框高
    generate::ImageAnchor imageAnchor{generate::ImageAnchor::Center};   // 内层源图锚点（对齐到图层框的哪个位置）
    generate::ImageAnchor imagePivot{generate::ImageAnchor::Center};    // 内层源图枢轴（源图对齐到锚点的部位）
    int imageX{0};                                          // 内层自由偏移 X
    int imageY{0};                                          // 内层自由偏移 Y
    preprocess::PreprocessPipeline imagePreprocess;         // 元素外观·预处理（作用于适应重采样后的源图，先于落位合成）
    std::uint8_t imageOpacity{255};                         // 元素外观·不透明度（0..255，落位合成时逐像素施乘）
    // ---- 图案元素（可选：空 = 透明底）----
    //   纯图案层 = 有 backdrop 无图片元素；图片层的图层框衬底 = backdrop 铺满区域。
    std::optional<generate::PatternParams> backdrop;
    preprocess::PreprocessPipeline backdropPreprocess;      // 元素外观·预处理（作用于生成的图案）
    std::uint8_t backdropOpacity{255};                      // 元素外观·不透明度
    // ---- 节点落位与外观 ----
    ImageTransform transform;
    preprocess::PreprocessPipeline preprocess;              // 节点外观（层级预处理，先物化再落位；口径与顶层 preprocess 一致）
    int zOrder{0};                                   // 叠放次序：0＝最底、越大越靠顶
};

// 图片元素是否存在（fileName / 解码像素 / 内层落位任一非空即视为有图片元素）。
bool hasImageElement(const ImageLayer& layer);

// ---------------------------------------------------------------------------
// materializeLayer：物化单层的「整层位图」（已组装·已缩放·已预处理·未落位）——装配序（SPEC §2.2a，唯一口径）：
//   图片元素源处理（适应重采样 → 元素预处理；**源处理前移是无框路径区域尺寸自洽的前提**）→ 区域定尺寸
//   （有框＝box、Native 按源尺寸收敛；无框＝预处理后源尺寸；纯图案＝transform 目标）→ 图案元素
//   （生成→其预处理→其透明度）→ 图片元素落位合成施其透明度（source-over 叠于图案之上）→ 区域缩放 →
//   节点外观（preprocess）。
//   不含落位定位（那是 composeCanvas 的合成期职责），故 GUI 可按 (layerId, 元素+节点签名)
//   缓存本结果，改 transform.x/y/锚点/不透明度时不重跑。不读取磁盘：File 源依赖 decodedPixels 已被上层注入；
//   未注入且无落位则视为空图（占位）。
// ---------------------------------------------------------------------------
core::Image materializeLayer(const ImageLayer& layer);

// ---------------------------------------------------------------------------
// composeCanvas：产出 width×height 的 RGBA 画布图。画布先铺满全透明底，再落**底图 base**
//   （非空则 source-over 铺于 (0,0)、按画布尺寸自然裁剪/留空），最后按 zOrder 升序逐层叠加
//   layers（std::vector<ImageLayer>，只装叠加层、**不含底图**）：逐层物化（materializeLayer：组装含底色框→缩放→预处理）→ 逐像素施 transform.opacity 作 alpha 乘子
//   → 依锚点/枢轴/自由偏移定位 → source-over 落位；越出画布的像素写入 w×h 缓冲时自然裁剪。
//   base 为空（默认）＝无底图，仅透明底 + 叠加层。整体显隐由调用方（GUI）以「传空 layers」实现，Core 不持显隐态。
//   materialized（可选）：预物化位图表（key＝layer.id，value＝materializeLayer 的**已组装·缩放·预处理·未落位**产物）；
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
//   纯图案底图（有 recipe 且无内层落位）：按画布(source)尺寸经 makePattern 现算图案
//   （不走 materializeLayer 的 transform 尺寸，避免回归）；如有逐层预处理则同口径 applyExec。
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
//   （materializeLayer 整层位图 → 锚点/枢轴/自由偏移定位），是落位几何的单一真相源。
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
