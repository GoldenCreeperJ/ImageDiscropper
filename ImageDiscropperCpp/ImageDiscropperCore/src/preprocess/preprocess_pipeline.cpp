// ============================================================================
// 文件：src/preprocess/preprocess_pipeline.cpp
// 作用：实现 PreprocessPipeline 的步骤管理与执行逻辑。
//       applyOne 是核心分派函数：按 PreprocessConfig 的实际类型调用
//       pixel_ops 模块中的对应算法，返回新图像。
// ============================================================================
#include "preprocess/preprocess_pipeline.h"

#include <algorithm>
#include <type_traits>
#include <variant>

#include "pixel_ops/color_ops.h"
#include "pixel_ops/geometric_ops.h"

namespace idc::preprocess {
namespace {

// 归一旋转角到象限 0..3（90° 步长），与 pixel_ops::normalizeAngle 同口径。
int rotateQuad(const int angle) {
    int a = angle % 360;
    if (a < 0) a += 360;
    return a / 90 % 4;
}

// 把一段几何 op（RotateOp/FlipOp，按施加先后排列）折叠为规范的 ≤2 个 op。
// 累积 D4 元素表示为 g = Fh(f) ∘ Rot(r)（先施 Rot(r)，f 为真再施水平镜像）。
// 新 op 施加于当前结果「之后」（左乘），据共轭关系 Fh∘Rot(θ)=Rot(-θ)∘Fh 推得增量规则：
//   Rotate(k) : f==0 → r+=k ；f==1 → r-=k（跨过镜像时旋转取反）
//   FlipH     : f^=1
//   FlipV     : r+=2; f^=1（因 FlipV = R180 ∘ FlipH）
// 分解（尽量少趟）：恒等→无；纯旋转→1 个 RotateOp；镜像且 r∈{0,2}→单个 FlipOp(H/V)；
// 镜像且 r∈{1,3}（对角镜像）→先 RotateOp 后 FlipOp(H)。
std::vector<PreprocessConfig> foldGeometryRun(const std::vector<PreprocessConfig>& run) {
    int r = 0;
    int f = 0;
    for (const auto& cfg : run) {
        if (const auto* rot = std::get_if<RotateOp>(&cfg)) {
            const int k = rotateQuad(rot->angle);
            r = f == 0 ? r + k : r - k;
        } else if (const auto* fl = std::get_if<FlipOp>(&cfg)) {
            if (fl->horizontal) {
                f ^= 1;
            } else {
                r += 2;
                f ^= 1;
            }
        }
    }
    r = (r % 4 + 4) % 4;
    std::vector<PreprocessConfig> out;
    if (f == 0) {
        if (r != 0) out.emplace_back(RotateOp{r * 90});
    } else {
        if (r == 0) {
            out.emplace_back(FlipOp{true});    // 水平镜像
        } else if (r == 2) {
            out.emplace_back(FlipOp{false});   // 垂直镜像（Fh ∘ R180 = FlipV）
        } else {
            out.emplace_back(RotateOp{r * 90}); // 对角镜像：先旋转后水平翻折
            out.emplace_back(FlipOp{true});
        }
    }
    return out;
}

// 掩码规范化：与 pixel_ops 完全同口径——长度非 3 视为 "111"（保证折叠后与 applyOne 逐像素一致）。
std::string normMask3(const std::string& m) { return m.size() == 3 ? m : std::string("111"); }

// 逐通道掩码复合：pre-gray 段将任意 Split/Invert 序列折成单映射 f(v)=(v&K)^J。
// Split(m): K&=m, J&=m；Invert(m): J^=m（逐通道 '0'/'1'）。alpha 全程不动，与 applyOne 一致。
char maskAnd(const char a, const char b) { return a == '1' && b == '1' ? '1' : '0'; }
char maskXor(const char a, const char b) { return a != b ? '1' : '0'; }

// 把一段**连续**色彩步折叠为规范形（逐像素等价）：
//   · 首个 GrayOp 之前（多通道）：全部 Split/Invert 复合为至多 SplitOp{K}+InvertOp{J}（恒等分量省略）；
//   · 只保留第一个 GrayOp（gray∘gray=clone，后续灰度为幂等空操作）；
//   · GrayOp 之后（单亮度通道）：SplitOp 为死步丢弃，InvertOp 按奇偶折叠（两次抵消），末尾至多补 1 个 InvertOp{"111"}。
// enteringGray：本段进入时图像是否已为灰度（由前面某段曾出现 GrayOp 决定）。灰度态在整个流水线内单调不回退
//   （几何 / resize 均保持灰度），故需由调用方传入；进入灰度后本段内不再有多通道路径。
// 前提：进入流水线的图像为多通道（载入器强解 RGBA），故 !enteringGray 的 pre-gray 位映射合并对真实输入成立。
std::vector<PreprocessConfig> foldColorRun(const std::vector<PreprocessConfig>& run, const bool enteringGray) {
    std::vector<PreprocessConfig> out;
    std::string k = "111";
    std::string j = "000";
    bool afterGray = enteringGray;   // 灰度态单调不回退；一旦进入即为单通道语义
    int invertParity = 0;
    const auto flushPre = [&] {
        if (k != "111") out.emplace_back(SplitOp{k});
        if (j != "000") out.emplace_back(InvertOp{j});
    };
    for (const auto& cfg : run) {
        if (std::holds_alternative<GrayOp>(cfg)) {
            if (!afterGray) { flushPre(); out.emplace_back(GrayOp{}); afterGray = true; }
            continue;   // 重复 gray（或 gray 后再 gray）＝幂等空操作，丢弃
        }
        if (afterGray) {
            // 单通道：Split 无意义丢弃；非全 0 的 Invert 翻转一次奇偶。
            if (const auto* inv = std::get_if<InvertOp>(&cfg); inv && normMask3(inv->mask) != "000")
                invertParity ^= 1;
            continue;   // SplitOp 及 mask=="000" 的 InvertOp 均为空操作
        }
        if (const auto* sp = std::get_if<SplitOp>(&cfg)) {
            const std::string m = normMask3(sp->mask);
            for (int c = 0; c < 3; ++c) { k[c] = maskAnd(k[c], m[c]); j[c] = maskAnd(j[c], m[c]); }
        } else if (const auto* iv = std::get_if<InvertOp>(&cfg)) {
            const std::string m = normMask3(iv->mask);
            for (int c = 0; c < 3; ++c) j[c] = maskXor(j[c], m[c]);
        }
    }
    if (!afterGray) flushPre();
    else if (invertParity) out.emplace_back(InvertOp{std::string("111")});
    return out;
}

} // namespace

// ---------------------------------------------------------------------------
// 公共接口：对一张图像应用单个操作，返回结果
// 分块依据：每种 PreprocessConfig 子类对应一个 if constexpr 分支，
//          由 std::visit 在编译期完成类型分派，避免运行时 switch。
// ---------------------------------------------------------------------------
core::Image applyOne(const core::Image& src, const PreprocessConfig& cfg) {
    return std::visit([&](const auto& op) -> core::Image {
        using T = std::decay_t<decltype(op)>;

        if constexpr (std::is_same_v<T, GrayOp>) {
            return pixel_ops::toGray(src);
        }
        else if constexpr (std::is_same_v<T, SplitOp>) {
            return pixel_ops::splitChannels(src, op.mask);
        }
        else if constexpr (std::is_same_v<T, InvertOp>) {
            return pixel_ops::invertChannels(src, op.mask);
        }
        else if constexpr (std::is_same_v<T, RotateOp>) {
            return pixel_ops::rotate(src, op.angle);
        }
        else if constexpr (std::is_same_v<T, FlipOp>) {
            return pixel_ops::flip(src, op.horizontal);
        }
        else if constexpr (std::is_same_v<T, ResizeOp>) {
            if (op.width <= 0 || op.height <= 0) return src.clone();   // 非正尺寸 → 无操作。
            return pixel_ops::resize(src, op.width, op.height);
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

// 语义等价、趟数更少的折叠版流水线：对**连续同类段**各自就地规整，段间（含 resize）绝不跨越重排。
//   · 连续几何段（RotateOp/FlipOp）→ 规范 ≤2 op（D4 纯置换复合，逐像素恒等）；
//   · 连续色彩段（GrayOp/SplitOp/InvertOp）→ 规范形（pre-gray 折成 Split+Invert、gray 幂等、
//     post-gray 死步丢弃 + invert 按奇偶折叠）；与 applyOne 同取整/掩码口径，逐像素恒等。
// 不跨边界调序：色彩与几何/resize 并非总可交换（resize 双线性、gray 截断），故只在同类连续段内折叠。
PreprocessPipeline PreprocessPipeline::folded() const {
    PreprocessPipeline out;
    std::vector<PreprocessConfig> geoRun;    // 当前连续几何段（按施加顺序）
    std::vector<PreprocessConfig> colorRun;  // 当前连续色彩段（按施加顺序）
    bool graySeen = false;                    // 全流水线单调灰度态：某段出现 GrayOp 后恒为 true
    const auto flushColor = [&] {
        if (colorRun.empty()) return;
        const bool hadGray = std::any_of(colorRun.begin(), colorRun.end(),
            [](const PreprocessConfig& c) { return std::holds_alternative<GrayOp>(c); });
        for (auto& c : foldColorRun(colorRun, graySeen)) out.add(std::move(c));
        colorRun.clear();
        if (hadGray) graySeen = true;
    };
    const auto flushGeo = [&] {
        for (auto& c : foldGeometryRun(geoRun)) out.add(std::move(c));
        geoRun.clear();
    };
    for (const auto& cfg : steps_) {
        if (isGeometryStep(cfg)) {
            flushColor();                    // 几何段起头：先落地前段色彩
            geoRun.push_back(cfg);
        } else if (isColorStep(cfg)) {
            flushGeo();                       // 色彩段起头：先落地前段几何
            colorRun.push_back(cfg);
        } else {
            // ResizeOp（或非预期类型）：本身即段界，两段先后落地后原样保留。
            flushColor();
            flushGeo();
            out.add(cfg);
        }
    }
    flushColor();
    flushGeo();
    return out;
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
