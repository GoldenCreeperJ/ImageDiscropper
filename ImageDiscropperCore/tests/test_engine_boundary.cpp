// ============================================================================
// 文件：tests/test_engine_boundary.cpp
// 作用：边界与异常验收（终稿 §6 / guideline §2.5 的 E-1~E-8）+ 空块跳过 +
//       像素归属无重复无丢失（左闭右开）。直接驱动 generateCutLines / induceGrid /
//       Grid::build / split / runEngine / exportImage，逐项断言异常契约。
// 分块依据：边界异常是独立关注点，与正常功能（pipeline）、导出落盘（export）分文件，
//       便于失败定位，避免单一测试文件膨胀为 God File。
// ============================================================================
#include "test_harness.h"

#include <filesystem>
#include <fstream>
#include <vector>

#include "core/image.h"
#include "engine/engine.h"

namespace {

// 构造带渐变的 RGBA 图。
idc::core::Image makeImage(const int w, const int h) {
    idc::core::Image img(w, h, idc::core::ImageFormat::RGBA);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
            img.setPixel(x, y, idc::core::Color(static_cast<std::uint8_t>((x * 13) % 256),
                                                static_cast<std::uint8_t>((y * 17) % 256),
                                                90, 255));
    return img;
}

} // namespace

// 边界与异常验收（§6）。
void testEngineBoundary() {
    using namespace idc::engine;
    using idc::core::Image;

    const Image img = makeImage(100, 80);

    // ---- E-1：选框退化（x1>x2 且 y1>y2）→ 禁止生成切割，runEngine 报错。----
    {
        EngineConfig cfg;
        cfg.source = SourceInfo{100, 80};
        cfg.cut.generator = CutGenerator::RECT;
        cfg.cut.rect = RectRegion(70, 60, 30, 20); // 宽高均非正
        cfg.cut.polarity = Polarity::REMOVE;
        const EngineResult r = runEngine(img, cfg);
        CHECK(!r.ok);
    }

    // ---- E-2：切割线超出图像边界 → 自动裁剪到 [0,W]×[0,H] 且补入边界。----
    {
        CutConfig cc;
        cc.generator = CutGenerator::RECT;
        cc.rect = RectRegion(-50, 10, 150, 60); // x1<0, x2>W
        const SourceInfo si{100, 80};
        const CutLineSet lines = generateCutLines(cc, si);
        CHECK(lines.xs.front() == 0 && lines.xs.back() == 100);
        CHECK(lines.xs.size() == 2); // -50→0、150→100 与边界去重，仅剩 {0,100}
        CHECK(lines.ys.front() == 0 && lines.ys.back() == 80);
    }

    // ---- E-3：x1==x2 退化 → 不产竖切割线，降级为横线切割。----
    {
        CutConfig cc;
        cc.generator = CutGenerator::RECT;
        cc.rect = RectRegion(50, 20, 50, 60); // x1==x2
        const SourceInfo si{100, 80};
        const CutLineSet lines = generateCutLines(cc, si);
        CHECK(lines.xs.size() == 2); // 仅边界 {0,100}，无竖切割
        CHECK(lines.ys.size() == 4); // 横线保留 {0,20,60,80}
    }

    // ---- E-4：贴边选框 x1=0 → 左侧块宽度为 0 被跳过（仅剩右侧两块）。----
    {
        EngineConfig cfg;
        cfg.source = SourceInfo{100, 80};
        cfg.cut.generator = CutGenerator::RECT;
        cfg.cut.rect = RectRegion(0, 20, 50, 60); // x1=0 贴左边
        cfg.cut.polarity = Polarity::REMOVE;
        const EngineResult r = runEngine(img, cfg);
        CHECK(r.ok);
        CHECK(r.kept.size() == 2); // 左侧两块宽度 0，不产生
    }
    // E-4（split 层）：零宽单元被显式跳过。
    {
        Grid g;
        g.buildFromLines({0, 0, 50, 100}, {0, 80}); // col0 = [0,0) 零宽
        const RegionSet rs = split(img, g);
        CHECK(rs.size() == 2); // 零宽块跳过，仅 col1/col2 保留
    }

    // ---- E-5：网格单元部分越界 → 余量策略 DISCARD / KEEP_PARTIAL / PAD。----
    {
        Grid g;
        GridParams p{0, 0, 30, 30, RemainderPolicy::DISCARD}; // 周期铺满全图，行列数自动推导
        g.build(p, 100, 100);
        CHECK(g.colCount() == 3 && g.rowCount() == 3 && g.cellCount() == 9); // 丢弃 10px 余量

        p.remainder = RemainderPolicy::KEEP_PARTIAL;
        g.build(p, 100, 100);
        CHECK(g.colCount() == 4 && g.rowCount() == 4 && g.cellCount() == 16);
        const Cell* kp = g.cellAt(0, 3);
        CHECK(kp && kp->partial && kp->area.right == 100); // 残缺单元裁剪到边界（宽 10）

        p.remainder = RemainderPolicy::PAD;
        g.build(p, 100, 100);
        const Cell* pad = g.cellAt(0, 3);
        CHECK(pad && pad->partial && pad->area.right == 120); // 补白：保留完整尺寸（越界）
    }

    // ---- E-6：删除区为空（REMOVE 却无单元被选中剔除）→ 报错。----
    {
        EngineConfig cfg;
        cfg.source = SourceInfo{100, 80};
        cfg.cut.generator = CutGenerator::HORIZONTAL_LINE;
        cfg.cut.rect = RectRegion(0, 50, 100, 50); // y1==y2 → 无横带
        cfg.cut.polarity = Polarity::REMOVE;
        const EngineResult r = runEngine(img, cfg);
        CHECK(!r.ok);
    }

    // ---- E-7：保留区为空（删除区覆盖全图）→ 禁止导出。----
    {
        EngineConfig cfg;
        cfg.source = SourceInfo{100, 80};
        cfg.cut.generator = CutGenerator::RECT;
        cfg.cut.rect = RectRegion(0, 0, 100, 80); // 十字覆盖全图
        cfg.cut.polarity = Polarity::REMOVE;
        const EngineResult r = runEngine(img, cfg);
        CHECK(!r.ok);
        CHECK(r.kept.empty());
    }

    // ---- E-8：导出路径为空 / 父路径不可创建 → 返回 false（不崩溃）。----
    // 说明：合并导出现已自动创建父目录（与分离一致），故“目录不存在”改为断言成功并清理；
    //       仍保留 E-8 失败用例：父路径被同名普通文件占用时 ensureDirectory 失败 → false。
    {
        namespace fs = std::filesystem;
        EngineConfig cfg;
        cfg.source = SourceInfo{100, 80};
        cfg.cut.generator = CutGenerator::RECT;
        cfg.cut.rect = RectRegion(30, 20, 70, 60);
        cfg.cut.polarity = Polarity::KEEP;
        cfg.emitParams.mode = EmitMode::MERGED;
        cfg.emitParams.layout = MergeLayout::COLLAPSE;
        const EngineResult r = runEngine(img, cfg);
        CHECK(r.ok);
        CHECK(!exportImage(r.composition, img, ""));                            // 空路径（合并）
        // 父目录不存在 → 自动创建并写出成功（修复后行为），随后清理临时目录。
        const std::string autoDir = "idc_auto_dir_xyz";
        CHECK(exportImage(r.composition, img, autoDir + "/out.png"));
        fs::remove_all(autoDir);
        // 父路径被普通文件占用 → 无法创建父目录 → false（E-8）。
        const std::string blocker = "idc_blocker_xyz";
        { std::ofstream ofs(blocker); ofs << "x"; }
        CHECK(!exportImage(r.composition, img, blocker + "/out.png"));
        fs::remove(blocker);
        // 分离导出：目标文件夹不存在会自动创建（见 test_engine_export），但空路径仍报错（E-8）。
        cfg.emitParams.mode = EmitMode::SEPARATE;
        const EngineResult rs = runEngine(img, cfg);
        CHECK(rs.ok);
        CHECK(!exportImage(rs.composition, img, ""));                           // 空路径（分离）
    }

    // ---- 像素归属：split 恰好铺满、互不重叠（左闭右开，无重复无丢失）。----
    {
        CutConfig cc;
        cc.generator = CutGenerator::RECT;
        cc.rect = RectRegion(30, 20, 70, 60);
        const SourceInfo si{100, 80};
        const Grid g = induceGrid(generateCutLines(cc, si), si);
        const RegionSet all = split(img, g);
        CHECK(all.size() == 9);                        // 3×3 全 9 格
        CHECK(all.totalArea() == 100LL * 80);          // 总面积 == 图像面积（无重复无丢失）
        CHECK(all.boundingBox() == RectRegion(0, 0, 100, 80)); // 恰好铺满
    }

    // ---- 全集守恒：保留集 + 删除集 == 全集（十字剔除 5 格、保留 4 角）。----
    {
        EngineConfig cfg;
        cfg.source = SourceInfo{100, 80};
        cfg.cut.generator = CutGenerator::RECT;
        cfg.cut.rect = RectRegion(30, 20, 70, 60);
        cfg.cut.polarity = Polarity::REMOVE;
        const EngineResult r = runEngine(img, cfg);
        const Grid g = induceGrid(generateCutLines(cfg.cut, cfg.source), cfg.source);
        const RegionSet all = split(img, g);
        CHECK(all.size() == 9);
        CHECK(r.kept.size() == 4); // 9 − 5(十字) = 4，数量守恒
    }

    // ---- Image::crop 左闭右开 + 越界裁剪 + 空区域。----
    {
        Image small(4, 4, idc::core::ImageFormat::RGBA);
        for (int y = 0; y < 4; ++y)
            for (int x = 0; x < 4; ++x)
                small.setPixel(x, y, idc::core::Color(static_cast<std::uint8_t>(x * 60),
                                                      static_cast<std::uint8_t>(y * 60), 0, 255));
        const Image c = small.crop(1, 1, 3, 3); // [1,3)×[1,3) → 2×2
        CHECK(c.width() == 2 && c.height() == 2);
        CHECK(c.getPixel(0, 0) == small.getPixel(1, 1)); // 左上对齐
        CHECK(c.getPixel(1, 1) == small.getPixel(2, 2)); // 右下不含边界 3
        const Image clamped = small.crop(2, 2, 99, 99);  // 越界自动裁剪到图像边界
        CHECK(clamped.width() == 2 && clamped.height() == 2);
        const Image emptyCrop = small.crop(2, 2, 2, 4);  // 零宽 → 空图
        CHECK(emptyCrop.empty());
    }

    std::cout << "[engine-boundary] OK\n";
}
