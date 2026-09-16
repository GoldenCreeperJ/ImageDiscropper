// ============================================================================
// 文件：tests/test_engine_export.cpp
// 作用：导出验收（终稿 §5）——分离导出写入文件夹（含自动创建）、坍缩合并单图、
//       坍缩不可行降级重排、PNG/BMP/JPEG/WebP 四格式写盘成功。
// 分块依据：本文件是唯一“落盘”的测试段，写入系统临时目录并在结束时清理；
//       计算路径见 test_engine_pipeline.cpp，边界异常见 test_engine_boundary.cpp。
// 说明：分离导出不再打包 ZIP，而是把每张图直接写入指定文件夹；此处校验文件夹
//       被自动创建、其中的 PNG 文件数量与命名符合模板。
// ============================================================================
#include "test_harness.h"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "core/image.h"
#include "engine/engine.h"

namespace fs = std::filesystem;

namespace {

// 构造带渐变的 RGBA 图。
idc::core::Image makeImage(const int w, const int h) {
    idc::core::Image img(w, h, idc::core::ImageFormat::RGBA);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
            img.setPixel(x, y, idc::core::Color(static_cast<std::uint8_t>((x * 5) % 256),
                                                static_cast<std::uint8_t>((y * 9) % 256),
                                                100, 255));
    return img;
}

// 读取整个文件为字节。
std::vector<std::uint8_t> readAll(const fs::path& p) {
    std::ifstream ifs(p, std::ios::binary);
    return std::vector<std::uint8_t>((std::istreambuf_iterator<char>(ifs)),
                                     std::istreambuf_iterator<char>());
}

} // namespace

// 导出功能验收（§5）。
void testEngineExport() {
    using namespace idc::engine;
    using idc::core::Image;

    // 独立临时目录，测试结束清理。
    const fs::path dir = fs::temp_directory_path() / "idc_engine_export_test";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir, ec);
    CHECK(!ec);

    const Image img = makeImage(100, 80);

    // ---- L2 十字剔除 → 分离导出到文件夹（四角 4 张 PNG，不压缩）。----
    EngineConfig cfg;
    cfg.source = SourceInfo{100, 80};
    cfg.cut.tier = Tier::L2;
    cfg.cut.generator = CutGenerator::RECT;
    cfg.cut.rect = RectRegion(30, 20, 70, 60);
    cfg.cut.polarity = Polarity::REMOVE;
    cfg.emitParams.mode = EmitMode::SEPARATE; // 分离
    {
        const EngineResult r = runEngine(img, cfg);
        CHECK(r.ok);
        CHECK(r.kept.size() == 4);
        const fs::path outDir = dir / "corners";   // 目标文件夹（尚不存在）
        CHECK(exportImage(r.composition, img, outDir.string()));
        CHECK(fs::is_directory(outDir));           // 文件夹被自动创建。
        // 命名模板默认 {name}_{index:03d}，name=文件夹名 "corners" → corners_000.png …
        CHECK(fs::exists(outDir / "corners_000.png"));
        int pngCount = 0;
        for (const auto& e : fs::directory_iterator(outDir))
            if (e.is_regular_file() && e.path().extension() == ".png") ++pngCount;
        CHECK(pngCount == 4);                      // 四角 → 4 张 PNG。
    }

    // ---- 多级不存在目录也会被自动创建（E-8：仅创建失败才报错）。----
    {
        const EngineResult r = runEngine(img, cfg); // 仍为 SEPARATE
        const fs::path nested = dir / "a" / "b" / "deep";
        CHECK(!fs::exists(nested));
        CHECK(exportImage(r.composition, img, nested.string()));
        CHECK(fs::is_directory(nested));
        CHECK(fs::exists(nested / "deep_000.png")); // {name}=最内层文件夹名 "deep"
    }

    // ---- 合并坍缩 → 单图，校验画布尺寸 (W−Δx)×(H−Δy) 落盘。----
    cfg.emitParams.mode = EmitMode::MERGED;
    cfg.emitParams.layout = MergeLayout::COLLAPSE;
    {
        const EngineResult r = runEngine(img, cfg);
        CHECK(r.ok);
        CHECK(r.collapsible);
        CHECK(r.composition.canvasWidth == 60 && r.composition.canvasHeight == 40);
        const fs::path png = dir / "collapsed.png";
        CHECK(exportImage(r.composition, img, png.string()));
        CHECK(fs::exists(png) && fs::file_size(png, ec) > 0);
        // PNG 魔数校验：89 50 4E 47。
        const std::vector<std::uint8_t> b = readAll(png);
        CHECK(b.size() > 4 && b[0] == 0x89 && b[1] == 0x50 && b[2] == 0x4E && b[3] == 0x47);
    }

    // ---- BMP / JPEG 写盘成功。----
    {
        const EngineResult r = runEngine(img, cfg);
        const fs::path bmp = dir / "collapsed.bmp";
        CHECK(exportImage(r.composition, img, bmp.string()));
        CHECK(fs::exists(bmp) && fs::file_size(bmp, ec) > 0);
        const fs::path jpg = dir / "collapsed.jpg";
        CHECK(exportImage(r.composition, img, jpg.string()));
        CHECK(fs::exists(jpg) && fs::file_size(jpg, ec) > 0);
    }

    // ---- WebP 写盘成功（libwebp 补齐；校验 RIFF....WEBP 容器魔数）。----
    {
        const EngineResult r = runEngine(img, cfg);
        const fs::path webp = dir / "collapsed.webp";
        CHECK(exportImage(r.composition, img, webp.string()));
        CHECK(fs::exists(webp) && fs::file_size(webp, ec) > 0);
        // WebP 容器：前 4 字节 "RIFF"，第 8..11 字节 "WEBP"。
        const std::vector<std::uint8_t> b = readAll(webp);
        CHECK(b.size() > 12 && b[0] == 'R' && b[1] == 'I' && b[2] == 'F' && b[3] == 'F'
              && b[8] == 'W' && b[9] == 'E' && b[10] == 'B' && b[11] == 'P');
    }

    // ---- 坍缩不可行 → 自动降级重排（L3 对角选择集）。----
    {
        const Image big = makeImage(300, 300);
        EngineConfig g;
        g.source = SourceInfo{300, 300};
        g.cut.tier = Tier::L3;
        g.cut.generator = CutGenerator::GRID;
        g.cut.grid = GridParams{0, 0, 100, 100, RemainderPolicy::DISCARD}; // 300/100 → 3×3 自动推导
        g.cut.polarity = Polarity::KEEP;
        g.selectedCells = {0, 4, 8}; // 对角：非整行整列 → 不可坍缩。
        g.emitParams.mode = EmitMode::MERGED;
        g.emitParams.layout = MergeLayout::COLLAPSE; // 请求坍缩，但应被降级为重排。
        const EngineResult r = runEngine(big, g);
        CHECK(r.ok);
        CHECK(!r.collapsible); // 判定为不可坍缩。
        // 降级为重排：3 块 100×100 排成一行（cols 缺省 = 保留块数）→ 300×100。
        CHECK(r.composition.canvasWidth == 300 && r.composition.canvasHeight == 100);
        const fs::path out = dir / "rearranged.png";
        CHECK(exportImage(r.composition, big, out.string()));
        CHECK(fs::exists(out) && fs::file_size(out, ec) > 0);
    }

    fs::remove_all(dir, ec); // 清理临时目录。
    std::cout << "[engine-export] OK\n";
}
