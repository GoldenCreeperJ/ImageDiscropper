// ============================================================================
// 文件：model/config_codec.h
// 作用：ConfigCodec——Document ↔ EngineConfig 的字段映射（buildEngineConfig /
//       buildSerializableConfig / applyEngineConfig）。与 Document 深度互操作（直接读写
//       私有状态），故为 Document 的 friend。
// 分块依据：配置组装/还原是「纯字段搬运 + 枚举映射 + 不变式守卫」，无状态；Document
//       仅保留三行转发，避免状态聚合器职责膨胀（CONTRIBUTING.md「分层纪律」）。
// ============================================================================
#pragma once

#include "engine/engine.h"

namespace idc::gui {

class Document;

class ConfigCodec {
public:
    // 运行配置（喂 Core runEngine）：源尺寸取工作图、切割/选择/排序/导出字段搬运，
    // preprocess 恒空（预处理已即时作用于工作图）。
    static engine::EngineConfig buildEngineConfig(const Document& doc);
    // 可序列化配置（§7 保存/载入）：同 buildEngineConfig 字段，但 preprocess 段填入
    // 画布级完整有效管线（[resize?] + 轻操作队列）、source 取原图/画布声明尺寸、
    // 携底图声明（generate 段）与叠加图层声明（layers 段，位图剥离）。
    static engine::EngineConfig buildSerializableConfig(const Document& doc);
    // 反向映射（配置加载与撤销重做共用，与 buildEngineConfig 互逆）：直接改私有字段后
    // 只发一次 changed()，避免逐字段多次触发刷新；图像/source 尺寸不还原。
    static void applyEngineConfig(Document& doc, const engine::EngineConfig& cfg);
};

} // namespace idc::gui
