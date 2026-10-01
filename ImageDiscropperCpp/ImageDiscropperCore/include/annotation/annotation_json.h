// ============================================================================
// 文件：include/annotation/annotation_json.h
// 作用：标注 JSON 的公共入口（SPEC §7 annotations 段元素语法）——把标注文本解析为 Annotation 列表。
// 分块依据：标注的 JSON 序列化 / 反序列化实现归属 annotation 模块（src/annotation/annotation_json.cpp），
//       不再挂在 engine 下；engine_config_json 组装整份 §7 文档时经私有内部头调用本模块的段级实现
//       （annotationsSegmentToJson / annotationsFromSegmentJson），公共签名 saveEngineConfig /
//       loadEngineConfig 不受影响。本头只暴露 std 级 API，不泄漏第三方 JSON 类型（对齐「nlohmann 不进公共头」纪律）。
// ============================================================================
#pragma once

#include <string>
#include <vector>

#include "annotation/annotation_set.h" // annotation::Annotation（完整类型，供 vector 实例化）

namespace idc::annotation {

// 解析标注 JSON 文本（SPEC §7 annotations 段元素语法，与整档存取共用同一套重建逻辑）：
//   根为对象＝单条标注；根为数组＝多条标注。逐条重建并**追加**到 out（调用方可多次累积）。
//   强制校验：逐条重建，任一条非法即整体失败（不静默丢弃、out 保持不变），err 逐条列出序号与原因；
//   几何由 type 分派：双锚点图元（约束多边形：矩形/正方形/菱形/四种三角 + 圆/椭圆）经 buildShape(p1,p2) 派生、恒合法；line/polyline 直连顶点、path/text 直连参数。
//   用途：命令行内联 --annotation '{...}'（单条对象）与 --annotations file.json（根为数组）的段级解析。
bool parseAnnotationsFromJsonText(const std::string& text,
                                  std::vector<Annotation>& out,
                                  std::string& err);

} // namespace idc::annotation
