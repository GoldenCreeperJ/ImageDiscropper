// ============================================================================
// 文件：include/engine/engine_config_json.h
// 作用：EngineConfig ↔ JSON 配置文件的双向存取（终稿 §9 schema / FR-L3.8 / NFR-4）。
//       把一次完整作业的配置序列化为可读、可复用的 JSON 文件，或从文件还原。
// 分块依据：对外只暴露“配置文件存取”（saveEngineConfig / loadEngineConfig）；JSON 语法
//       与“配置字段 ↔ JSON 键”的语义映射全部封装在实现文件内（使用 nlohmann/json），
//       故第三方 JSON 库不经此公共头扩散到下游（依赖 PRIVATE，与 stb 一致）。
// 说明：严格对齐 §9 schema——source / preprocess / cut / order / emit，外加 select
//       （L3 显式选择集 selectedCells）。枚举以短横线小写串表示（如 "row-major"）。
// ============================================================================
#pragma once

#include <string>

#include "engine/engine.h"

namespace idc::engine {

// 保存配置到文件（缩进美化）；path 为空或写盘失败（E-8）返回 false。
bool saveEngineConfig(const std::string& path, const EngineConfig& config);

// 从文件加载配置；path 为空、读盘失败或 JSON 解析失败返回 false。
bool loadEngineConfig(const std::string& path, EngineConfig& out);

} // namespace idc::engine
