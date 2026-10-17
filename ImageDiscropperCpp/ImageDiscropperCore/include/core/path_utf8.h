// ============================================================================
// 文件：include/core/path_utf8.h
// 作用：UTF-8 std::string ↔ std::filesystem 宽路径互转助手（Core / CLI 共用）。
// 说明：全仓路径契约是「std::string 按 UTF-8 解析」；char8_t 与 char 之间字节同一，
//       经 reinterpret_cast 互转不影响路径编解码（std::filesystem::path 的 char8_t
//       构造与 u8string() 均按 UTF-8 解释）。
// 分块依据：与 core_nums / core_texts 同族的基础契约头；GUI 层不涉及（Qt 自管路径编码）。
// ============================================================================
#pragma once

#include <filesystem>
#include <string>
#include <string_view>

namespace idc::core {

// UTF-8 字节串 → filesystem 路径（宽路径按 UTF-8 解析，中文 / 非 ASCII 可正确定位）。
inline std::filesystem::path pathFromUtf8(const std::string_view utf8) {
    const auto* p = reinterpret_cast<const char8_t*>(utf8.data());
    return {p, p + utf8.size()};
}

// filesystem 路径 → UTF-8 std::string（与全仓「路径按 UTF-8」契约对齐）。
inline std::string pathToUtf8(const std::filesystem::path& path) {
    const std::u8string s = path.u8string();
    return std::string(reinterpret_cast<const char*>(s.data()), s.size());
}

} // namespace idc::core
