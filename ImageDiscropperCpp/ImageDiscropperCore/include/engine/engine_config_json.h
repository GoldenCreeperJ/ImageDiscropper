// ============================================================================
// 文件：include/engine/engine_config_json.h
// 作用：EngineConfig ↔ JSON 配置文件的双向存取（SPEC §7 schema / FR-L3.8 / NFR-4）。
//       把一次完整作业的配置序列化为可读、可复用的 JSON 文件，或从文件还原。
// 分块依据：对外只暴露“配置文件存取”（saveEngineConfig / loadEngineConfig）；JSON 语法
//       与“配置字段 ↔ JSON 键”的语义映射全部封装在实现文件内（使用 nlohmann/json），
//       故第三方 JSON 库不经此公共头扩散到下游（依赖 PRIVATE，与 stb 一致）。
// 说明：严格对齐 SPEC §7 schema——source / preprocess / cut / order / emit，外加 select
//       （L3 显式选择集 selectedCells）与 annotations（标注列表，见下）。枚举以短横线小写串表示（如 "row-major"）。
//       标注不经 EngineConfig 内嵌（engine 与 annotation 模块保持解耦），而是由 save/load 的
//       可选参数传递 AnnotationSet：nullptr（默认）完全忽略 annotations 段，向后兼容。
//       annotations 段的 JSON 实现归属 annotation 模块（见 annotation/annotation_json.h），本头只负责整档 config 存取。
// ============================================================================
#pragma once

#include <string>
#include <vector>

#include "engine/engine.h"

namespace idc::annotation { class AnnotationSet; }

namespace idc::engine {

// 保存到文件（缩进美化）；path 为空或写盘失败（E-8）返回 false。
// annotations 非空时额外写出 SPEC §7 annotations 段（逐条含几何/样式/变换）；集合为空时省略该段。
bool saveEngineConfig(const std::string& path, const EngineConfig& config,
                      const annotation::AnnotationSet* annotations = nullptr);

// 从文件加载配置；path 为空、读盘失败或 JSON 解析失败返回 false。
// annotations 非空时同步还原标注：文件含 annotations 段则整体替换集合内容（restore），
// 不含则清空；传 nullptr 时标注段被忽略（配置字段照常加载）。
// 标注段采用严格校验：任一元素非法即整体载入失败；annoErr 非空时逐条回传非法原因（供 CLI 报因）。
bool loadEngineConfig(const std::string& path, EngineConfig& out,
                      annotation::AnnotationSet* annotations = nullptr,
                      std::string* annoErr = nullptr);

} // namespace idc::engine
