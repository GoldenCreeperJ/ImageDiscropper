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

// 测试适配：把 runEngine / runEngineGeometry 的 std::expected<EngineResult, CoreError>
// 摊平为 {ok / error / kept / composition / collapsible / uniformBlocks} 结构体以简化断言，
// 失败时值成员取默认（kept 空、composition 空、collapsible false、uniformBlocks false）。
struct TestResult {
    bool ok;
    std::string error;
    idc::engine::RegionSet kept;
    idc::engine::Composition composition;
    bool collapsible;
    bool uniformBlocks;
};

TestResult toTest(std::expected<idc::engine::EngineResult, idc::core::CoreError> e) {
    if (!e) return {false, std::move(e.error().message), {}, {}, false, false};
    return {true, std::string{}, std::move(e->kept), std::move(e->composition), e->collapsible,
            e->uniformBlocks};
}

// 构造一张带伪随机渐变的 RGBA 图，确保各单元像素互异（便于位置断言）。
idc::core::Image makeImage(const int w, const int h) {
    idc::core::Image img(w, h, idc::core::ImageFormat::Rgba);
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
        cfg.cut.tier = Tier::Standard;
        cfg.cut.generator = CutGenerator::Rect;
        cfg.cut.rect = RectRegion(30, 20, 70, 60); // x1,y1,x2,y2
        cfg.cut.polarity = Polarity::Keep;
        cfg.exportParams.mode = ExportMode::Merged;
        cfg.exportParams.layout = MergeLayout::Collapse;
        const auto r = toTest(runEngine(img, cfg));
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
        cfg.cut.generator = CutGenerator::HorizontalLine;
        cfg.cut.rect = RectRegion(0, 20, 100, 60); // 仅用 y1,y2
        cfg.cut.polarity = Polarity::Keep;
        cfg.exportParams.mode = ExportMode::Merged;
        cfg.exportParams.layout = MergeLayout::Collapse;
        const auto r = toTest(runEngine(img, cfg));
        CHECK(r.ok);
        CHECK(r.composition.canvasWidth == 100 && r.composition.canvasHeight == 40);
    }

    // ---- L1 竖线保留中间竖带：canvas = 带宽 × H。----
    {
        const Image img = makeImage(100, 80);
        EngineConfig cfg;
        cfg.source = SourceInfo{100, 80};
        cfg.cut.generator = CutGenerator::VerticalLine;
        cfg.cut.rect = RectRegion(30, 0, 70, 80); // 仅用 x1,x2
        cfg.cut.polarity = Polarity::Keep;
        cfg.exportParams.mode = ExportMode::Merged;
        cfg.exportParams.layout = MergeLayout::Collapse;
        const auto r = toTest(runEngine(img, cfg));
        CHECK(r.ok);
        CHECK(r.composition.canvasWidth == 40 && r.composition.canvasHeight == 80);
    }

    // ---- L2 十字剔除：保留四角，坍缩尺寸 (W−Δx)×(H−Δy)，四角落位符合 SPEC §4.2。----
    {
        const Image img = makeImage(100, 80);
        EngineConfig cfg;
        cfg.source = SourceInfo{100, 80};
        cfg.cut.tier = Tier::Erase;
        cfg.cut.generator = CutGenerator::Rect;
        cfg.cut.rect = RectRegion(30, 20, 70, 60);
        cfg.cut.polarity = Polarity::Remove;
        cfg.exportParams.mode = ExportMode::Merged;
        cfg.exportParams.layout = MergeLayout::Collapse;
        const auto r = toTest(runEngine(img, cfg));
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
        cfg.cut.generator = CutGenerator::HorizontalLine;
        cfg.cut.rect = RectRegion(0, 20, 100, 60);
        cfg.cut.polarity = Polarity::Remove;
        cfg.exportParams.mode = ExportMode::Merged;
        cfg.exportParams.layout = MergeLayout::Collapse;
        const auto r = toTest(runEngine(img, cfg));
        CHECK(r.ok);
        CHECK(r.collapsible);
        CHECK(r.composition.canvasWidth == 100 && r.composition.canvasHeight == 40);
    }

    // ---- L2 竖线剔除：坍缩为 (W−Δx) × H。----
    {
        const Image img = makeImage(100, 80);
        EngineConfig cfg;
        cfg.source = SourceInfo{100, 80};
        cfg.cut.generator = CutGenerator::VerticalLine;
        cfg.cut.rect = RectRegion(30, 0, 70, 80);
        cfg.cut.polarity = Polarity::Remove;
        cfg.exportParams.mode = ExportMode::Merged;
        cfg.exportParams.layout = MergeLayout::Collapse;
        const auto r = toTest(runEngine(img, cfg));
        CHECK(r.ok);
        CHECK(r.collapsible);
        CHECK(r.composition.canvasWidth == 60 && r.composition.canvasHeight == 80);
    }

    // ---- 极性开关：同一选框 KEEP 保中心、REMOVE 保四角（FR-L2.5）。----
    {
        const Image img = makeImage(100, 80);
        EngineConfig cfg;
        cfg.source = SourceInfo{100, 80};
        cfg.cut.generator = CutGenerator::Rect;
        cfg.cut.rect = RectRegion(30, 20, 70, 60);
        cfg.exportParams.mode = ExportMode::Merged;
        cfg.exportParams.layout = MergeLayout::Collapse;
        cfg.cut.polarity = Polarity::Keep;
        const auto rk = toTest(runEngine(img, cfg));
        cfg.cut.polarity = Polarity::Remove;
        const auto rr = toTest(runEngine(img, cfg));
        CHECK(rk.ok && rr.ok);
        CHECK(rk.kept.size() == 1); // 中心一块
        CHECK(rr.kept.size() == 4); // 四角四块
    }

    // ---- L3 网格定义 + 显式单元选择 + 重排合并。----
    {
        const Image img = makeImage(300, 300);
        EngineConfig cfg;
        cfg.source = SourceInfo{300, 300};
        cfg.cut.tier = Tier::Grid;
        cfg.cut.generator = CutGenerator::Grid;
        cfg.cut.grid = GridParams{.originX = 0, .originY = 0, .cellWidth = 100, .cellHeight = 100, .remainder = RemainderPolicy::Discard}; // 300/100 → 3×3 自动推导
        cfg.cut.polarity = Polarity::Keep;
        cfg.selectedCells = {0, 2, 6, 8}; // 四角单元
        cfg.order.strategy = SortStrategy::RowMajor;
        cfg.exportParams.mode = ExportMode::Merged;
        cfg.exportParams.layout = MergeLayout::Rearrange;
        cfg.exportParams.cols = 2;
        cfg.exportParams.rows = 2;
        const auto r = toTest(runEngine(img, cfg));
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
            cfg.cut.tier = Tier::Grid;
            cfg.cut.generator = CutGenerator::Grid;
            cfg.cut.grid = GridParams{.originX = 0, .originY = 0, .cellWidth = 100, .cellHeight = 100, .remainder = RemainderPolicy::Discard};
            cfg.cut.polarity = Polarity::Keep;
            cfg.selectedCells = {0, 1, 2, 3};
            cfg.order.strategy = SortStrategy::RowMajor;
            cfg.exportParams.mode = ExportMode::Merged;
            cfg.exportParams.layout = MergeLayout::Rearrange;
            cfg.exportParams.cols = 2;
            cfg.exportParams.rows = 2;
            return cfg;
        };
        // 行优先（默认）：槽序 [0,1,2,3] → 块1 落 slot1=(r0,c1)。
        { const auto r = toTest(runEngine(img, base()));
          CHECK(r.ok && r.composition.placements.size() == 4);
          CHECK(r.composition.placements[1].dest.left == 100 && r.composition.placements[1].dest.top == 0); }
        // 列优先：槽序 [0,2,1,3] → 块1 落 slot2=(r1,c0)。
        { EngineConfig cfg = base(); cfg.exportParams.mergeOrder.strategy = SortStrategy::ColumnMajor;
          const auto r = toTest(runEngine(img, cfg)); CHECK(r.ok);
          CHECK(r.composition.placements[1].dest.left == 0 && r.composition.placements[1].dest.top == 100); }
        // 蛇形（行优先）：槽序 [0,1,3,2] → 块2 落 slot3=(r1,c1)。
        { EngineConfig cfg = base(); cfg.exportParams.mergeOrder.snake = true;
          const auto r = toTest(runEngine(img, cfg)); CHECK(r.ok);
          CHECK(r.composition.placements[2].dest.left == 100 && r.composition.placements[2].dest.top == 100); }
        // 倒序（行优先）：槽序 [3,2,1,0] → 块0 落 slot3=(r1,c1)。
        { EngineConfig cfg = base(); cfg.exportParams.mergeOrder.reverse = true;
          const auto r = toTest(runEngine(img, cfg)); CHECK(r.ok);
          CHECK(r.composition.placements[0].dest.left == 100 && r.composition.placements[0].dest.top == 100); }
    }

    // ---- 重排准入门限（SPEC §4.3）：仅当保留块长宽全等才可重排；不全等时按
    //      「可坍缩 → 坍缩，否则 → 分离导出」回退（比例轴最容易产出参差块）。----
    {
        const Image img = makeImage(300, 300);
        // 横轴 1:5:1 → 43/214/43；纵轴 1:2 → 100/200。
        auto base = [&] {
            EngineConfig cfg;
            cfg.source = SourceInfo{300, 300};
            cfg.cut.tier = Tier::Grid;
            cfg.cut.generator = CutGenerator::Grid;
            cfg.cut.grid.axisX = GridAxisSpec{.mode = GridAxisMode::Weights, .weights = {1.0, 5.0, 1.0}};
            cfg.cut.grid.axisY = GridAxisSpec{.mode = GridAxisMode::Weights, .weights = {1.0, 2.0}};
            cfg.cut.polarity = Polarity::Keep;
            cfg.order.strategy = SortStrategy::RowMajor;
            cfg.exportParams.mode = ExportMode::Merged;
            cfg.exportParams.layout = MergeLayout::Rearrange;
            cfg.exportParams.cols = 2;
            cfg.exportParams.rows = 1;
            return cfg;
        };
        // 参差块 + 不可坍缩（删掉的列 1 下半孤悬，既不构成整行也不构成整列）→ 分离导出。
        { EngineConfig cfg = base();
          cfg.selectedCells = {0, 1, 3};
          const auto r = toTest(runEngine(img, cfg));
          CHECK(r.ok && r.kept.size() == 3);
          CHECK(!r.uniformBlocks);
          CHECK(!r.collapsible);
          CHECK(r.composition.placements.size() == 3);
          CHECK(r.composition.canvasWidth == 0 && r.composition.canvasHeight == 0); // 分离：无统一画布。
          CHECK(r.composition.placements[1].dest.left == 0 && r.composition.placements[1].dest.top == 0); }
        // 参差块 + 可坍缩（删掉的列 2 为整列）→ 回退为坍缩（保留原相对位置，不会按最宽块撑格）。
        { EngineConfig cfg = base();
          cfg.selectedCells = {0, 1};
          const auto r = toTest(runEngine(img, cfg));
          CHECK(r.ok && r.kept.size() == 2);
          CHECK(!r.uniformBlocks);
          CHECK(r.collapsible);
          CHECK(r.composition.canvasWidth == 257 && r.composition.canvasHeight == 100); // 43 + 214。
          CHECK(r.composition.placements[1].dest.left == 43); }
        // 块长宽全等 → 仍走重排（画布 = cols × rows 整数格）。
        { EngineConfig cfg = base();
          cfg.selectedCells = {0, 2}; // 均为 43×100
          const auto r = toTest(runEngine(img, cfg));
          CHECK(r.ok && r.kept.size() == 2);
          CHECK(r.uniformBlocks);
          CHECK(r.composition.canvasWidth == 86 && r.composition.canvasHeight == 100); }
    }

    // ---- 仅 L3 可重排：非 L3 显式 REARRANGE → 报错。----
    {
        const Image img = makeImage(100, 80);
        EngineConfig cfg;
        cfg.source = SourceInfo{100, 80};
        cfg.cut.tier = Tier::Erase;
        cfg.cut.generator = CutGenerator::Rect;
        cfg.cut.rect = RectRegion(30, 20, 70, 60);
        cfg.cut.polarity = Polarity::Remove;
        cfg.exportParams.mode = ExportMode::Merged;
        cfg.exportParams.layout = MergeLayout::Rearrange; // 非网格模式重排 → 拒绝
        const auto r = toTest(runEngine(img, cfg));
        CHECK(!r.ok);
        CHECK(r.error.find("网格") != std::string::npos);
    }

    // ---- 排序策略：Sequence::build 生成序号（FR-L3.5）。----
    {
        Sequence s;
        s.build(9, 3, 3, SequenceParams{.strategy = SortStrategy::RowMajor, .reverse = false, .snake = false});
        CHECK((s.order() == std::vector{0, 1, 2, 3, 4, 5, 6, 7, 8}));

        s.build(9, 3, 3, SequenceParams{.strategy = SortStrategy::ColumnMajor, .reverse = false, .snake = false});
        CHECK((s.order() == std::vector{0, 3, 6, 1, 4, 7, 2, 5, 8}));

        s.build(9, 3, 3, SequenceParams{.strategy = SortStrategy::RowMajor, .reverse = true, .snake = false}); // 整体逆序
        CHECK((s.order() == std::vector{8, 7, 6, 5, 4, 3, 2, 1, 0}));

        s.build(9, 3, 3, SequenceParams{.strategy = SortStrategy::RowMajor, .reverse = false, .snake = true}); // 蛇形
        CHECK((s.order() == std::vector{0, 1, 2, 5, 4, 3, 6, 7, 8}));

        Sequence custom;
        custom.setCustom({3, 1, 2, 0});
        CHECK(custom.size() == 4 && custom.order().front() == 3);
    }

    // ---- 仅尺寸入口等价性：几何入口 == 先物化再走图像入口（预览脱离像素的正确性）。----
    {
        const Image img = makeImage(120, 80);
        EngineConfig cfg;
        cfg.source = SourceInfo{120, 80};
        cfg.cut.tier = Tier::Erase;
        cfg.cut.generator = CutGenerator::Rect;
        cfg.cut.rect = RectRegion(30, 20, 90, 60);
        cfg.cut.polarity = Polarity::Remove;
        cfg.exportParams.mode = ExportMode::Merged;
        cfg.exportParams.layout = MergeLayout::Collapse;
        // 预览侧：先用流水线把尺寸推演出来（rotate90 交换 → 80×120），再以该尺寸走仅尺寸入口。
        cfg.preprocess.add(idc::preprocess::RotateOp{90});
        const auto g = cfg.preprocess.geometryOutput(img.width(), img.height());
        EngineConfig runCfg = cfg;
        runCfg.preprocess.clear();                 // 仅尺寸入口不吃管线；尺寸已含旋转效果。
        runCfg.source = SourceInfo{g.width, g.height};
        const auto rImg = toTest(runEngine(img, cfg));
        const auto rGeo = toTest(runEngineGeometry(SourceInfo{g.width, g.height}, runCfg));
        CHECK(rImg.ok && rGeo.ok);
        CHECK(rGeo.kept.size() == rImg.kept.size());
        CHECK(rGeo.composition.canvasWidth == rImg.composition.canvasWidth &&
              rGeo.composition.canvasHeight == rImg.composition.canvasHeight);
    }

    std::cout << "[engine-pipeline] OK\n";
}
