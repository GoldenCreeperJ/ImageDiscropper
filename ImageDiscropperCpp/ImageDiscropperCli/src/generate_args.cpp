// ============================================================================
// 文件：src/generate_args.cpp
// 作用：generate_args.h 的实现——读取 --size / --pattern / --orientation / --bg / --fg /
//       --period / --line-width / --dot-radius 选项，装配 Core generate::PatternParams 并调
//       generate::makePattern 合成画布；另提供 resolveInputImage 统一「读文件 / 生成」两种输入来源。
//       图片不入图案类型；图片经 config 的 layers 段（file 图层）或 generate 段落位声明引入。
// 分块依据：CLI 不实现任何像素 / 图案逻辑，全部委托 Core（CONTRIBUTING.md「分层纪律」）；本文件只做
//       「取选项 + 纯格式校验 + 调 Core + 汇总错误」。颜色解析复用 value_parser::parseColor（含 alpha）。
// 常量层：选项名取 texts::opt（不可译）、报因文案经门面 IDC_CTR(msg, ...) 取 texts::msg；尺寸单边与总像素上限
//       直接用 Core 的 generate::kMaxDimension / kMaxTotalPixels（单一真相源在上游，本层不复制数值）。
//       图案类型 / 方向 / 噪声模式的 token 值（solid / horizontal / full-color …）与枚举成对出现在
//       映射表里，属「数据即定义」，**不提**。
// ============================================================================
#include "generate_args.h"

#include <cerrno>
#include <cstdlib>
#include <iostream>
#include <string>

#include "generate/pattern.h" // generate::PatternParams / makePattern / ImageAnchor / kMaxDimension
#include "canvas/layer.h"     // canvas::ImageLayer / composeCanvas（--input + --size 同给时的铺入合成）
#include "value_parser.h"     // parseColor（底色 / 前景色，含 alpha）

#include "cli_error.h"
#include "i18n/cli_texts.h"
#include "i18n/cli_i18n.h"

namespace idc::cli {

// 选项名简写（本 TU 内不出现 opt，故用 opts，避开 resolveInputImage 的形参 opt；报因文案走 IDC_CTR）。
namespace opts = texts::opt;

namespace {

// 严格正整数解析：整串须为十进制正整数（>0），否则失败。
bool toStrictPositive(const std::string& s, int& out) {
    if (s.empty()) return false;
    errno = 0;
    char* end = nullptr;
    const long v = std::strtol(s.c_str(), &end, 10);
    if (errno == ERANGE || end == s.c_str() || *end != '\0') return false;
    if (v <= 0) return false;
    out = static_cast<int>(v);
    return true;
}

// 解析像素尺寸 "WxH"（以 'x'/'X' 分隔，均须 1..kMaxDimension）。
bool parseSize(const std::string& s, int& w, int& h, std::string& err) {
    std::string low;
    low.reserve(s.size());
    for (const char c : s) low.push_back(static_cast<char>(c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c));
    const std::size_t x = low.find('x');
    if (x == std::string::npos) { err = IDC_CTR(msg, kErrSizeForm) + s + "'"; return false; }
    int a = 0, b = 0;
    if (!toStrictPositive(s.substr(0, x), a) || !toStrictPositive(s.substr(x + 1), b)) {
        err = IDC_CTR(msg, kErrSizeTwoPos) + s + "'";
        return false;
    }
    if (a > generate::kMaxDimension || b > generate::kMaxDimension) {
        err = IDC_CTR(msg, kErrSizeDimMax) + std::to_string(generate::kMaxDimension) + IDC_CTR(msg, kErrLimitTail) + s + "'";
        return false;
    }
    if (static_cast<long long>(a) * static_cast<long long>(b) > generate::kMaxTotalPixels) {
        err = IDC_CTR(msg, kErrSizePixelsMax) + std::to_string(generate::kMaxTotalPixels) + IDC_CTR(msg, kErrLimitTail) + s + "'";
        return false;
    }
    w = a;
    h = b;
    return true;
}

// 解析图案类型名 → PatternKind（大小写不敏感）。
bool parsePatternKind(const std::string& s, generate::PatternKind& out, std::string& err) {
    std::string v;
    v.reserve(s.size());
    for (const char c : s) v.push_back(static_cast<char>(c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c));
    if (v == "solid") out = generate::PatternKind::Solid;
    else if (v == "linear-gradient") out = generate::PatternKind::LinearGradient;
    else if (v == "radial-gradient") out = generate::PatternKind::RadialGradient;
    else if (v == "checkerboard") out = generate::PatternKind::Checkerboard;
    else if (v == "stripes") out = generate::PatternKind::Stripes;
    else if (v == "grid-lines") out = generate::PatternKind::GridLines;
    else if (v == "dot-grid") out = generate::PatternKind::DotGrid;
    else if (v == "concentric") out = generate::PatternKind::Concentric;
    else if (v == "radial-rays") out = generate::PatternKind::RadialRays;
    else if (v == "hue-gradient") out = generate::PatternKind::HueGradient;
    else if (v == "noise") out = generate::PatternKind::Noise;
    else {
        err = IDC_CTR(msg, kErrUnknownKind) + s + IDC_CTR(msg, kErrUnknownKindTail);
        return false;
    }
    return true;
}

// 解析方向名 → Orientation（大小写不敏感，接受 horizontal/vertical/diagonal-down/diagonal-up 及缩写 h/v）。
bool parseOrientation(const std::string& s, generate::Orientation& out, std::string& err) {
    std::string v;
    v.reserve(s.size());
    for (const char c : s) v.push_back(static_cast<char>(c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c));
    if (v == "horizontal" || v == "h") out = generate::Orientation::Horizontal;
    else if (v == "vertical" || v == "v") out = generate::Orientation::Vertical;
    else if (v == "diagonal-down" || v == "diagonal" || v == "d") out = generate::Orientation::DiagonalDown;
    else if (v == "diagonal-up" || v == "diag-up") out = generate::Orientation::DiagonalUp;
    else {
        err = IDC_CTR(msg, kErrUnknownOrientation) + s + IDC_CTR(msg, kErrUnknownOrientationTail);
        return false;
    }
    return true;
}

// 解析噪声配色模式 → NoiseMode（大小写不敏感，仅 --pattern noise 时有义）。
bool parseNoiseMode(const std::string& s, generate::NoiseMode& out, std::string& err) {
    std::string v;
    v.reserve(s.size());
    for (const char c : s) v.push_back(static_cast<char>(c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c));
    if (v == "full-color" || v == "color") out = generate::NoiseMode::FullColor;
    else if (v == "grayscale" || v == "gray") out = generate::NoiseMode::Grayscale;
    else if (v == "two-color" || v == "two") out = generate::NoiseMode::TwoColor;
    else {
        err = IDC_CTR(msg, kErrUnknownNoiseMode) + s + IDC_CTR(msg, kErrUnknownNoiseModeTail);
        return false;
    }
    return true;
}

// 非负整数解析（种子用）：整串须为十进制且 >=0，否则失败。
bool toNonNegative(const std::string& s, int& out) {
    if (s.empty()) return false;
    errno = 0;
    char* end = nullptr;
    const long v = std::strtol(s.c_str(), &end, 10);
    if (errno == ERANGE || end == s.c_str() || *end != '\0') return false;
    if (v < 0) return false;
    out = static_cast<int>(v);
    return true;
}

} // namespace

bool hasGenerateOptions(const ArgParser& args) {
    return args.has(opts::kSize);
}

bool hasPatternOptions(const ArgParser& args) {
    return args.has(opts::kPattern) || args.has(opts::kOrientation) || args.has(opts::kBg) ||
           args.has(opts::kFg) || args.has(opts::kPeriod) || args.has(opts::kLineWidth) ||
           args.has(opts::kDotRadius) || args.has(opts::kSeed) || args.has(opts::kNoise);
}

// 仅解析尺寸 + 图案参数为 PatternParams（不落像素）；new --save-config 与 buildGeneratedImage 共用。
bool parseGenerateParams(const ArgParser& args, int& w, int& h,
                         generate::PatternParams& params, std::string& err) {
    // --- 尺寸：必填（触发合成的前提）。---
    if (!args.has(opts::kSize)) {
        err = IDC_CTR(msg, kErrNoSizeForGen);
        return false;
    }
    if (!parseSize(args.get(opts::kSize), w, h, err)) return false;

    // --- 装配 PatternParams（缺省与 Core 默认一致）。---
    if (args.has(opts::kPattern) && !parsePatternKind(args.get(opts::kPattern), params.kind, err)) return false;
    if (args.has(opts::kOrientation) && !parseOrientation(args.get(opts::kOrientation), params.orientation, err)) return false;
    if (args.has(opts::kBg) && !parseColor(args.get(opts::kBg), params.background, err)) return false;
    if (args.has(opts::kFg) && !parseColor(args.get(opts::kFg), params.foreground, err)) return false;
    if (args.has(opts::kPeriod) && !toStrictPositive(args.get(opts::kPeriod), params.period)) {
        err = IDC_CTR(msg, kErrPeriodPositive) + args.get(opts::kPeriod) + "'";
        return false;
    }
    if (args.has(opts::kLineWidth) && !toStrictPositive(args.get(opts::kLineWidth), params.lineWidth)) {
        err = IDC_CTR(msg, kErrLineWidthPositive) + args.get(opts::kLineWidth) + "'";
        return false;
    }
    if (args.has(opts::kDotRadius) && !toStrictPositive(args.get(opts::kDotRadius), params.dotRadius)) {
        err = IDC_CTR(msg, kErrDotRadiusPositive) + args.get(opts::kDotRadius) + "'";
        return false;
    }
    if (args.has(opts::kSeed) && !toNonNegative(args.get(opts::kSeed), params.seed)) {
        err = IDC_CTR(msg, kErrSeedNonNegative) + args.get(opts::kSeed) + "'";
        return false;
    }
    if (args.has(opts::kNoise) && !parseNoiseMode(args.get(opts::kNoise), params.noiseMode, err)) return false;
    return true;
}

bool buildGeneratedImage(const ArgParser& args, core::Image& out, std::string& err) {
    int width = 0, height = 0;
    generate::PatternParams params;
    if (!parseGenerateParams(args, width, height, params, err)) return false;

    // --- 委托 Core 生成；非法尺寸或越界会产空图（防御性再确认）。---
    out = generate::makePattern(width, height, params);
    if (out.empty()) {
        err = IDC_CTR(msg, kErrPatternGenFailed);
        return false;
    }
    return true;
}

ExitCode resolveInputImage(const ArgParser& args, core::Image& out, const JobOptions& opt) {
    const bool hasInput = args.has(opts::kInput);
    const bool hasGen = hasGenerateOptions(args);

    // --fit 只对「位图 + 画布」组合有效（①②无组合对象）。
    if (args.has(opts::kFit) && !(hasInput && hasGen)) {
        reportError(makeError(ExitCode::ArgError, IDC_CTR(msg, kErrFitNeedsBoth)));
        return ExitCode::ArgError;
    }

    // 来源①：仅 --input → 位图即工作图（画布=图尺寸）。
    if (hasInput && !hasGen) return loadInputImage(args.get(opts::kInput), out, opt);

    // 来源②：仅生成选项 → 生成画布即工作图。
    if (!hasInput) {
        if (std::string err; !buildGeneratedImage(args, out, err)) {
            reportError(makeError(ExitCode::ArgError, IDC_CTR(msg, kErrGenArgs), err, IDC_CTR(msg, kHintGenArgs)));
            return ExitCode::ArgError;
        }
        if (opt.verbose)
            std::cerr << IDC_CTR(msg, kLogPrefix) << IDC_CTR(msg, kLogGenCanvas) << out.width() << "x" << out.height() << "\n";
        return ExitCode::Ok;
    }

    // 来源③：--input 与 --size 同给 → 画布尺寸由 --size 定（含图案 / 底色），位图居中铺入
    //   （缺省 native 原生尺寸；--fit crop/stretch/native 改为位图→画布适应）；--layer 仍可叠加其上。
    core::Image bmp;
    if (const ExitCode ec = loadInputImage(args.get(opts::kInput), bmp, opt); ec != ExitCode::Ok)
        return ec;
    if (std::string err; !buildGeneratedImage(args, out, err)) {
        reportError(makeError(ExitCode::ArgError, IDC_CTR(msg, kErrGenArgs), err, IDC_CTR(msg, kHintGenArgs)));
        return ExitCode::ArgError;
    }
    canvas::ImageLayer overlay;
    overlay.decodedPixels = std::move(bmp);
    overlay.zOrder = 0;
    if (args.has(opts::kFit)) {
        if (std::string err; !parseFit(args.get(opts::kFit), overlay.imageFit, err)) {
            reportError(makeError(ExitCode::ArgError, err));
            return ExitCode::ArgError;
        }
        overlay.hasImagePlacement = true;   // 位图→画布适应：框恒=画布、锚点 center。
        overlay.boxWidth = out.width();
        overlay.boxHeight = out.height();
    }
    const core::Image composed = canvas::composeCanvas({overlay}, out.width(), out.height(), out);
    if (composed.empty()) {
        reportError(makeError(ExitCode::RuntimeError, IDC_CTR(msg, kErrLayerCanvasEmpty),
                              IDC_CTRF(msg, kDetSourceSize, std::to_string(out.width()) + "x" + std::to_string(out.height())),
                              IDC_CTR(msg, kHintSourceBounds)));
        return ExitCode::RuntimeError;
    }
    out = composed;
    return ExitCode::Ok;
}

} // namespace idc::cli
