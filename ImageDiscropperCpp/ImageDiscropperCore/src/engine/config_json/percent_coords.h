// ============================================================================
// 文件：src/engine/config_json/percent_coords.h（Core 私有内部头，仅 percent_coords.cpp 与 engine_config_json.cpp 包含）
// 作用：百分比坐标的读侧归一化与保存侧回写接口声明——"50%" 类几何字段在解析前就地换算为像素、
//       保存时按载入记录原样回写 "N%"。与 schema 校验、codec 分置，序列化侧只依赖这三个入口。
// 分块依据：百分比坐标的换算涉及 JSON Pointer 记账、工作图基准推导与各段几何键白名单，自成一套
//       独立机制（往返契约见 core/percent.h 的 PercentHints）；独立成对文件后，engine_config_json.cpp
//       只保留「配置字段 ↔ JSON 键」的读写映射。本头只被 Core 内部 .cpp 以相对路径包含
//       （同 json_common.h 先例，绝不外泄公共头）。
// ============================================================================
#pragma once

#include <string>

#include "../../json_common.h" // jsonutil::json（私有内部类型，不入公共头）
#include "core/percent.h"   // core::PercentHints（百分比字段往返记录）

namespace idc::engine {

// 宽高基准（画布 / 工作图尺寸），坐标换算时 x 轴取宽、y 轴取高。
struct BoxSize {
    int width{0};
    int height{0};
};

// 整档百分比坐标预归一化（engineConfigFromJson 在 schema 守卫通过后、各段解析器运行前调用）：
//   把白名单几何键上的 "N%" 就地换成整数像素，令 rectFromJson / layerFromJson / annotation 段
//   解析器始终只见整数；每处换算记一条 PercentHint（JSON Pointer + 比例 + 像素），保存时据此
//   原样回写 "N%"（见 applyPercentHints）。失败返回 false，err 携点分路径定位报因。
bool resolvePercentCoords(jsonutil::json& root, core::PercentHints& hints, std::string& err);

// 保存侧回写（saveEngineConfig 在序列化后调用）：把「仍是载入时那个像素值、且基准尺寸未变」的
//   字段换回 "N%"（比例原样保留）；值被编辑过则写像素，路径失效（图层 / 标注被删、结构变化）则
//   跳过，基准变了（换图 / 新建画布改变 source 或预处理几何步）也不再按旧比例回写——
//   三者都为了绝不写出与当前尺寸不符的百分比。
void applyPercentHints(jsonutil::json& root, const core::PercentHints& hints);

// 段级入口的图层百分比换算（transform 四键，基准＝画布宽高）：就地换成像素，不记 PercentHints
//   （命令行传入的 token 不属于配置载入的往返记录，保存时即落像素）。与整档读侧 layers 段同口径：
//   x/width 用宽、y/height 用高；写百分比而基准未知（<= 0）→ 报错，不静默归零。
bool resolveLayerPercentInPlace(jsonutil::json& item, const BoxSize& canvas, const std::string& path, std::string& err);

} // namespace idc::engine
