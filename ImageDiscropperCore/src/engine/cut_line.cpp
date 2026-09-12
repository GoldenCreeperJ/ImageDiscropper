// ============================================================================
// 文件：src/engine/cut_line.cpp
// 作用：实现切割线阶段（Grid-Selection-Emit 阶段①）——切割线归一化 normalizeCutLines
//       与由切割配置生成贯穿全图切割线的 generateCutLines（终稿 §2 / §4.2 / §4.3 / §4.4）。
// 分块依据：切割线是“贯穿全图的直线”这一公理的载体；本文件只负责线的生成与归一化，
//       网格铺设见 grid.cpp，二者解耦。
// ============================================================================
#include "engine/engine.h"

#include <algorithm>

namespace idc::engine {

// 归一化切割线集合：对 xs / ys 分别裁剪到 [0,limit]、补入边界 0 与 limit、
// 去重并升序（终稿 §2 阶段① 约定 + §6 越界自动裁剪，对应 E-2）。
void normalizeCutLines(CutLineSet& lines, const int width, const int height) {
    // 单轴归一化：clamp 到 [0,limit] → 补边界 → 排序 → 去重。
    const auto cleanAxis = [](std::vector<int>& v, const int limit) {
        for (int& x : v) x = std::clamp(x, 0, limit);
        v.push_back(0);
        v.push_back(limit);
        std::sort(v.begin(), v.end());
        v.erase(std::unique(v.begin(), v.end()), v.end());
    };
    cleanAxis(lines.xs, width);
    cleanAxis(lines.ys, height);
}

// 由切割配置生成贯穿全图的切割线集合（阶段①）。
// 五种生成器共用同一“产线 → 归一化”路径，仅产线规则不同（NFR-0：模式即参数预设）。
CutLineSet generateCutLines(const CutConfig& cut, const SourceInfo& source) {
    CutLineSet lines;
    const int W = source.width;
    const int H = source.height;

    switch (cut.generator) {
        case CutGenerator::RECT: {
            // 单矩形诱导十字切割线：X={0,x1,x2,W}, Y={0,y1,y2,H}（§4.2/§4.3.4）。
            const RectRegion& r = cut.rect;
            // E-3：x1==x2 退化 → 不产竖线，降级为横线切割；E-1：x1>x2 非法 → 不产竖切割。
            if (r.left < r.right) { lines.xs.push_back(r.left); lines.xs.push_back(r.right); }
            if (r.top < r.bottom) { lines.ys.push_back(r.top); lines.ys.push_back(r.bottom); }
            break;
        }
        case CutGenerator::HORIZONTAL_LINE: {
            // 仅两条横线（横带保留/剔除）：Y={0,y1,y2,H}，X 保持 {0,W}（§4.2 横线）。
            const RectRegion& r = cut.rect;
            if (r.top < r.bottom) { lines.ys.push_back(r.top); lines.ys.push_back(r.bottom); }
            break;
        }
        case CutGenerator::VERTICAL_LINE: {
            // 仅两条竖线（竖带保留/剔除）：X={0,x1,x2,W}，Y 保持 {0,H}（§4.2 竖线）。
            const RectRegion& r = cut.rect;
            if (r.left < r.right) { lines.xs.push_back(r.left); lines.xs.push_back(r.right); }
            break;
        }
        case CutGenerator::MULTI_RECT: {
            // 方案 A（§4.3.6）：k 个矩形各取 x1/x2、y1/y2 汇入线集，删除区为十字带并集，
            // 与单矩形严格同构，保持“按线切割”公理。
            for (const RectRegion& r : cut.rects) {
                if (r.left < r.right) { lines.xs.push_back(r.left); lines.xs.push_back(r.right); }
                if (r.top < r.bottom) { lines.ys.push_back(r.top); lines.ys.push_back(r.bottom); }
            }
            break;
        }
        case CutGenerator::GRID: {
            // 参数化网格（L3）：由基准点 + 单元尺寸 + 间距推导单元左右/上下边界线。
            // 注：runEngine 对 GRID 直接走 Grid::build（可处理余量策略），此处产线供
            //     UI 显示切割线或一致性校验之用。
            const GridParams& g = cut.grid;
            const int stepX = g.cellWidth + g.gapX;
            const int stepY = g.cellHeight + g.gapY;
            if (stepX > 0) {
                for (int i = 0; ; ++i) {
                    const int xl = g.originX + i * stepX;
                    if (xl >= W) break;
                    lines.xs.push_back(xl);                            // 单元左边界
                    lines.xs.push_back(std::min(xl + g.cellWidth, W)); // 单元右边界
                }
            }
            if (stepY > 0) {
                for (int i = 0; ; ++i) {
                    const int yt = g.originY + i * stepY;
                    if (yt >= H) break;
                    lines.ys.push_back(yt);                            // 单元上边界
                    lines.ys.push_back(std::min(yt + g.cellHeight, H));// 单元下边界
                }
            }
            break;
        }
    }

    // 统一归一化：去重、升序、裁剪、补边界（阶段① 约定）。
    normalizeCutLines(lines, W, H);
    return lines;
}

} // namespace idc::engine
