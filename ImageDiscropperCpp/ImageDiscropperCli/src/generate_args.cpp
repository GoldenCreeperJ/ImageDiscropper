// ============================================================================
// 文件：src/generate_args.cpp
// 作用：generate_args.h 的实现——读取 --size / --pattern / --orientation / --bg / --fg /
//       --period / --line-width / --dot-radius 选项，装配 Core generate::PatternParams 并调
//       generate::makePattern 合成画布；另提供 resolveInputImage 统一「读文件 / 生成」两种输入来源。
//       「贴图图案」已移除：CLI 不再有 --pattern image / --pattern-image / --image-anchor /
//       --image-scale；图片层只能经 config 的 layers 段传入。
// 分块依据：CLI 不实现任何像素 / 图案逻辑，全部委托 Core（CONTRIBUTING.md「分层纪律」）；本文件只做
//       「取选项 + 纯格式校验 + 调 Core + 汇总错误」。颜色解析复用 value_parser::parseColor（含 alpha）。
// ============================================================================
#include "generate_args.h"

#include <cerrno>
#include <cstdlib>
#include <iostream>
#include <string>

#include "generate/pattern.h" // generate::PatternParams / makePattern / kMaxDimension
#include "value_parser.h"     // parseColor（底色 / 前景色，含 alpha）

#include "cli_error.h"

namespace idc::cli {
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
    for (const char c : s) low.push_back(static_cast<char>((c >= 'A' && c <= 'Z') ? c - 'A' + 'a' : c));
    const std::size_t x = low.find('x');
    if (x == std::string::npos) { err = "尺寸须为 WxH 形式（如 1024x768）：'" + s + "'"; return false; }
    int a = 0, b = 0;
    if (!toStrictPositive(s.substr(0, x), a) || !toStrictPositive(s.substr(x + 1), b)) {
        err = "尺寸 WxH 须为两个正整数：'" + s + "'";
        return false;
    }
    if (a > generate::kMaxDimension || b > generate::kMaxDimension) {
        err = "尺寸单边超出上限（最大 " + std::to_string(generate::kMaxDimension) + "）：'" + s + "'";
        return false;
    }
    if (static_cast<long long>(a) * static_cast<long long>(b) > generate::kMaxTotalPixels) {
        err = "总像素超出上限（宽×高 ≤ " + std::to_string(generate::kMaxTotalPixels) + "）：'" + s + "'";
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
    for (const char c : s) v.push_back(static_cast<char>((c >= 'A' && c <= 'Z') ? c - 'A' + 'a' : c));
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
        err = "不支持的图案类型：'" + s +
              "'（可选 solid/linear-gradient/radial-gradient/checkerboard/stripes/grid-lines/dot-grid/" +
              "concentric/radial-rays/hue-gradient/noise）";
        return false;
    }
    return true;
}

// 解析方向名 → Orientation（大小写不敏感，接受 horizontal/vertical/diagonal-down/diagonal-up 及缩写 h/v）。
bool parseOrientation(const std::string& s, generate::Orientation& out, std::string& err) {
    std::string v;
    v.reserve(s.size());
    for (const char c : s) v.push_back(static_cast<char>((c >= 'A' && c <= 'Z') ? c - 'A' + 'a' : c));
    if (v == "horizontal" || v == "h") out = generate::Orientation::Horizontal;
    else if (v == "vertical" || v == "v") out = generate::Orientation::Vertical;
    else if (v == "diagonal-down" || v == "diagonal" || v == "d") out = generate::Orientation::DiagonalDown;
    else if (v == "diagonal-up" || v == "diag-up") out = generate::Orientation::DiagonalUp;
    else {
        err = "不支持的方向：'" + s + "'（可选 horizontal/vertical/diagonal-down/diagonal-up）";
        return false;
    }
    return true;
}

// 解析噪声配色模式 → NoiseMode（大小写不敏感，仅 --pattern noise 时有义）。
bool parseNoiseMode(const std::string& s, generate::NoiseMode& out, std::string& err) {
    std::string v;
    v.reserve(s.size());
    for (const char c : s) v.push_back(static_cast<char>((c >= 'A' && c <= 'Z') ? c - 'A' + 'a' : c));
    if (v == "full-color" || v == "color") out = generate::NoiseMode::FullColor;
    else if (v == "grayscale" || v == "gray") out = generate::NoiseMode::Grayscale;
    else if (v == "two-color" || v == "two") out = generate::NoiseMode::TwoColor;
    else {
        err = "不支持的噪声模式：'" + s + "'（可选 full-color/grayscale/two-color）";
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
    return args.has("--size");
}

// 仅解析尺寸 + 图案参数为 PatternParams（不落像素）；new --save-config 与 buildGeneratedImage 共用。
bool parseGenerateParams(const ArgParser& args, int& w, int& h,
                         generate::PatternParams& params, std::string& err) {
    // --- 尺寸：必填（触发合成的前提）。---
    if (!args.has("--size")) {
        err = "缺少 --size，无法生成画布";
        return false;
    }
    if (!parseSize(args.get("--size"), w, h, err)) return false;

    // --- 装配 PatternParams（缺省与 Core 默认一致）。---
    if (args.has("--pattern") && !parsePatternKind(args.get("--pattern"), params.kind, err)) return false;
    if (args.has("--orientation") && !parseOrientation(args.get("--orientation"), params.orientation, err)) return false;
    if (args.has("--bg") && !parseColor(args.get("--bg"), params.background, err)) return false;
    if (args.has("--fg") && !parseColor(args.get("--fg"), params.foreground, err)) return false;
    if (args.has("--period") && !toStrictPositive(args.get("--period"), params.period)) {
        err = "--period 须为正整数：'" + args.get("--period") + "'";
        return false;
    }
    if (args.has("--line-width") && !toStrictPositive(args.get("--line-width"), params.lineWidth)) {
        err = "--line-width 须为正整数：'" + args.get("--line-width") + "'";
        return false;
    }
    if (args.has("--dot-radius") && !toStrictPositive(args.get("--dot-radius"), params.dotRadius)) {
        err = "--dot-radius 须为正整数：'" + args.get("--dot-radius") + "'";
        return false;
    }
    if (args.has("--seed") && !toNonNegative(args.get("--seed"), params.seed)) {
        err = "--seed 须为非负整数：'" + args.get("--seed") + "'";
        return false;
    }
    if (args.has("--noise") && !parseNoiseMode(args.get("--noise"), params.noiseMode, err)) return false;
    return true;
}

bool buildGeneratedImage(const ArgParser& args, core::Image& out, std::string& err) {
    int width = 0, height = 0;
    generate::PatternParams params;
    if (!parseGenerateParams(args, width, height, params, err)) return false;

    // --- 委托 Core 生成；非法尺寸或越界会产空图（防御性再确认）。---
    out = generate::makePattern(width, height, params);
    if (out.empty()) {
        err = "图案生成失败（尺寸或参数非法）";
        return false;
    }
    return true;
}

ExitCode resolveInputImage(const ArgParser& args, core::Image& out, const JobOptions& opt) {
    // 来源①：--input 读文件（复用 job 的执行桥，失败已打印，退出码 3）。
    if (args.has("--input")) return loadInputImage(args.get("--input"), out, opt);

    // 来源②：用生成选项合成画布（失败归参数错误 1）。
    std::string err;
    if (!buildGeneratedImage(args, out, err)) {
        reportError(makeError(ExitCode::ArgError, "图像生成参数错误", err,
                              "请检查 --size / --pattern / --orientation / --bg / --fg / --period / --seed / --noise 等选项"));
        return ExitCode::ArgError;
    }
    if (opt.verbose)
        std::cerr << "idc: 已生成画布输入 " << out.width() << "x" << out.height() << "\n";
    return ExitCode::Ok;
}

} // namespace idc::cli
