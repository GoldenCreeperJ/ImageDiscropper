// ============================================================================
// 文件：tests/test_engine_pipeline.cpp
// 作用：引擎功能验收（SPEC §2.1 / §3 / §4.4）——通过 runEngine 跑通完整流水线，
//       断言 L1 保留尺寸、L2 十字四角位置与坍缩尺寸、极性开关、L3 网格/选择/排序/重排，
//       以及 Sequence::build 的 row-major / column-major / reverse / snake / custom 序号，
//       并重排填充顺序 MergeOrder（行/列优先 + 蛇形 + 倒序）与「仅 L3 可重排」限制。
// 分块依据：只覆盖“计算路径”（不落盘）；导出落盘见 test_engine_export.cpp，
//       边界异常见 test_engine_boundary.cpp，避免单文件膨胀。
// ============================================================================
#include "test_harness.h"

#include <vector>

#include "core/image.h"
#include "engine/engine.h"

namespace {

// 构造一张带伪随机渐变的 RGBA 图，确保各单元像素互异（便于位置断言）。
idc::core::Image makeImage(const int w, const int h) {
    idc::core::Image img(w, h, idc::core::ImageFormat::RGBA);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
            img.setPixel(x, y, idc::core::Color(static_cast<std::uint8_t>(x * 7 % 256),
                                                static_cast<std::uint8_t>(y * 11 % 256),
                                                128, 255));
    return img;
}

} // namespace

// 引擎流水线功能验收（SPEC §3）。
void testEnginePipeline() {
    using namespace idc::engine;
    using idc::core::Image;

    // ---- L1 十字保留中心：常规矩形裁剪，坍缩为中心块自身尺寸。----
    {
        const Image img = makeImage(100, 80);
        EngineConfig cfg;
        cfg.source = SourceInfo{100, 80};
        cfg.cut.tier = Tier::L1;
        cfg.cut.generator = CutGenerator::RECT;
        cfg.cut.rect = RectRegion(30, 20, 70, 60); // x1,y1,x2,y2
        cfg.cut.polarity = Polarity::KEEP;
        cfg.emitParams.mode = EmitMode::MERGED;
        cfg.emitParams.layout = MergeLayout::COLLAPSE;
        const EngineResult r = runEngine(img, cfg);
        CHECK(r.ok);
        CHECK(r.collapsible);
        CHECK(r.kept.size() == 1);
        CHECK(r.composition.canvasWidth == 40 && r.composition.canvasHeight == 40);
        CHECK(r.composition.placements.size() == 1);
        CHECK(r.composition.placements[0].dest == RectRegion(0, 0, 40, 40));
    }

    // ---- L1 横线保留中间横带：canvas = W × 带高。----
    {
        const Image img = makeImage(100, 80);
        EngineConfig cfg;
        cfg.source = SourceInfo{100, 80};
        cfg.cut.generator = CutGenerator::HORIZONTAL_LINE;
        cfg.cut.rect = RectRegion(0, 20, 100, 60); // 仅用 y1,y2
        cfg.cut.polarity = Polarity::KEEP;
        cfg.emitParams.mode = EmitMode::MERGED;
        cfg.emitParams.layout = MergeLayout::COLLAPSE;
        const EngineResult r = runEngine(img, cfg);
        CHECK(r.ok);
        CHECK(r.composition.canvasWidth == 100 && r.composition.canvasHeight == 40);
    }

    // ---- L1 竖线保留中间竖带：canvas = 带宽 × H。----
    {
        const Image img = makeImage(100, 80);
        EngineConfig cfg;
        cfg.source = SourceInfo{100, 80};
        cfg.cut.generator = CutGenerator::VERTICAL_LINE;
        cfg.cut.rect = RectRegion(30, 0, 70, 80); // 仅用 x1,x2
        cfg.cut.polarity = Polarity::KEEP;
        cfg.emitParams.mode = EmitMode::MERGED;
        cfg.emitParams.layout = MergeLayout::COLLAPSE;
        const EngineResult r = runEngine(img, cfg);
        CHECK(r.ok);
        CHECK(r.composition.canvasWidth == 40 && r.composition.canvasHeight == 80);
    }

    // ---- L2 十字剔除：保留四角，坍缩尺寸 (W−Δx)×(H−Δy)，四角落位符合 SPEC §4.2。----
    {
        const Image img = makeImage(100, 80);
        EngineConfig cfg;
        cfg.source = SourceInfo{100, 80};
        cfg.cut.tier = Tier::L2;
        cfg.cut.generator = CutGenerator::RECT;
        cfg.cut.rect = RectRegion(30, 20, 70, 60);
        cfg.cut.polarity = Polarity::REMOVE;
        cfg.emitParams.mode = EmitMode::MERGED;
        cfg.emitParams.layout = MergeLayout::COLLAPSE;
        const EngineResult r = runEngine(img, cfg);
        CHECK(r.ok);
        CHECK(r.collapsible);
        CHECK(r.kept.size() == 4);
        // Δx = 70−30 = 40, Δy = 60−20 = 40 → canvas = 60 × 40。
        CHECK(r.composition.canvasWidth == 60 && r.composition.canvasHeight == 40);
        const auto& pl = r.composition.placements;
        CHECK(pl.size() == 4);
        // 左上→(0,0)、右上→(x1,0)、左下→(0,y1)、右下→(x1,y1)，其中 x1=30,y1=20。
        CHECK(pl[0].dest.left == 0 && pl[0].dest.top == 0);
        CHECK(pl[1].dest.left == 30 && pl[1].dest.top == 0);
        CHECK(pl[2].dest.left == 0 && pl[2].dest.top == 20);
        CHECK(pl[3].dest.left == 30 && pl[3].dest.top == 20);
    }

    // ---- L2 横线剔除：坍缩为 W × (H−Δy)。----
    {
        const Image img = makeImage(100, 80);
        EngineConfig cfg;
        cfg.source = SourceInfo{100, 80};
        cfg.cut.generator = CutGenerator::HORIZONTAL_LINE;
        cfg.cut.rect = RectRegion(0, 20, 100, 60);
        cfg.cut.polarity = Polarity::REMOVE;
        cfg.emitParams.mode = EmitMode::MERGED;
        cfg.emitParams.layout = MergeLayout::COLLAPSE;
        const EngineResult r = runEngine(img, cfg);
        CHECK(r.ok);
        CHECK(r.collapsible);
        CHECK(r.composition.canvasWidth == 100 && r.composition.canvasHeight == 40);
    }

    // ---- L2 竖线剔除：坍缩为 (W−Δx) × H。----
    {
        const Image img = makeImage(100, 80);
        EngineConfig cfg;
        cfg.source = SourceInfo{100, 80};
        cfg.cut.generator = CutGenerator::VERTICAL_LINE;
        cfg.cut.rect = RectRegion(30, 0, 70, 80);
        cfg.cut.polarity = Polarity::REMOVE;
        cfg.emitParams.mode = EmitMode::MERGED;
        cfg.emitParams.layout = MergeLayout::COLLAPSE;
        const EngineResult r = runEngine(img, cfg);
        CHECK(r.ok);
        CHECK(r.collapsible);
        CHECK(r.composition.canvasWidth == 60 && r.composition.canvasHeight == 80);
    }

    // ---- 极性开关：同一选框 KEEP 保中心、REMOVE 保四角（FR-L2.5）。----
    {
        const Image img = makeImage(100, 80);
        EngineConfig cfg;
        cfg.source = SourceInfo{100, 80};
        cfg.cut.generator = CutGenerator::RECT;
        cfg.cut.rect = RectRegion(30, 20, 70, 60);
        cfg.emitParams.mode = EmitMode::MERGED;
        cfg.emitParams.layout = MergeLayout::COLLAPSE;
        cfg.cut.polarity = Polarity::KEEP;
        const EngineResult rk = runEngine(img, cfg);
        cfg.cut.polarity = Polarity::REMOVE;
        const EngineResult rr = runEngine(img, cfg);
        CHECK(rk.ok && rr.ok);
        CHECK(rk.kept.size() == 1); // 中心一块
        CHECK(rr.kept.size() == 4); // 四角四块
    }

    // ---- L3 网格定义 + 显式单元选择 + 重排合并。----
    {
        const Image img = makeImage(300, 300);
        EngineConfig cfg;
        cfg.source = SourceInfo{300, 300};
        cfg.cut.tier = Tier::L3;
        cfg.cut.generator = CutGenerator::GRID;
        cfg.cut.grid = GridParams{0, 0, 100, 100, RemainderPolicy::DISCARD}; // 300/100 → 3×3 自动推导
        cfg.cut.polarity = Polarity::KEEP;
        cfg.selectedCells = {0, 2, 6, 8}; // 四角单元
        cfg.order.strategy = SortStrategy::ROW_MAJOR;
        cfg.emitParams.mode = EmitMode::MERGED;
        cfg.emitParams.layout = MergeLayout::REARRANGE;
        cfg.emitParams.cols = 2;
        cfg.emitParams.rows = 2;
        const EngineResult r = runEngine(img, cfg);
        CHECK(r.ok);
        CHECK(r.kept.size() == 4);
        // 单元 100×100，2×2 画布 → 200×200。
        CHECK(r.composition.canvasWidth == 200 && r.composition.canvasHeight == 200);
        CHECK(r.composition.placements.size() == 4);
    }

    // ---- L3 重排填充顺序 MergeOrder（行/列优先 + 蛇形 + 倒序）：与选择排序正交。----
    // 选择序 row-major、selectedCells={0,1,2,3} → 块列表 [c0,c1,c2,c3]；单元 100×100、2×2 画布。
    {
        const Image img = makeImage(300, 300);
        auto base = [&] {
            EngineConfig cfg;
            cfg.source = SourceInfo{300, 300};
            cfg.cut.tier = Tier::L3;
            cfg.cut.generator = CutGenerator::GRID;
            cfg.cut.grid = GridParams{0, 0, 100, 100, RemainderPolicy::DISCARD};
            cfg.cut.polarity = Polarity::KEEP;
            cfg.selectedCells = {0, 1, 2, 3};
            cfg.order.strategy = SortStrategy::ROW_MAJOR;
            cfg.emitParams.mode = EmitMode::MERGED;
            cfg.emitParams.layout = MergeLayout::REARRANGE;
            cfg.emitParams.cols = 2;
            cfg.emitParams.rows = 2;
            return cfg;
        };
        // 行优先（默认）：槽序 [0,1,2,3] → 块1 落 slot1=(r0,c1)。
        { const EngineResult r = runEngine(img, base());
          CHECK(r.ok && r.composition.placements.size() == 4);
          CHECK(r.composition.placements[1].dest.left == 100 && r.composition.placements[1].dest.top == 0); }
        // 列优先：槽序 [0,2,1,3] → 块1 落 slot2=(r1,c0)。
        { EngineConfig cfg = base(); cfg.emitParams.mergeOrder.strategy = SortStrategy::COLUMN_MAJOR;
          const EngineResult r = runEngine(img, cfg); CHECK(r.ok);
          CHECK(r.composition.placements[1].dest.left == 0 && r.composition.placements[1].dest.top == 100); }
        // 蛇形（行优先）：槽序 [0,1,3,2] → 块2 落 slot3=(r1,c1)。
        { EngineConfig cfg = base(); cfg.emitParams.mergeOrder.snake = true;
          const EngineResult r = runEngine(img, cfg); CHECK(r.ok);
          CHECK(r.composition.placements[2].dest.left == 100 && r.composition.placements[2].dest.top == 100); }
        // 倒序（行优先）：槽序 [3,2,1,0] → 块0 落 slot3=(r1,c1)。
        { EngineConfig cfg = base(); cfg.emitParams.mergeOrder.reverse = true;
          const EngineResult r = runEngine(img, cfg); CHECK(r.ok);
          CHECK(r.composition.placements[0].dest.left == 100 && r.composition.placements[0].dest.top == 100); }
    }

    // ---- 仅 L3 可重排：非 L3 显式 REARRANGE → 报错。----
    {
        const Image img = makeImage(100, 80);
        EngineConfig cfg;
        cfg.source = SourceInfo{100, 80};
        cfg.cut.tier = Tier::L2;
        cfg.cut.generator = CutGenerator::RECT;
        cfg.cut.rect = RectRegion(30, 20, 70, 60);
        cfg.cut.polarity = Polarity::REMOVE;
        cfg.emitParams.mode = EmitMode::MERGED;
        cfg.emitParams.layout = MergeLayout::REARRANGE; // L2 重排 → 拒绝
        const EngineResult r = runEngine(img, cfg);
        CHECK(!r.ok);
        CHECK(r.error.find("L3") != std::string::npos);
    }

    // ---- 排序策略：Sequence::build 生成序号（FR-L3.5）。----
    {
        Sequence s;
        s.build(9, 3, 3, SequenceParams{SortStrategy::ROW_MAJOR, false, false});
        CHECK((s.order() == std::vector{0, 1, 2, 3, 4, 5, 6, 7, 8}));

        s.build(9, 3, 3, SequenceParams{SortStrategy::COLUMN_MAJOR, false, false});
        CHECK((s.order() == std::vector{0, 3, 6, 1, 4, 7, 2, 5, 8}));

        s.build(9, 3, 3, SequenceParams{SortStrategy::ROW_MAJOR, true, false}); // 整体逆序
        CHECK((s.order() == std::vector{8, 7, 6, 5, 4, 3, 2, 1, 0}));

        s.build(9, 3, 3, SequenceParams{SortStrategy::ROW_MAJOR, false, true}); // 蛇形
        CHECK((s.order() == std::vector{0, 1, 2, 5, 4, 3, 6, 7, 8}));

        Sequence custom;
        custom.setCustom({3, 1, 2, 0});
        CHECK(custom.size() == 4 && custom.order().front() == 3);
    }

    std::cout << "[engine-pipeline] OK\n";
}
