// ============================================================================
// 文件：include/generate/pattern.h
// 作用：程序化图案生成（「新建画布」的画布内容工厂）。给定尺寸 + 底色（含 alpha）
//       + 前景色 + 若干图案参数，产出一张 RGBA 图像。供 GUI「新建」对话框采集参数后
//       经 EngineBridge 调用，也供 CLI 的 `new` 子命令与各命令的 `--size` 生成源调用。
// 分块依据：纯像素生成逻辑（渐变插值、棋盘/条纹判定、网格/点阵布点、同心环/放射扇区），
//           全数内聚于本模块，GUI 不复现任何像素运算（CONTRIBUTING.md「分层纪律」）。
// 说明：所有生成恒输出 RGBA；非法尺寸（宽或高 <= 0，或超 kMaxDimension，或总像素超 kMaxTotalPixels）
//       返回空图（0×0），调用方据此判失败。护栏在分配像素前短路，绝不至 bad_alloc。
//       底色 alpha 直接写入像素，alpha<255 即得透明/半透明画布。
//       图片不作为程序化图案类型：它是 canvas::ImageLayer「图片图层」（见 include/canvas/layer.h），
//       由 GUI 附加层入口经两段合成（图片→图层框→底图，composeCanvas）落入画布，本模块无图片像素逻辑。
// ============================================================================
#pragma once

#include <string>

#include "core/color.h"
#include "core/core_nums.h"  // 图案参数出厂默认（常量层）
#include "core/image.h"

namespace idc::generate {

// 生成的单边长上限：拦截单维离谱输入（如 1×999999）；内存护栏另由 kMaxTotalPixels 兜底。
inline constexpr int kMaxDimension = 65535;

// 生成的总像素上限（宽×高）：对齐 NFR-3 量级，64,000,000 px ≈ 256MB（RGBA）。
//   65535×65535 单边看似合法却需 ≈17.2GB，会在 Image 构造 assign() 处 bad_alloc 崩进程；
//   makePattern 在分配前据此短路返回空图，GUI/CLI 早校验退出码 1。单边合法但总像素超限亦拒绝。
inline constexpr long long kMaxTotalPixels = 64LL * 1000 * 1000;

// ---------------------------------------------------------------------------
// PatternKind：程序生成的画布图案类型。
//   Solid          —— 纯色（仅用 background，含 alpha）。
//   LinearGradient —— background→foreground 沿 orientation 轴线性插值（alpha 同插）。
//   RadialGradient —— background(中心)→foreground(四角) 按归一化半径插值。
//   Checkerboard   —— 双色棋盘格，格子边长 = period。
//   Stripes        —— 双色条纹，方向由 orientation 选：水平(随 x)/垂直(随 y)/斜↘(沿 x−y)/斜↗(沿 x+y)，周期 = period。
//   GridLines      —— 底色上每隔 period 画一条 foreground 线（线宽 lineWidth）。
//   DotGrid        —— 底色上每隔 period 布一个 foreground 实心点（半径 dotRadius）。
//   Concentric     —— 以图像中心为圆心的同心环带，环间距 = period、环宽 = lineWidth；orientation 选形状：水平=圆环(欧氏)/垂直=方环(切比雪夫，回字纹)。
//   RadialRays     —— 以图像中心为顶点的放射扇区，双色交替，扇区数由 period 定标。
//   HueGradient    —— 全色相彩虹渐变，沿 orientation 轴扫 H∈[0,1)；period 为一个彩虹循环的像素宽（全饱和全明度、不透明）。
//   Noise          —— 确定性伪随机噪声（由 seed 定种、同 seed+同尺寸恒同图，供 C++/Rust 逐像素对拍）；
//                      noiseMode 选 全彩 RGB / 灰度 / 双色(bg fg 随机)；orientation/period/lineWidth/dotRadius 无效。
// ---------------------------------------------------------------------------
enum class PatternKind {
    Solid,
    LinearGradient,
    RadialGradient,
    Checkerboard,
    Stripes,
    GridLines,
    DotGrid,
    Concentric,
    RadialRays,
    HueGradient,
    Noise,
};

// 图案推进方向 / 轴：线性渐变轴、色相扫描轴、条纹方向（含斜向）、同心环形状。
enum class Orientation {
    Horizontal,   // 沿 X 推进（条纹：竖条；同心：圆环）
    Vertical,     // 沿 Y 推进（条纹：横条；同心：方环）
    DiagonalDown, // ↘ 沿 (x−y) 推进（仅条纹）
    DiagonalUp,   // ↗ 沿 (x+y) 推进（仅条纹）
};

// 噪声配色模式（仅 Noise 生效）。
enum class NoiseMode {
    FullColor,    // 每像素 R/G/B 各独立随机，不透明（全色域压测）
    Grayscale,    // 每像素随机灰阶（R=G=B），不透明
    TwoColor,     // 每像素在 foreground / background 两色间随机选一（双色雪点）
};

// 落位锚点（3×3 = 居中 + 8 靠边）：供 canvas::ImageTransform 的画布锚点/图片枢轴与 §7 序列化使用
//   （图案本身无落位语义，图片落位归 canvas 模块）。
enum class ImageAnchor {
    TopLeft, TopCenter, TopRight,
    MiddleLeft, Center, MiddleRight,
    BottomLeft, BottomCenter, BottomRight,
};

// 返回图案类型的**显示名**（供 GUI 下拉 / 错误提示展示）：按当前语言取译文，源语言下就是中文。
//   返回 std::string 而非 const char*：非源语言时译文来自查表得到的临时串，返回指针会悬空。
//   落盘与比较用的 §7 token 不走本函数（见 texts::defaults::kPatternKind* 与 gen*Token 两套映射）。
std::string patternKindName(PatternKind kind);

// ---------------------------------------------------------------------------
// PatternParams：一次生成的全部可调参数（GUI 采集后原样搬运，无引擎语义）。
// ---------------------------------------------------------------------------
struct PatternParams {
    PatternKind kind{PatternKind::Solid};
    core::Color background{core::kWhite};   // 底色（alpha 生效；渐变起点色）
    core::Color foreground{core::kBlack};   // 前景/图案色（渐变终点色）
    Orientation orientation{Orientation::Horizontal};
    int period{core::nums::generate::kDefaultPeriod};      // 周期 / 格子边长 / 环间距（px）；内部钳到 >=2
    int lineWidth{core::nums::generate::kDefaultLineWidth}; // 网格线 / 同心环线宽（px）；内部钳到 >=1
    int dotRadius{core::nums::generate::kDefaultDotRadius}; // 点阵点半径（px）；内部钳到 >=1
    int seed{0};         // 噪声种子（仅 Noise 用，>=0；同 seed+同尺寸恒同图）
    NoiseMode noiseMode{NoiseMode::FullColor};   // 噪声配色模式（仅 Noise 用）

    // 参数相等（供 GUI 图层物化缓存签名 / 配置往返比较）。
    bool operator==(const PatternParams& o) const {
        return kind == o.kind && background == o.background && foreground == o.foreground &&
               orientation == o.orientation && period == o.period && lineWidth == o.lineWidth &&
               dotRadius == o.dotRadius && seed == o.seed && noiseMode == o.noiseMode;
    }
};

// ---------------------------------------------------------------------------
// makePattern：产出 width×height 的 RGBA 图案图。
//   非法尺寸（<=0 或任一边超 kMaxDimension，或 宽×高 超 kMaxTotalPixels）→ 返回空图（empty() 为真）。
//   params 内的 period/lineWidth/dotRadius 会被钳到合法下限，越界坐标自动忽略。
// ---------------------------------------------------------------------------
core::Image makePattern(int width, int height, const PatternParams& params);

} // namespace idc::generate
