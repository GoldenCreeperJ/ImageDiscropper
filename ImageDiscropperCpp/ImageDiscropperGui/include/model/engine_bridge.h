// ============================================================================
// 文件：model/engine_bridge.h
// 作用：GUI 访问 Core 切割引擎与预处理的统一桥接类（无状态）——切割/预处理调用集中在此。
//       切割/预处理相关 GUI 代码只与 EngineBridge 交互、不直接 include 引擎实现细节；标注域由
//       AnnotationBridge 驱动、坐标/图像转换由 util 适配器承担（详见 model/README.md，CONTRIBUTING.md「分层纪律」）。
// 分块依据：
//   - loadImage    委托 Core engine::readImageFile（解码）。
//   - runPreview   委托 Core engine::runEngine（仅区域数学，不搬像素，适合实时预览）。
//   - cutLines     委托 Core engine::generateCutLines（取得贯穿全图的切割线供渲染）。
//   - buildGrid    委托 Core Grid::build / induceGrid（取得诱导网格供 L3 画网格线与单元）。
//   - exportResult 委托 Core engine::runEngine + engine::exportImage（全分辨率落盘）。
// 说明：本类无状态、可拷贝；把 Core 的返回值原样透传给上层，错误文本转为 QString。
// ============================================================================
#pragma once

#include <QString>

#include <string>

#include "core/image.h"
#include "engine/engine.h"

namespace idc::gui {

// ---------------------------------------------------------------------------
// EngineBridge：切割引擎与预处理的 Core 桥接类（标注域见 AnnotationBridge）。
// ---------------------------------------------------------------------------
class EngineBridge {
public:
    // 从文件解码图像（委托 Core readImageFile，统一加载为 RGBA）。
    // 成功返回 true 并填充 out；失败返回 false 并给出中文错误 err。
    static bool loadImage(const QString& path, core::Image& out, QString& err);

    // 跑一次预览计算（委托 Core runEngine）。返回 EngineResult（含 kept / composition /
    // collapsible / ok / error）。runEngine 只做区域数学，不对大图搬像素，适合实时刷新。
    static engine::EngineResult runPreview(const core::Image& work,
                                           const engine::EngineConfig& cfg);

    // 由切割配置生成贯穿全图的切割线集合（委托 Core generateCutLines），供画布渲染。
    // GUI 不自算切割线几何——只把 Core 给出的 xs/ys 交给选区图元画成橙色贯穿线（CONTRIBUTING.md「分层纪律」）。
    static engine::CutLineSet cutLines(const engine::CutConfig& cut,
                                       const engine::SourceInfo& src);

    // 依切割配置产出诱导网格（供 L3 画网格线与单元选择）。与 runEngine 内部一致：
    // GRID 生成器走 Grid::build（处理余量策略），其余生成器走切割线诱导 induceGrid。
    // GUI 不自算网格几何——只把 Core 产出的 Grid 交给画布渲染（CONTRIBUTING.md「分层纪律」）。
    static engine::Grid buildGrid(const engine::CutConfig& cut,
                                  const engine::SourceInfo& src);

    // 导出（委托 Core runEngine + exportImage，对全分辨率工作图操作，无损）。
    // outputPath：分离模式为目标目录，合并模式为单图文件路径。
    // 成功返回 true；引擎失败或写盘失败返回 false 并给出中文错误 err。
    static bool exportResult(const core::Image& work, const engine::EngineConfig& cfg,
                             const QString& outputPath, QString& err);

    // ---- 配置文件存取（FR-L3.8 / NFR-4；委托 Core，GUI 不自实现 JSON，CONTRIBUTING.md「分层纪律」）----
    // 保存：把一次作业配置序列化为 SPEC §7 schema 的 JSON 文件（缩进美化）。
    // path 为空或写盘失败（Core E-8）返回 false 并给出中文错误 err。
    static bool saveConfig(const QString& path, const engine::EngineConfig& cfg, QString& err);
    // 加载：从 JSON 文件还原配置（SPEC §7 schema）。path 为空、读盘失败或 JSON 解析失败返回 false
    // 并给出中文错误 err；成功返回 true 并填充 out（由上层 Document::applyEngineConfig 反向映射）。
    static bool loadConfig(const QString& path, engine::EngineConfig& out, QString& err);

    // ---- 预处理（FR-1；委托 Core pixel_ops::*，GUI 不自实现像素运算，CONTRIBUTING.md「分层纪律」）----
    // 每个方法对输入图做一次变换并返回新图（不修改入参）；上层据此刷新 Document 工作图。
    // 说明：本 GUI 采用「即时累积」预处理——每次操作把工作图变换后写回，原图始终保留
    //       供「重置预处理」还原（CONTRIBUTING.md「分层纪律」 流水线：原图 → 预处理 → 切割 → 导出）。
    static core::Image rotateImage(const core::Image& src, int angleDeg);      // 旋转 90/180/270
    static core::Image flipImage(const core::Image& src, bool horizontal);     // 水平/垂直翻转
    static core::Image resizeImage(const core::Image& src, int newW, int newH); // 目标尺寸缩放
    static core::Image scaleImage(const core::Image& src, double factor);      // 按比例缩放
    static core::Image toGrayImage(const core::Image& src);                    // 黑白（灰度）
    static core::Image invertImage(const core::Image& src, const std::string& mask); // 按通道反色
    static core::Image splitImage(const core::Image& src, const std::string& mask);  // 按通道分离
};

} // namespace idc::gui
