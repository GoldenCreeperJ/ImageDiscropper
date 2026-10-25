// ============================================================================
// 文件：src/json_common.h（Core 私有内部头，不入库 include/、不经任何公共头对外）
// 作用：engine 与 annotation 两模块共用的 nlohmann/json 宽容读取助手，以及二者之间
//       「SPEC §7 annotations 段 ↔ JSON 片段」的跨模块交换接口声明。
// 分块依据：
//   · 助手（at/jInt/jStr/jBool/jDouble/colorToHex/colorFromHex）为「配置字段映射」与
//     「标注序列化」共同依赖，集中此处 inline 定义，避免两模块各写一份造成口径漂移——
//     容差 / 缺省值 / 十六进制解析语义直接决定旧配置文件的向后兼容，须单一真相源。
//   · 两个片段函数把「标注段」的读写实现留给 annotation 模块（见 src/annotation/annotation_json.cpp），
//     engine_config_json 仅在组装 / 解析整份 §7 文档时调用之——数据层解耦在代码层收口。
//   · 本头内含 <nlohmann/json.hpp>，故只可被 Core 内部 .cpp 以相对路径（"../json_common.h"）包含，
//     绝不经公共头（include/）外泄给下游（依赖单向纪律同 CMakeLists 注释）。
// ============================================================================
#pragma once

#include <nlohmann/json.hpp>

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>

#include "core/color.h"

namespace idc::annotation {
class AnnotationSet;
}

namespace idc::jsonutil {

using json = nlohmann::ordered_json; // 保插入序：序列化按写入顺序输出，§7 落盘键序可控（整棵配置树共用此单一类型）。

// ---------------------------------------------------------------------------
// 颜色 ↔ "#AARRGGBB"（SPEC §7 示例 "#00000000" 即全透明）。
// ---------------------------------------------------------------------------
inline std::string colorToHex(const core::Color& c) {
    char buf[16];
    std::snprintf(buf, sizeof(buf), "#%02X%02X%02X%02X", c.a, c.r, c.g, c.b);
    return std::string(buf);
}

// 解析两位十六进制；越界或非法返回 fallback。
inline int hex2(const std::string& s, const std::size_t pos, const int fallback) {
    if (pos + 2 > s.size()) return fallback;
    int v = 0;
    for (int i = 0; i < 2; ++i) {
        const char c = s[pos + static_cast<std::size_t>(i)];
        v <<= 4;
        if (c >= '0' && c <= '9') v |= c - '0';
        else if (c >= 'a' && c <= 'f') v |= c - 'a' + 10;
        else if (c >= 'A' && c <= 'F') v |= c - 'A' + 10;
        else return fallback;
    }
    return v;
}

inline core::Color colorFromHex(const std::string& str) {
    std::string h = str;
    if (!h.empty() && h[0] == '#') h.erase(0, 1);
    if (h.size() >= 8) { // AARRGGBB
        return core::Color(static_cast<std::uint8_t>(hex2(h, 2, 0)),
                           static_cast<std::uint8_t>(hex2(h, 4, 0)),
                           static_cast<std::uint8_t>(hex2(h, 6, 0)),
                           static_cast<std::uint8_t>(hex2(h, 0, 255)));
    }
    if (h.size() >= 6) { // RRGGBB（不透明）
        return core::Color(static_cast<std::uint8_t>(hex2(h, 0, 0)),
                           static_cast<std::uint8_t>(hex2(h, 2, 0)),
                           static_cast<std::uint8_t>(hex2(h, 4, 0)),
                           255);
    }
    return core::kTransparent;
}

// 颜色串严格校验（供 engine §7 schema 与 annotation 标注段共用的唯一口径）：
//   # 可省；长度须为 6（RRGGBB）或 8（AARRGGBB），且全为十六进制字符。与 colorFromHex 的宽容
//   解析互补——colorFromHex 坏值落默认，本函数判「写出的值是否合法」，用于严格报因。
inline bool isValidHexColor(const std::string& raw) {
    const std::string h = !raw.empty() && raw[0] == '#' ? raw.substr(1) : raw;
    if (h.size() != 6 && h.size() != 8) return false;
    for (const char c : h) {
        if (const bool ok = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'); !ok) return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// 宽容读取助手：nlohmann 的 const operator[] 对缺失键会断言，故统一经 at() 取子元素，
//       再按类型安全取值；缺失或类型不符时返回给定默认值。
// ---------------------------------------------------------------------------
// 安全取对象子元素：obj 非对象或键缺失时返回静态 null 单例（避免断言/悬垂）。
inline const json& at(const json& obj, const char* key) {
    static const json kNull = nullptr;
    if (!obj.is_object()) return kNull;
    const auto it = obj.find(key);
    return it == obj.end() ? kNull : *it;
}

// 整数读取：schema 层 checkInt 已在入参处拒绝小数，此处不再承担业务级宽容；
// 浮点分支仅为绕过 schema 的直接调用兜底（向零截断），其余（含 null/缺失）取默认。
inline int jInt(const json& v, const int def) {
    if (v.is_number_integer()) return v.get<int>();                    // 整数直接取。
    if (v.is_number_float()) return static_cast<int>(v.get<double>()); // 浮点截断。
    return def;                                                        // 其余（含 null/缺失）取默认。
}

inline std::string jStr(const json& v, const char* def) {
    return v.is_string() ? v.get<std::string>() : std::string(def);
}

inline bool jBool(const json& v, const bool def) {
    return v.is_boolean() ? v.get<bool>() : def;
}

inline double jDouble(const json& v, const double def) {
    return v.is_number() ? v.get<double>() : def; // 整数/浮点统一取 double。
}

} // namespace idc::jsonutil

namespace idc::annotation {

// 标注段 ↔ JSON 片段（跨模块私有交换，nlohmann 类型不经公共头外泄；实现见 src/annotation/annotation_json.cpp）：
//   · annotationsSegmentToJson：集合内标注 → SPEC §7 annotations 数组（逐条经标注序列化，无形状 / 未知类跳过）。
//   · annotationsFromSegmentJson：annotations 数组值 → 集合（逐条严格校验，任一非法写 annoErr 返回 false；
//     段缺失 / 非数组 → 清空集合，与「配置文件为标注真相源」一致；成功则按 zOrder 稳定排序后 restore）。
// engine_config_json 组装 / 解析整份 §7 文档时调用，二者的实现均落在 annotation 模块，使标注 JSON 归属收口于 annotation。
jsonutil::json annotationsSegmentToJson(const AnnotationSet& set);
bool annotationsFromSegmentJson(const jsonutil::json& arrValue, AnnotationSet& set, std::string* annoErr);

} // namespace idc::annotation
