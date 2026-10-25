// ============================================================================
// 文件：tests/test_engine_grid_axis.cpp
// 作用：L3 网格单轴三态验收（FR-L3.1 / SPEC §3.4.3）——分割数（Count）与比例（Weights）
//       的取整律（前 n-1 格四舍五入、末格吸收误差）、与周期轴（Pitch）的混合、
//       不可展开定义的引擎级精确报因，以及配置 JSON 双形态（平铺 / x-y 分轴）往返。
// 分块依据：单轴解析形式是独立关注点，与三种余量策略（test_engine_boundary）、
//       整体字段往返（test_engine_config_json）分开，便于失败定位。
// ============================================================================
#include "test_harness.h"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "core/image.h"
#include "engine/engine.h"
#include "engine/engine_config_json.h"

namespace {

using namespace idc;

// 构造带渐变的 RGBA 图（引擎只需尺寸与可切分像素）。
core::Image makeImage(const int w, const int h) {
    core::Image img(w, h, core::ImageFormat::Rgba);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
            img.setPixel(x, y, core::Color(static_cast<std::uint8_t>(x % 256),
                                           static_cast<std::uint8_t>(y % 256), 90, 255));
    return img;
}

engine::GridAxisSpec countAxis(const int n) {
    engine::GridAxisSpec a;
    a.mode = engine::GridAxisMode::Count;
    a.count = n;
    return a;
}

engine::GridAxisSpec weightAxis(const std::vector<double>& w) {
    engine::GridAxisSpec a;
    a.mode = engine::GridAxisMode::Weights;
    a.weights = w;
    return a;
}

engine::GridAxisSpec pitchAxis(const int origin, const int cell, const engine::RemainderPolicy rp) {
    engine::GridAxisSpec a;
    a.mode = engine::GridAxisMode::Pitch;
    a.origin = origin;
    a.cell = cell;
    a.remainder = rp;
    return a;
}

// 单轴铺出的各单元尺寸（列宽 / 行高），按列 / 行序号升序。
std::vector<int> colWidths(const engine::Grid& g) {
    std::vector<int> out;
    for (int c = 0; c < g.colCount(); ++c) {
        out.push_back(g.cellAt(0, c) != nullptr ? g.cellAt(0, c)->area.width() : 0);
    }
    return out;
}

std::vector<int> rowHeights(const engine::Grid& g) {
    std::vector<int> out;
    for (int r = 0; r < g.rowCount(); ++r) {
        out.push_back(g.cellAt(r, 0) != nullptr ? g.cellAt(r, 0)->area.height() : 0);
    }
    return out;
}

// 把一段 JSON 文本落到临时文件后载入，返回是否成功（失败原因经 err 回传）。
bool loadConfigText(const std::filesystem::path& file, const std::string& text,
                    engine::EngineConfig& out, std::string& err) {
    {
        std::ofstream ofs(file, std::ios::trunc | std::ios::binary);
        if (!ofs) return false;
        ofs << text;
    }
    const auto r = engine::loadEngineConfig(file.string(), out);
    if (!r) { err = r.error().message; return false; }
    return true;
}

} // namespace

// 网格单轴三态验收（FR-L3.1 / SPEC §3.4.3）。
void testEngineGridAxis() {
    using namespace engine;

    // ---- 分割数取整律：cw = round(limit / n)，前 n-1 格等宽，末格吸收全部误差。----
    {
        struct Case {
            int limit;
            int count;
            std::vector<int> expect;
        };
        const Case cases[] = {
            {1000, 3, {333, 333, 334}},   // 1000/3 → 333 余 1，末格 +1
            {5, 3, {2, 2, 1}},            // 5/3 → round(1.67)=2，末格只剩 1
            {200, 3, {67, 67, 66}},       // 200/3 → round(66.67)=67，末格反向吃 1
            {7, 5, {1, 1, 1, 1, 3}},      // 7/5 → round(1.4)=1，末格吃 3
            {3, 3, {1, 1, 1}},            // 整除：逐格相等
        };
        for (const Case& c : cases) {
            GridParams p;
            p.axisX = countAxis(c.count);
            p.axisY = countAxis(1);
            Grid g;
            g.build(p, c.limit, 10);
            CHECK(g.colCount() == c.count);
            CHECK(g.rowCount() == 1);
            CHECK(colWidths(g) == c.expect);
            // 恒恰好铺满 [0, limit]、不产生残缺单元。
            CHECK(g.cellAt(0, c.count - 1) != nullptr &&
                  g.cellAt(0, c.count - 1)->area.right == c.limit);
            CHECK(g.cellAt(0, 0) != nullptr && g.cellAt(0, 0)->area.left == 0);
            for (const Cell& cell : g.cells()) CHECK(!cell.partial);
        }
    }

    // ---- 比例取整律：scale = limit / Σw；前 k-1 格 round(wᵢ × scale)，末格吃误差。----
    {
        GridParams p;
        p.axisX = weightAxis({1, 2, 1});   // 1000 → 250 / 500 / 250
        p.axisY = weightAxis({1});         // 整轴 1 份
        Grid g;
        g.build(p, 1000, 10);
        CHECK(g.colCount() == 3 && g.rowCount() == 1);
        CHECK((colWidths(g) == std::vector{250, 500, 250}));

        // 小数权重：3:1 → 0.75 / 0.25。
        GridParams q;
        q.axisX = weightAxis({3, 1});
        q.axisY = weightAxis({1});
        g.build(q, 400, 10);
        CHECK((colWidths(g) == std::vector{300, 100}));

        // 权重个数多于像素数 → 分不出正宽单元（整轴不可展开）。
        GridParams r;
        r.axisX = weightAxis({1, 1, 1, 1});
        r.axisY = countAxis(1);
        g.build(r, 3, 10);
        CHECK(g.cellCount() == 0);
    }

    // ---- 等价性：count = k ≡ weights 全 1（同一取整律 ⇒ 同一切割线）。----
    {
        for (const int limit : {1000, 200, 7, 5}) {
            for (const int k : {1, 2, 3, 4}) {
                if (k > limit) continue;
                GridParams pc;
                pc.axisX = countAxis(k);
                pc.axisY = countAxis(1);
                Grid gc;
                gc.build(pc, limit, 10);

                GridParams pw;
                pw.axisX = weightAxis(std::vector<double>(static_cast<std::size_t>(k), 1.0));
                pw.axisY = weightAxis({1});
                Grid gw;
                gw.build(pw, limit, 10);

                CHECK(gc.colCount() == gw.colCount());
                CHECK(colWidths(gc) == colWidths(gw));
            }
        }
    }

    // ---- 混合轴：横向分割数 + 纵向周期（保留残缺），两轴各按各的语义展开。----
    {
        GridParams p;
        p.axisX = countAxis(3);                                        // 横向恰好 3 份
        p.axisY = pitchAxis(0, 30, RemainderPolicy::Discard);           // 纵向 100/30 → 3 行，余 10 丢弃
        Grid g;
        g.build(p, 100, 100);
        CHECK(g.colCount() == 3 && g.rowCount() == 3 && g.cellCount() == 9);
        // 横向铺满到 100（末格吸收误差），纵向停在 90（残缺行被丢弃）。
        CHECK(g.cellAt(0, 2) != nullptr && g.cellAt(0, 2)->area.right == 100);
        CHECK(g.cellAt(2, 0) != nullptr && g.cellAt(2, 0)->area.bottom == 90);
        for (const Cell& cell : g.cells()) CHECK(!cell.partial);

        // 纵向改 KeepPartial：多出一行残缺单元，裁剪到边界、partial=true。
        p.axisY = pitchAxis(0, 30, RemainderPolicy::KeepPartial);
        g.build(p, 100, 100);
        CHECK(g.rowCount() == 4 && g.cellCount() == 12);
        const Cell* kp = g.cellAt(3, 0);
        CHECK(kp != nullptr && kp->partial && kp->area.bottom == 100 && kp->area.height() == 10);
    }

    // ---- 两轴皆非周期：恒恰好铺满全图、无残缺单元、面积守恒。----
    {
        GridParams p;
        p.axisX = countAxis(3);
        p.axisY = countAxis(4);   // 如用户所写「3 竖 4 横」
        Grid g;
        g.build(p, 100, 80);
        CHECK(g.colCount() == 3 && g.rowCount() == 4 && g.cellCount() == 12);
        long long sum = 0;
        for (const Cell& cell : g.cells()) {
            CHECK(!cell.partial);
            sum += cell.area.area();
        }
        CHECK(sum == 100LL * 80);   // 无重复、无丢失
        CHECK(g.cellAt(3, 2) != nullptr &&
              g.cellAt(3, 2)->area == RectRegion(66, 60, 100, 80));   // 末行末列吃满误差
    }

    // ---- 引擎侧精确报因：不可展开时给具体原因（而非笼统 NoGrid）。----
    {
        const core::Image img = makeImage(3, 20);
        const auto run = [&](const GridParams& gp) {
            EngineConfig cfg;
            cfg.source = SourceInfo{3, 20};
            cfg.cut.tier = Tier::Grid;
            cfg.cut.generator = CutGenerator::Grid;
            cfg.cut.grid = gp;
            cfg.cut.polarity = Polarity::Keep;
            return runEngine(img, cfg);
        };

        GridParams tooMany;
        tooMany.axisX = countAxis(4);    // 4 > 宽 3：会产生零宽格
        tooMany.axisY = countAxis(1);
        const auto r1 = run(tooMany);
        CHECK(!r1);
        CHECK(r1.error().code == core::CoreError::Code::NoGrid);
        CHECK(r1.error().message.find("分割数") != std::string::npos);

        GridParams zeroCount;
        zeroCount.axisX = countAxis(0);
        zeroCount.axisY = countAxis(1);
        const auto r2 = run(zeroCount);
        CHECK(!r2 && r2.error().message.find("分割数") != std::string::npos);

        GridParams badWeight;
        badWeight.axisX = weightAxis({1, 0, 1});   // 含非正权重
        badWeight.axisY = countAxis(1);
        const auto r3 = run(badWeight);
        CHECK(!r3 && r3.error().message.find("比例") != std::string::npos);

        GridParams badCell;
        badCell.axisX = countAxis(1);
        badCell.axisY = pitchAxis(0, 0, RemainderPolicy::Discard);   // 周期非正
        const auto r4 = run(badCell);
        CHECK(!r4 && r4.error().message.find("单元") != std::string::npos);

        // 合法分割数：引擎正常出片（无显式选择时默认全选），3 列 × 1 行 → 3 块。
        GridParams ok;
        ok.axisX = countAxis(3);
        ok.axisY = countAxis(1);
        const auto r5 = run(ok);
        CHECK(r5);
        CHECK(r5->kept.size() == 3);
    }

    // ---- 配置 JSON：分轴形态（x / y）往返，平铺形态保持平铺。----
    {
        namespace fs = std::filesystem;
        const fs::path dir = fs::temp_directory_path() / "idc_grid_axis_test";
        std::error_code ec;
        fs::remove_all(dir, ec);
        fs::create_directories(dir, ec);
        const fs::path file = dir / "cfg.json";

        const std::string head = R"({"source":{"width":1000,"height":800},"cut":{"tier":"grid","generator":"grid","params":)";
        const std::string tail = R"(},"export":{"mode":"merged"}})";

        // ① 分割数形态：x=3、y=4。
        {
            EngineConfig got;
            std::string err;
            CHECK_MSG(loadConfigText(file, head + R"({"x":{"count":3},"y":{"count":4}})" + tail, got, err), err);
            CHECK(got.cut.grid.axisX.has_value() && got.cut.grid.axisX->mode == GridAxisMode::Count &&
                  got.cut.grid.axisX->count == 3);
            CHECK(got.cut.grid.axisY.has_value() && got.cut.grid.axisY->count == 4);
        }

        // ② 比例形态：百分比串与整数比等价（只取相对比例）。
        {
            EngineConfig got;
            std::string err;
            CHECK_MSG(loadConfigText(file,
                head + R"({"x":{"weights":["25%","50%","25%"]},"y":{"weights":[3,1]}})" + tail, got, err), err);
            CHECK(got.cut.grid.axisX.has_value() && got.cut.grid.axisX->mode == GridAxisMode::Weights);
            CHECK((got.cut.grid.axisX->weights == std::vector<double>{25.0, 50.0, 25.0}));
            CHECK(got.cut.grid.axisY.has_value() && got.cut.grid.axisY->mode == GridAxisMode::Weights);
            CHECK((got.cut.grid.axisY->weights == std::vector<double>{3.0, 1.0}));

            // 百分比与整数比落同一组像素宽（25/50/25 ≡ 1/2/1）。
            Grid gpct;
            gpct.build(got.cut.grid, 1000, 800);
            CHECK((colWidths(gpct) == std::vector{250, 500, 250}));

            GridParams pint;
            pint.axisX = weightAxis({1, 2, 1});
            pint.axisY = got.cut.grid.axisY;
            Grid gint;
            gint.build(pint, 1000, 800);
            CHECK((colWidths(gint) == colWidths(gpct)));
        }

        // ③ 缺省轴 = 整轴 1 份（不作周期缺省，避免 cell=1 退化成像素行）。
        {
            EngineConfig got;
            std::string err;
            CHECK_MSG(loadConfigText(file, head + R"({"x":{"count":3}})" + tail, got, err), err);
            CHECK(got.cut.grid.axisY.has_value() && got.cut.grid.axisY->mode == GridAxisMode::Count &&
                  got.cut.grid.axisY->count == 1);
        }

        // ④ 平铺形态（两轴皆周期）仍按平铺四键读取，且不做分轴覆盖。
        {
            EngineConfig got;
            std::string err;
            CHECK_MSG(loadConfigText(file,
                head + R"({"originX":0,"originY":0,"cellWidth":100,"cellHeight":100,"remainder":"keep-partial"})" + tail,
                got, err), err);
            CHECK(!got.cut.grid.axisX.has_value() && !got.cut.grid.axisY.has_value());
            CHECK(got.cut.grid.cellWidth == 100 && got.cut.grid.remainder == RemainderPolicy::KeepPartial);
        }

        // ⑤ 混写（平铺键 + x/y 分轴）→ 严格校验报错。
        {
            EngineConfig got;
            std::string err;
            CHECK(!loadConfigText(file,
                head + R"({"originX":0,"x":{"count":3}})" + tail, got, err));
            CHECK(err.find("不可混用") != std::string::npos);
        }

        // ⑥ 单轴内键组冲突（count + weights 同写）→ 报错。
        {
            EngineConfig got;
            std::string err;
            CHECK(!loadConfigText(file,
                head + R"({"x":{"count":3,"weights":[1,1,1]}})" + tail, got, err));
            CHECK(err.find("三选一") != std::string::npos);
        }

        // ⑦ 权重非正 → 报错（定位到具体下标）。
        {
            EngineConfig got;
            std::string err;
            CHECK(!loadConfigText(file,
                head + R"({"x":{"weights":[1,0,1]}})" + tail, got, err));
            CHECK(err.find("权重") != std::string::npos);
        }

        // ⑧ 保存侧：分轴形态写回 x / y 子对象（不混写平铺键），往返后语义不变。
        {
            EngineConfig cfg;
            cfg.source = SourceInfo{1000, 800};
            cfg.cut.tier = Tier::Grid;
            cfg.cut.generator = CutGenerator::Grid;
            cfg.cut.grid.axisX = weightAxis({1, 2, 1});
            cfg.cut.grid.axisY = pitchAxis(0, 100, RemainderPolicy::Discard);
            CHECK(engine::saveEngineConfig(file.string(), cfg));

            EngineConfig got;
            std::string err;
            const auto r = engine::loadEngineConfig(file.string(), got);
            CHECK_MSG(static_cast<bool>(r), r ? std::string{} : r.error().message);
            CHECK(got.cut.grid.axisX.has_value() && got.cut.grid.axisX->mode == GridAxisMode::Weights);
            CHECK((got.cut.grid.axisX->weights == std::vector<double>{1.0, 2.0, 1.0}));
            CHECK(got.cut.grid.axisY.has_value() && got.cut.grid.axisY->mode == GridAxisMode::Pitch &&
                  got.cut.grid.axisY->cell == 100);

            // 落盘文本里只应有 x / y，不应出现平铺四键。
            std::ifstream ifs(file);
            const std::string text((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
            CHECK(text.find("\"x\"") != std::string::npos && text.find("\"y\"") != std::string::npos);
            CHECK(text.find("\"originX\"") == std::string::npos);   // 不混写平铺键
        }

        fs::remove_all(dir, ec);
    }

    std::cout << "[engine-grid-axis] OK\n";
}
