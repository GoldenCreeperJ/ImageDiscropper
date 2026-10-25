// ============================================================================
// 文件：include/core/percent.h
// 作用：百分比字面量（"50%"）的解析、规范化与像素换算——§7 配置与 CLI 几何选项共用。
// 分块依据：三项都是纯函数、无状态、无第三方依赖，header-only 供 Core / CLI 同时使用；
//   「百分比 = 相对某个基准尺寸的比例」的换算口径只此一处，避免配置层与 CLI 各写一份而漂移。
// 说明：token 形如 "50%" / "-12.5%" / "100%"（可带符号与小数，必须以 % 结尾、不含空白）；
//   比例 = 百分数 / 100；换算按四舍五入（与 SPEC §2.1 交互坐标 round 同口径）。
//   PercentHint 记录「某字段以百分比书写」的原始比例与载入时换算出的像素值，供保存配置时
//   判断该字段是否仍未被编辑、从而原样回写百分比（而不是固化成像素）。
// ============================================================================
#pragma once

#include <charconv>
#include <cmath>
#include <cstddef>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace idc::core {

// 百分比 token 的长度上限（防超长输入；正常 "100%" / "-12.5%" 远小于此）。
inline constexpr std::size_t kPercentTokenMaxLen = 32;

// "N%" → 比例（"50%" → 0.5）；非百分比串 / 语法非法 / 非有限值 → nullopt。
inline std::optional<double> parsePercent(const std::string_view token) {
    if (token.size() < 2 || token.size() > kPercentTokenMaxLen || token.back() != '%') return std::nullopt;
    const std::string_view body = token.substr(0, token.size() - 1);
    if (body.empty()) return std::nullopt;
    double value = 0.0;
    const auto [ptr, ec] = std::from_chars(body.data(), body.data() + body.size(), value);
    if (ec != std::errc{} || ptr != body.data() + body.size() || !std::isfinite(value)) return std::nullopt;
    return value / 100.0;
}

// 比例 → 规范化 token（0.5 → "50%"、0.125 → "12.5%"、-0.25 → "-25%"）：定点 4 位小数、去尾零。
inline std::string formatPercent(const double ratio) {
    char buf[64];
    const double pct = ratio * 100.0;
    const auto [ptr, ec] = std::to_chars(buf, buf + sizeof(buf), pct, std::chars_format::fixed, 4);
    std::string s = ec == std::errc{} ? std::string(buf, ptr) : std::string("0");
    if (const auto dot = s.find('.'); dot != std::string::npos) {
        while (!s.empty() && s.back() == '0') s.pop_back();
        if (!s.empty() && s.back() == '.') s.pop_back();
    }
    if (s == "-0") s = "0";
    return s + "%";
}

// 比例 → 像素：四舍五入（远离零）；基准 <= 0 → 0；结果夹到 int 范围。
inline int resolvePercent(const double ratio, const int basis) {
    if (basis <= 0 || !std::isfinite(ratio)) return 0;
    const double px = std::round(ratio * static_cast<double>(basis));
    if (px <= static_cast<double>(std::numeric_limits<int>::min())) return std::numeric_limits<int>::min();
    if (px >= static_cast<double>(std::numeric_limits<int>::max())) return std::numeric_limits<int>::max();
    return static_cast<int>(px);
}

// 一条「百分比书写」记录：JSON Pointer 定位字段、原始比例、载入时的像素换算结果。
struct PercentHint {
    std::string pointer;    // 如 "/cut/params/x1"、"/layers/0/transform/x"、"/annotations/1/points/0/0"
    double ratio{0.0};      // 0.5 = 50%
    double resolved{0.0};   // 载入时按基准换算出的像素值（保存时据此判断字段是否被编辑）
};

using PercentHints = std::vector<PercentHint>;

} // namespace idc::core
