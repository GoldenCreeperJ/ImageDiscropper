// ============================================================================
// 文件：src/preprocess/preprocess_pipeline.cpp
// 作用：实现 PreprocessPipeline 的步骤管理与执行逻辑。
//       applyOne 是核心分派函数：按 PreprocessConfig 的实际类型调用
//       pixel_kernels 私有核（旋转/翻转/反色/分离）与 core::Image 成员（灰度/缩放），返回新图像。
//       applyExec 另走执行层融合：先把队列压成 ExecStep 计划（resize 为块界，块内 ≤4 步），
//       再交融合核 channelMap / remapD4 单趟执行；applyOne 逐步语义保留作等价参照 oracle。
// ============================================================================
#include "preprocess/preprocess_pipeline.h"

#include <algorithm>
#include <type_traits>
#include <variant>

#include "pixel_kernels.h"

namespace idc::preprocess {
namespace {

// 归一旋转角到象限 0..3（90° 步长），与 pixel_kernels.cpp 的 normalizeAngle 同口径。
int rotateQuad(const int angle) {
    int a = angle % 360;
    if (a < 0) a += 360;
    return a / 90 % 4;
}

// 掩码规范化：与 pixel_kernels 色彩核完全同口径——长度非 3 视为 "111"（保证融合执行与 applyOne 逐像素一致）。
std::string normMask3(const std::string& m) { return m.size() == 3 ? m : std::string("111"); }

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
    std::string k = "111", j = "000";    // pre-gray 位映射累加器
    bool gray = inputIsGray;             // 灰度态单调不回退（几何 / resize 均保持灰度）
    int parity = 0;                      // 灰度整幅取反奇偶
    const auto flushMap = [&] {
        if (!gray && (k != "111" || j != "000")) plan.emplace_back(ExecChannelMap{k, j});
        k = "111";
        j = "000";
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
                // 单通道：Split 死步丢弃；非全 '0' 掩码的 Invert 整幅翻一次（非 3 长归 "111" 同口径）。
                if (const auto* inv = std::get_if<InvertOp>(&cfg); inv && normMask3(inv->mask) != "000")
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
    return std::visit([&](const auto& op) -> core::Image {
        using T = std::decay_t<decltype(op)>;

        if constexpr (std::is_same_v<T, ExecChannelMap>) {
            return channelMap(src, op.k, op.j);
        }
        else if constexpr (std::is_same_v<T, ExecGray>) {
            return src.toGray();
        }
        else if constexpr (std::is_same_v<T, ExecGrayInvert>) {
            return invertChannels(src, "111");
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
    return std::visit([&](const auto& op) -> core::Image {
        using T = std::decay_t<decltype(op)>;

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
// PreprocessPipeline：步骤管理
// ---------------------------------------------------------------------------

// 追加步骤到队尾。
void PreprocessPipeline::add(PreprocessConfig cfg) {
    steps_.push_back(std::move(cfg));
}

// 在指定位置插入；index 超出范围时追加到队尾。
void PreprocessPipeline::insert(const std::size_t index, PreprocessConfig cfg) {
    if (index >= steps_.size()) steps_.push_back(std::move(cfg));
    else steps_.insert(steps_.begin() + static_cast<std::ptrdiff_t>(index), std::move(cfg));
}

// 删除指定位置步骤；越界忽略。
void PreprocessPipeline::remove(const std::size_t index) {
    if (index >= steps_.size()) return;
    steps_.erase(steps_.begin() + static_cast<std::ptrdiff_t>(index));
}

// 就地替换指定位置步骤（编辑参数）；越界忽略。
void PreprocessPipeline::setStep(const std::size_t index, PreprocessConfig cfg) {
    if (index >= steps_.size()) return;
    steps_[index] = std::move(cfg);
}

// 上移指定位置步骤；已在首位时忽略。
void PreprocessPipeline::moveUp(const std::size_t index) {
    if (index == 0 || index >= steps_.size()) return;
    std::swap(steps_[index], steps_[index - 1]);
}

// 下移指定位置步骤；已在末位时忽略。
void PreprocessPipeline::moveDown(const std::size_t index) {
    if (index + 1 >= steps_.size()) return;
    std::swap(steps_[index], steps_[index + 1]);
}

// 清空所有步骤。
void PreprocessPipeline::clear() { steps_.clear(); }

// 按顺序应用所有步骤到 src，返回结果图像。
core::Image PreprocessPipeline::apply(const core::Image& src) const {
    core::Image cur = src.clone();
    for (const auto& cfg : steps_) {
        cur = applyOne(cur, cfg);
    }
    return cur;
}

// 执行层融合入口：先把队列压成 execPlan（见 buildExecPlan 注释），再按计划单趟执行。
// 与 apply(src) 逐像素等价（含尺寸/格式）；像素趟数恒 ≤ 块数×4，与队列长度解耦。
// 计划构建不碰像素；逐步 apply/applyOne 保留不动，充当等价性参照 oracle。
core::Image PreprocessPipeline::applyExec(const core::Image& src) const {
    const std::vector<ExecStep> plan = buildExecPlan(steps_, src.isGray());
    core::Image cur = src.clone();
    for (const auto& st : plan) {
        cur = applyExecStep(cur, st);
    }
    return cur;
}

// 纯元数据推演：不触碰像素，只按各步语义更新 (w,h) 与净旋转象限。
PreprocessPipeline::GeometryOut PreprocessPipeline::geometryOutput(const int width, const int height) const {
    int w = width, h = height, rotQuad = 0;
    for (const auto& cfg : steps_) {
        std::visit([&](const auto& op) {
            using T = std::decay_t<decltype(op)>;
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

} // namespace idc::preprocess
