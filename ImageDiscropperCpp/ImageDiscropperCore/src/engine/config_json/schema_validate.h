// ============================================================================
// 文件：src/engine/config_json/schema_validate.h（Core 私有内部头，仅 schema_validate.cpp 与 engine_config_json.cpp 包含）
// 作用：SPEC §7 配置 schema 的读侧严格校验接口声明——校验实现与序列化本体（engine_config_json.cpp）
//       分置，序列化侧只依赖这两个入口，其余 check*/validate* 助手全部内聚于实现文件。
// 分块依据：严格校验自成一套完整判据（present-but-invalid 拦截 + 报因定位），与 codec 的字段映射
//       正交；独立成对文件后，engine_config_json.cpp 只保留「配置字段 ↔ JSON 键」的读写映射。
//       本头只声明两个被序列化侧调用的入口，且只被 Core 内部 .cpp 以相对路径包含
//       （同 json_common.h 先例，绝不外泄公共头）。
// ============================================================================
#pragma once

#include <string>

#include "../../json_common.h" // jsonutil::json（私有内部类型，不入公共头）

namespace idc::engine {

// 整档 schema 严格校验（读侧前置守卫，engineConfigFromJson 在宽容还原前调用）：
//   仅拦「键显式写出且值非 null」而类型 / 枚举 / 颜色非法的 present-but-invalid 情形，
//   首处失败即停，reason 携点分路径定位报因；缺失 / null 放行（交默认值路径）。
bool validateSchema(const jsonutil::json& root, std::string& reason);

// 单层段严格校验（§7 layers 元素语法，parseImageLayersFromJsonText 逐条调用，与整档同口径）：
//   path 为报因定位前缀（如 "layers[0]"）。失败返回 false，reason 写入定位报因。
bool validateLayerSeg(const jsonutil::json& lv, const std::string& path, std::string& reason);

} // namespace idc::engine
