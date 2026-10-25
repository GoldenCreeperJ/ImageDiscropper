// ============================================================================
// 文件：include/engine/engine_config_json.h
// 作用：EngineConfig ↔ JSON 配置文件的双向存取（SPEC §7 schema / FR-L3.8 / NFR-4）。
//       把一次完整作业的配置序列化为可读、可复用的 JSON 文件，或从文件还原。
// 分块依据：对外只暴露“配置文件存取”（saveEngineConfig / loadEngineConfig）；JSON 语法
//       与“配置字段 ↔ JSON 键”的语义映射全部封装在实现文件内（使用 nlohmann/json），
//       故第三方 JSON 库不经此公共头扩散到下游（依赖 PRIVATE，与 stb 一致）。
// 说明：严格对齐 SPEC §7 schema——source / opacity / preprocess / cut / select / order / export，
//       外加可选 generate（底图）/ layers（多层画布）/ annotations（标注列表，见下）。枚举以短横线小写串表示（如 "row-major"）。
//       标注不经 EngineConfig 内嵌（engine 与 annotation 模块保持解耦），而是由 save/load 的
//       可选参数传递 AnnotationSet：nullptr（默认）完全忽略 annotations 段，向后兼容。
//       annotations 段的 JSON 实现归属 annotation 模块（见 annotation/annotation_json.h），本头只负责整档 config 存取。
// ============================================================================
#pragma once

#include <expected>
#include <string>
#include <vector>

#include "canvas/layer.h"     // canvas::ImageLayer（parseImageLayersFromJsonText 的解析产物类型）
#include "core/core_error.h"  // core::CoreError（配置存取失败侧载荷）
#include "core/percent.h"     // core::PercentHints（百分比字段的往返记录）
#include "engine/engine.h"

namespace idc::annotation { class AnnotationSet; }

namespace idc::engine {

// 保存到文件（缩进美化）；成功返回空 expected。path 为空（PathEmpty）或写盘失败（Io，E-8）返回错误。
// annotations 非空时额外写出 SPEC §7 annotations 段（逐条含几何/样式/变换）；集合为空时省略该段。
// percentHints 非空时把仍未被编辑的百分比字段原样写回 "N%"（见 core/percent.h）：载入时记录的
//   像素值若与当前配置一致则保留比例，被 GUI/CLI 改过则写像素；路径失效（层/标注被删）则跳过。
std::expected<void, core::CoreError> saveEngineConfig(const std::string& path, const EngineConfig& config,
                                                      const annotation::AnnotationSet* annotations = nullptr,
                                                      const core::PercentHints* percentHints = nullptr);

// 从文件加载配置；成功返回空 expected（out 仍经引用回填）。
// path 为空（PathEmpty）、读盘失败（Io）、JSON 解析/结构失败（ConfigParseFailed）或标注段非法（AnnotationInvalid）返回错误。
// annotations 非空时同步还原标注：文件含 annotations 段则整体替换集合内容（restore），
// 不含则清空；传 nullptr 时标注段被忽略（配置字段照常加载）。
// 标注段采用严格校验：任一元素非法即整体载入失败；annoErr 非空时逐条回传非法原因（供 CLI 报因）。
// percentHints 非空时收集本档以 "N%" 书写的几何字段（路径 / 比例 / 换算像素），供保存时原样回写。
std::expected<void, core::CoreError> loadEngineConfig(const std::string& path, EngineConfig& out,
                                                      annotation::AnnotationSet* annotations = nullptr,
                                                      std::string* annoErr = nullptr,
                                                      core::PercentHints* percentHints = nullptr);

// 段级解析：把一段「图层 JSON」（§7 layers 元素语法，根为单对象或对象数组）解析为 ImageLayer 列表
// 追加到 out（与 annotation 模块的 parseAnnotationsFromJsonText 同层同形，供 CLI 命令行 --layer/--layers 复用）。
// transform 的 x/y/width/height 可写百分比串（基准＝画布宽高，与 §7 layers 段同口径）：给定
// basisWidth/basisHeight（缺省 0＝基准未知）时先换算为像素再还原；写百分比而基准未知（<= 0）报错而非归零。
// 文本为空 / 根非对象非数组 / 解析不出有效图层 → 返回 false 并回传原因（JSON 语法错误直接透传 parse_error 详情）。
bool parseImageLayersFromJsonText(const std::string& text, std::vector<canvas::ImageLayer>& out,
                                  std::string& err, int basisWidth = 0, int basisHeight = 0);

} // namespace idc::engine
