// ============================================================================
// 文件：canvas/canvas_scene_internal.h（GUI 私有内部头，仅 canvas_scene 各 TU 包含）
// 作用：CanvasScene 拆分 TU 共用的两个几何助手（全部 inline）——nearest（候选坐标集取最近值）、
//       cellSceneRect（单元区域 → 场景矩形）。
// 分块依据：canvas_scene 按图元族拆五个 TU（主件 + _annotations / _rects / _grid / _layers），
//       切割线关联（主件）与网格单元点选（_grid）两处都要这两个助手；集中于此避免拷贝漂移
//       （仿 param_panel_internal.h 先例，不经 include/ 外泄）。
// ============================================================================
#pragma once

#include <cstdlib>
#include <vector>

#include <QRectF>

#include "engine/engine.h"   // engine::Cell（单元点选图元的几何来源）

namespace idc::gui {

// 在候选坐标集 vals 中取最接近 target 者；vals 为空返回 -1。
// 用于把 Core 的切割线（xs/ys 含 0/W 边界）关联到最近的选区边。
inline int nearest(const std::vector<int>& vals, const int target) {
    int best = -1;
    int bestD = -1;
    for (const int v : vals) {
        if (const int d = std::abs(v - target); bestD < 0 || d < bestD) { bestD = d; best = v; }
    }
    return best;
}

// 单元区域 → 场景矩形（原图像素坐标；RectRegion 左闭右开，width/height 即跨度）。
inline QRectF cellSceneRect(const engine::Cell& c) {
    return {static_cast<qreal>(c.area.left), static_cast<qreal>(c.area.top),
            static_cast<qreal>(c.area.width()), static_cast<qreal>(c.area.height())};
}

} // namespace idc::gui
