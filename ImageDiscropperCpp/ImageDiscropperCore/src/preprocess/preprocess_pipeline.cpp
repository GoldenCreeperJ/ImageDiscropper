
// ============================================================================
// 文件：src/preprocess/preprocess_pipeline.cpp
// 作用：实现 PreprocessPipeline 的步骤管理与执行逻辑。
//       applyOne 是核心分派函数：按 PreprocessConfig 的实际类型调用
//       pixel_kernels 私有核（旋转/翻转/反色/分离）与 core::Image 成员（灰度/缩放），返回新图像。
//       applyExec 另走执行层融合：先把队列压成 ExecStep 计划（resize 为块界，块内 ≤4 步），
//       再交融合核 channelMap（RGB 位映射单趟）/ remapD4 单趟执行；applyOne 逐步语义保留作等价参照 oracle。
// ============================================================================
#include "preprocess/preprocess_pipeline.h"

#include <algorithm>
#include <ranges>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#include "pixel_kernels.h"

#include "core/core_nums.h"   // 参数边界（常量层）
#include "i18n/core_texts.h"  // 通道掩码 token（常量层）

namespace idc::preprocess {
namespace {

// 常量层别名（文件内私有）：通道掩码 token 与掩码长度。
namespace masks = core::texts::defaults;
namespace nums = core::nums;

// 归一旋转角到象限 0..3（90° 步长），与 pixel_kernels.cpp 的 normalizeAngle 同口径。
int rotateQuad(const int angle) {
    int a = angle % 360;
    if (a < 0) a += 360;
    return a / 90 % 4;
}

// 掩码规范化：与 pixel_kernels 色彩核完全同口径——长度非 3 视为全通道（保证融合执行与 applyOne 逐像素一致）。
std::string normMask3(const std::string& m) { return m.size() == nums::preprocess::kMaskLength ? m : std::string(masks::kMaskAllOn); }

// 逐通道掩码复合：pre-gray 段将任意 Split/Invert 序列折成单映射 f(v)=(v&K)^J。
// Split(m): K&=m, J&=m；Invert(m): J^=m（逐通道 '0'/'1'）。alpha 全程不动，与 applyOne 一致。
char maskAnd(const char a, const char b) { return a == '1' && b == '1' ? '1' : '0'; }
char maskXor(const char a, const char b) { return a != b ? '1' : '0'; }

// ---------------------------------------------------------------------------
// execPlan：执行层融合计划（内部类型，**绝不进 PreprocessConfig / §7 schema / 序列化 / GUI 词表**）。
// 块（以 ResizeOp 为界）内任意队列至多 4 步即可承载：
//   ExecChannelMap —— pre-gray 多通道位映射 f(v)=(v&K)^J（channelMap 融合核，折掉任意 Split/Invert 序列）；
//   ExecGray       —— 灰度化（幂等，块内至多一次）；
//   ExecGrayInvert —— 灰度态整幅取反（字节 XOR，奇偶累积；GRAY 输入时跨块持续）；
//   ExecD4         —— 整块几何步单趟重排（remapD4 融合核，折掉任意 Rotate/Flip 序列，含跨 gray 合并）；
//   ResizeOp       —— 块界，原样透传（复用 applyOne 单步语义，含非正尺寸空操作）。
// 不透明度不属本管线（由 canvas::ImageTransform.opacity 在合成期承载），本计划不携带 alpha 仿射。
// 可交换性依据：逐像素位映射 / 灰度字节计算与 D4 精确置换互为可交换，故同块内色彩与几何可各自独立融合
//   （几何无损前提：copyPixel 的 GRAY 单字节直拷）；resize 双线性取整与两者均不可交换 → 唯一块界。
// 输出步序：同块内按 map → gray → 灰度反色 → D4 规范序发射（可交换故与声明序逐像素等价）。
struct ExecChannelMap { std::string k, j; };
struct ExecGray {};
struct ExecGrayInvert {};
struct ExecD4 { int quad; bool flipH; };
using ExecStep = std::variant<ExecChannelMap, ExecGray, ExecGrayInvert, ExecD4, ResizeOp>;

// 由声明队列构建融合计划。inputIsGray 决定初始通道态（载入器强解 RGBA 时恒为 false；
// 直接喂 GRAY 图时 Split 全部变死步、Invert 按整幅奇偶——与 applyOne 逐步语义逐像素对齐）。
// 几何累加用 D4 共轭律（Fh∘Rot(θ)=Rot(-θ)∘Fh）；掩码累加经 normMask3/maskAnd/maskXor 逐通道复合。
std::vector<ExecStep> buildExecPlan(const std::vector<PreprocessConfig>& steps, const bool inputIsGray) {
    std::vector<ExecStep> plan;
    int r = 0, f = 0;                    // D4 累加器：g = Fh(f) ∘ Rot(r)
    std::string k = masks::kMaskAllOn, j = masks::kMaskAllOff;    // pre-gray 位映射累加器
    bool gray = inputIsGray;             // 灰度态单调不回退（几何 / resize 均保持灰度）
    int parity = 0;                      // 灰度整幅取反奇偶
    const auto flushMap = [&] {
        if (!gray && (k != masks::kMaskAllOn || j != masks::kMaskAllOff)) plan.emplace_back(ExecChannelMap{k, j});
        k = masks::kMaskAllOn;
        j = masks::kMaskAllOff;
    };
    const auto flushInvert = [&] {
        if (gray && parity != 0) {
            plan.emplace_back(ExecGrayInvert{});
            parity = 0;
        }
    };
    const auto flushD4 = [&] {
        if (r != 0 || f != 0) plan.emplace_back(ExecD4{(r % 4 + 4) % 4, f != 0});
        r = 0;
        f = 0;
    };
    for (const auto& cfg : steps) {
        if (isGeometryStep(cfg)) {
            if (const auto* rot = std::get_if<RotateOp>(&cfg)) {
                const int quad = rotateQuad(rot->angle);
                r = f == 0 ? r + quad : r - quad;
            } else if (const auto* fl = std::get_if<FlipOp>(&cfg)) {
                if (fl->horizontal) {
                    f ^= 1;
                } else {
                    r += 2;
                    f ^= 1;
                }
            }
        } else if (isColorStep(cfg)) {
            if (std::holds_alternative<GrayOp>(cfg)) {
                if (!gray) {   // 重复 gray 幂等丢弃
                    flushMap();    // pre-gray 位映射必在灰度化之前落地
                    plan.emplace_back(ExecGray{});
                    gray = true;
                }
            } else if (gray) {
                // 单通道：Split 死步丢弃；非全 '0' 掩码的 Invert 整幅翻一次（非 3 长归全通道同口径）。
                if (const auto* inv = std::get_if<InvertOp>(&cfg); inv && normMask3(inv->mask) != masks::kMaskAllOff)
                    parity ^= 1;
            } else if (const auto* sp = std::get_if<SplitOp>(&cfg)) {
                const std::string m = normMask3(sp->mask);
                for (int c = 0; c < 3; ++c) { k[c] = maskAnd(k[c], m[c]); j[c] = maskAnd(j[c], m[c]); }
            } else if (const auto* iv = std::get_if<InvertOp>(&cfg)) {
                const std::string m = normMask3(iv->mask);
                for (int c = 0; c < 3; ++c) j[c] = maskXor(j[c], m[c]);
            }
        } else {
            // ResizeOp（块界）：本块三步先落地再透传，绝不跨 resize 合并（双线性取整不可交换）。
            flushMap();
            flushInvert();
            flushD4();
            plan.emplace_back(std::get<ResizeOp>(cfg));
        }
    }
    flushMap();
    flushInvert();
    flushD4();
    return plan;
}

// 执行单个计划步：融合核直调；ResizeOp 回 fall applyOne，保证与逐步语义同一口径。
core::Image applyExecStep(const core::Image& src, const ExecStep& st) {
    return std::visit([&]<typename T0>(const T0& op) -> core::Image {
        using T = std::decay_t<T0>;

        if constexpr (std::is_same_v<T, ExecChannelMap>) {
            return channelMap(src, op.k, op.j);
        }
        else if constexpr (std::is_same_v<T, ExecGray>) {
            return src.toGray();
        }
        else if constexpr (std::is_same_v<T, ExecGrayInvert>) {
            return invertChannels(src, masks::kMaskAllOn);
        }
        else if constexpr (std::is_same_v<T, ExecD4>) {
            return remapD4(src, op.quad, op.flipH);
        }
        else {
            return applyOne(src, op);   // ResizeOp
        }
    }, st);
}

} // namespace

// ---------------------------------------------------------------------------
// 公共接口：对一张图像应用单个操作，返回结果
// 分块依据：每种 PreprocessConfig 子类对应一个 if constexpr 分支，
//          由 std::visit 在编译期完成类型分派，避免运行时 switch。
//          像素核：几何/色彩走 pixel_kernels（私有），灰度与缩放走 core::Image 成员。
// ---------------------------------------------------------------------------
core::Image applyOne(const core::Image& src, const PreprocessConfig& cfg) {
    return std::visit([&]<typename T0>(const T0& op) -> core::Image {
        using T = std::decay_t<T0>;

        if constexpr (std::is_same_v<T, GrayOp>) {
            return src.toGray();
        }
        else if constexpr (std::is_same_v<T, SplitOp>) {
            return splitChannels(src, op.mask);
        }
        else if constexpr (std::is_same_v<T, InvertOp>) {
            return invertChannels(src, op.mask);
        }
        else if constexpr (std::is_same_v<T, RotateOp>) {
            return rotate(src, op.angle);
        }
        else if constexpr (std::is_same_v<T, FlipOp>) {
            return flip(src, op.horizontal);
        }
        else if constexpr (std::is_same_v<T, ResizeOp>) {
            if (op.width <= 0 || op.height <= 0) return src.clone();   // 非正尺寸 → 无操作。
            return src.resized(op.width, op.height);
        }
        else {
            return src.clone();
        }
    }, cfg);
}

// ---------------------------------------------------------------------------

// 按顺序应用所有步骤到 src，返回结果图像。
core::Image PreprocessPipeline::apply(const core::Image& src) const {
    core::Image cur = src.clone();
    for (const auto& cfg : items()) {
        cur = applyOne(cur, cfg);
    }
    return cur;
}

// 执行层融合入口：先把队列压成 execPlan（见 buildExecPlan 注释），再按计划单趟执行。
// 与 apply(src) 逐像素等价（含尺寸/格式）；像素趟数恒 ≤ 块数×4，与队列长度解耦。
// 计划构建不碰像素；逐步 apply/applyOne 保留不动，充当等价性参照 oracle。
core::Image PreprocessPipeline::applyExec(const core::Image& src) const {
    const std::vector<ExecStep> plan = buildExecPlan(items(), src.isGray());
    core::Image cur = src.clone();
    for (const auto& st : plan) {
        cur = applyExecStep(cur, st);
    }
    return cur;
}

// 纯元数据推演：不触碰像素，只按各步语义更新 (w,h) 与净旋转象限。
PreprocessPipeline::GeometryOut PreprocessPipeline::geometryOutput(const int width, const int height) const {
    int w = width, h = height, rotQuad = 0;
    for (const auto& cfg : items()) {
        std::visit([&]<typename T0>(const T0& op) {
            using T = std::decay_t<T0>;
            if constexpr (std::is_same_v<T, RotateOp>) {
                const int norm = (op.angle % 360 + 360) % 360;   // 归一到 [0,360)
                if (norm == 90 || norm == 270) std::swap(w, h);
                rotQuad = (rotQuad + norm / 90) % 4;
            } else if constexpr (std::is_same_v<T, ResizeOp>) {
                if (op.width > 0 && op.height > 0) { w = op.width; h = op.height; }
            }
            // Gray/Split/Invert/Flip：尺寸与旋转象限不变。
        }, cfg);
    }
    return {w, h, rotQuad};
}

// 正向矩形映射：把 [x0,y0]-[x1,y1] 矩形（连续坐标）随各几何步映到输出坐标系，逐步追踪当前尺寸 curW×curH。
//   各步顶点对射与 pixel_kernels 的逐像素正映射严格同口径（去 -1 的连续坐标版）：
//     rotate 90CW  (px,py)->(curH-py, px)，尺寸互换；  180 ->(curW-px, curH-py)；  270CW ->(py, curW-px)，尺寸互换；
//     flipH (px,py)->(curW-px, py)；  flipV ->(px, curH-py)；  resize 线性缩放 sx=tw/curW, sy=th/curH。
//   矩形映 4 角后取包围盒（轴对齐映轴对齐，90 倍旋转/翻转仍轴对齐）。
PreprocessPipeline::MappedRect PreprocessPipeline::mapRectGeometry(
        const double x, const double y, const double width, const double height,
        const int inW, const int inH) const {
    double x0 = x, y0 = y, x1 = x + width, y1 = y + height;
    int curW = inW, curH = inH;
    for (const auto& cfg : items()) {
        std::visit([&]<typename T0>(const T0& op) {
            using T = std::decay_t<T0>;
            if constexpr (std::is_same_v<T, RotateOp>) {
                if (const int norm = (op.angle % 360 + 360) % 360; norm == 90) {
                    const double nx0 = curH - y1, ny0 = x0, nx1 = curH - y0, ny1 = x1;
                    x0 = nx0; y0 = ny0; x1 = nx1; y1 = ny1;
                    std::swap(curW, curH);
                } else if (norm == 180) {
                    const double nx0 = curW - x1, ny0 = curH - y1, nx1 = curW - x0, ny1 = curH - y0;
                    x0 = nx0; y0 = ny0; x1 = nx1; y1 = ny1;
                } else if (norm == 270) {
                    const double nx0 = y0, ny0 = curW - x1, nx1 = y1, ny1 = curW - x0;
                    x0 = nx0; y0 = ny0; x1 = nx1; y1 = ny1;
                    std::swap(curW, curH);
                }
            } else if constexpr (std::is_same_v<T, FlipOp>) {
                if (op.horizontal) {
                    const double nx0 = curW - x1, nx1 = curW - x0;
                    x0 = nx0; x1 = nx1;
                } else {
                    const double ny0 = curH - y1, ny1 = curH - y0;
                    y0 = ny0; y1 = ny1;
                }
            } else if constexpr (std::is_same_v<T, ResizeOp>) {
                if (op.width > 0 && op.height > 0 && curW > 0 && curH > 0) {
                    const double sx = static_cast<double>(op.width) / curW;
                    const double sy = static_cast<double>(op.height) / curH;
                    x0 *= sx; x1 *= sx; y0 *= sy; y1 *= sy;
                    curW = op.width; curH = op.height;
                }
            }
            // Gray/Split/Invert：不碰几何，矩形直通。
        }, cfg);
    }
    return {x0, y0, x1 - x0, y1 - y0};
}

// 逆向增量映射：先正向走一遍记录每个几何步及其输入尺寸，再逆序施加各步线性部分的逆变换，
//   把输出（工作图）坐标系的平移增量 (dx,dy) 映回输入（画布）坐标系。与 mapRectGeometry 互逆：
//     resize 正为 (dx*tw/inW, dy*th/inH) → 逆取倒数；  rotate90 正 (dx,dy)->(-dy,dx) → 逆 (dy,-dx)；
//     rotate270 正 ->(dy,-dx) → 逆 (-dy,dx)；  rotate180 / flip 自逆。色彩步不改变向量。
void PreprocessPipeline::mapDeltaToInput(const double dx, const double dy,
                                          const int inW, const int inH,
                                          double& outDx, double& outDy) const {
    struct GeomStep { enum Kind { Rotate, Flip, Resize } kind;
                     int angle{0}; bool horizontal{false}; int tw{0}; int th{0}; int inW{0}; int inH{0}; };
    std::vector<GeomStep> steps;
    int curW = inW, curH = inH;
    for (const auto& cfg : items()) {
        std::visit([&]<typename T0>(const T0& op) {
            using T = std::decay_t<T0>;
            if constexpr (std::is_same_v<T, RotateOp>) {
                if (const int norm = (op.angle % 360 + 360) % 360; norm == 90 || norm == 180 || norm == 270) {
                    steps.push_back({GeomStep::Rotate, norm, false, 0, 0, curW, curH});
                    if (norm == 90 || norm == 270) std::swap(curW, curH);
                }
            } else if constexpr (std::is_same_v<T, FlipOp>) {
                steps.push_back({GeomStep::Flip, 0, op.horizontal, 0, 0, curW, curH});
            } else if constexpr (std::is_same_v<T, ResizeOp>) {
                if (op.width > 0 && op.height > 0) {
                    steps.push_back({GeomStep::Resize, 0, false, op.width, op.height, curW, curH});
                    curW = op.width; curH = op.height;
                }
            }
        }, cfg);
    }
    double vx = dx, vy = dy;
    for (const auto &[kind, angle, horizontal, tw, th, inW, inH] : std::ranges::reverse_view(steps)) {
        switch (kind) {
            case GeomStep::Resize:
                if (tw > 0 && th > 0 && inW > 0 && inH > 0) {
                    vx *= static_cast<double>(inW) / tw;
                    vy *= static_cast<double>(inH) / th;
                }
                break;
            case GeomStep::Rotate:
                if (angle == 90) { const double nx = vy, ny = -vx; vx = nx; vy = ny; }        // 逆 90CW
                else if (angle == 270) { const double nx = -vy, ny = vx; vx = nx; vy = ny; }  // 逆 270CW
                else { vx = -vx; vy = -vy; }                                                     // 180 自逆
                break;
            case GeomStep::Flip:
                if (horizontal) vx = -vx; else vy = -vy;
                break;
        }
    }
    outDx = vx;
    outDy = vy;
}

} // namespace idc::preprocess
