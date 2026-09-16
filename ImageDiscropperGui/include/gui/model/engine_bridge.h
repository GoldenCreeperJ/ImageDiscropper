// ============================================================================
// 文件：model/engine_bridge.h
// 作用：GUI 与 Core 之间唯一的桥接类——所有对 Core API 的调用都集中在此。
//       其余 GUI 代码（面板/画布/主窗口）只与 EngineBridge 交互，不直接 include
//       引擎实现细节，保证「GUI 不含切割/几何/导出逻辑」（guideline §0 A-0.1/A-0.3）。
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

#include "core/image.h"
#include "engine/engine.h"

namespace idc::gui {

// ---------------------------------------------------------------------------
// EngineBridge：唯一触碰 Core 的类。
// ---------------------------------------------------------------------------
class EngineBridge {
public:
    // 从文件解码图像（委托 Core readImageFile，统一加载为 RGBA）。
    // 成功返回 true 并填充 out；失败返回 false 并给出中文错误 err。
    bool loadImage(const QString& path, idc::core::Image& out, QString& err) const;

    // 跑一次预览计算（委托 Core runEngine）。返回 EngineResult（含 kept / composition /
    // collapsible / ok / error）。runEngine 只做区域数学，不对大图搬像素，适合实时刷新。
    idc::engine::EngineResult runPreview(const idc::core::Image& work,
                                         const idc::engine::EngineConfig& cfg) const;

    // 由切割配置生成贯穿全图的切割线集合（委托 Core generateCutLines），供画布渲染。
    // GUI 不自算切割线几何——只把 Core 给出的 xs/ys 交给选区图元画成橙色贯穿线（A-0.1）。
    idc::engine::CutLineSet cutLines(const idc::engine::CutConfig& cut,
                                     const idc::engine::SourceInfo& src) const;

    // 依切割配置产出诱导网格（供 L3 画网格线与单元选择）。与 runEngine 内部一致：
    // GRID 生成器走 Grid::build（处理余量策略），其余生成器走切割线诱导 induceGrid。
    // GUI 不自算网格几何——只把 Core 产出的 Grid 交给画布渲染（A-0.1）。
    idc::engine::Grid buildGrid(const idc::engine::CutConfig& cut,
                                const idc::engine::SourceInfo& src) const;

    // 导出（委托 Core runEngine + exportImage，对全分辨率工作图操作，无损）。
    // outputPath：分离模式为目标目录，合并模式为单图文件路径。
    // 成功返回 true；引擎失败或写盘失败返回 false 并给出中文错误 err。
    bool exportResult(const idc::core::Image& work, const idc::engine::EngineConfig& cfg,
                      const QString& outputPath, QString& err) const;
};

} // namespace idc::gui
