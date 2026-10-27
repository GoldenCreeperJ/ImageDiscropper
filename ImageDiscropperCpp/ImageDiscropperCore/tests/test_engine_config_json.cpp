

// ============================================================================
// 文件：tests/test_engine_config_json.cpp
// 作用：配置文件存取验收（SPEC §7）——EngineConfig ↔ JSON 往返、preprocess 队列（含 resize）、
//       可选 annotations 段的带样式标注往返（几何/样式/变换整体快照），以及向后兼容行为
//       （不传集合指针时忽略标注段、无段载入清空集合）。
// 分块依据：本文件只测「序列化语义」，不触碰引擎计算与导出落盘（那些见 test_engine_pipeline /
//       test_engine_export）；写入系统临时目录并在结束时清理。
// ============================================================================
#include "test_harness.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>
#include <vector>

#include "annotation/annotation_set.h"
#include "annotation/annotation_json.h"
#include "annotation/shape_factory.h"
#include "core/color.h"
#include "engine/engine.h"
#include "engine/engine_config_json.h"
#include "geometry/shapes.h"
#include "preprocess/preprocess_config.h"

namespace {

// 浮点近似相等（JSON 双精度往返容差）。
bool nearEq(const double a, const double b) { return std::fabs(a - b) < 1e-9; }

// 构造覆盖四类具体形状 + 变换/样式的标注集合。
idc::annotation::AnnotationSet makeSet() {
    using namespace idc;
    annotation::AnnotationSet set;
    std::vector<annotation::Annotation> items;

    // ① 矩形（PolygonShape）：圆角 + OBB 变换（缩放 1.5×2 后旋转 30°）+ 变换途中平移
    //   （考验「矩阵 + obb 参数」整体快照：单靠 (sx,sy,deg) 无法复现平移分量的场景）。
    {
        annotation::Annotation a;
        auto poly = std::make_unique<geometry::PolygonShape>(
            std::vector<core::Point2D>{{10, 20}, {60, 20}, {60, 70}, {10, 70}},
            geometry::ShapeType::Rectangle, true, 8.0);
        poly->applyObbTransform(1.5, 2.0, 30.0);
        poly->translateWorld(5.0, -7.0); // 变换后移动：xform_ 不动、几何平移。
        a.style.color = core::Color(255, 140, 0, 255);
        a.style.fillColor = core::Color(0, 0, 0, 64);
        a.style.strokeWidth = 3;
        a.style.fill = true;
        a.shape = std::move(poly);
        items.push_back(std::move(a));
    }
    // ② 椭圆（EllipseShape）：恒等变换（transform 段应省略）。
    {
        annotation::Annotation a;
        a.style.color = core::kRed;
        a.style.fillColor = core::kRed;
        a.style.strokeWidth = 2;
        a.shape = std::make_unique<geometry::EllipseShape>(100, 50, 80, 40, geometry::ShapeType::Ellipse);
        items.push_back(std::move(a));
    }
    // ③ 文字（TextShape）：位置 + 内容 + 字号。
    {
        annotation::Annotation a;
        a.style.color = core::kWhite;
        a.style.strokeWidth = 1;
        a.shape = std::make_unique<geometry::TextShape>("你好 abc", 33.5, 44.25, 22.5);
        items.push_back(std::move(a));
    }
    // ④ 自由路径（PathShape）：M/L/Q/C/Z 五类段全命中。
    {
        annotation::Annotation a;
        geometry::Path p;
        p.moveTo(0, 0);
        p.lineTo(20, 10);
        p.quadTo(30, 20, 40, 10);
        p.cubicTo(50, 0, 60, 30, 70, 5);
        p.closePath();
        a.style.color = core::kBlue;
        a.style.strokeWidth = 4;
        a.shape = std::make_unique<geometry::PathShape>(std::move(p), geometry::ShapeType::Path);
        items.push_back(std::move(a));
    }
    set.restore(std::move(items));
    return set;
}

} // namespace

// 配置 JSON 存取验收（SPEC §7，含 annotations 段）。
void testEngineConfigJson() {
    using namespace idc;
    namespace fs = std::filesystem;

    const fs::path dir = fs::temp_directory_path() / "idc_config_json_test";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir, ec);
    const fs::path file = dir / "cfg.json";

    // ---- 基座配置：source + 声明式队列（resize→rotate）+ cut/export 非默认值。----
    engine::EngineConfig cfg;
    cfg.source = engine::SourceInfo{800, 600};
    cfg.preprocess.add(preprocess::ResizeOp{400, 300});
    cfg.preprocess.add(preprocess::RotateOp{90});
    cfg.cut.tier = engine::Tier::Grid;
    cfg.cut.generator = engine::CutGenerator::Grid;
    cfg.cut.grid = engine::GridParams{.originX = 0, .originY = 0, .cellWidth = 100, .cellHeight = 100, .remainder = engine::RemainderPolicy::Pad};
    cfg.cut.polarity = engine::Polarity::Keep;
    cfg.selectedCells = {0, 4};
    cfg.exportParams.mode = engine::ExportMode::Merged;
    cfg.exportParams.layout = engine::MergeLayout::Rearrange;
    cfg.exportParams.cols = 3;
    cfg.exportParams.padColor = core::Color(0, 0, 0, 0);

    // 多层画布 layers 段：一配方层（带逐层 preprocess + transform）+ 一文件层（仅相对名/落位）。
    {
        std::vector<canvas::ImageLayer> st;
        canvas::ImageLayer l0;
        l0.id = 7;
        generate::PatternParams pp;
        pp.kind = generate::PatternKind::Checkerboard;
        pp.period = 4;
        pp.background = core::kWhite;
        pp.foreground = core::kBlack;
        l0.backdrop = pp;
        l0.transform.width = 8; l0.transform.height = 8;
        l0.transform.inCanvasAnchor = generate::ImageAnchor::TopLeft;
        l0.transform.opacity = 180;   // 层不透明度（合成期 alpha 乘子）：验证往返。
        l0.zOrder = 0;
        l0.preprocess.add(preprocess::GrayOp{});
        canvas::ImageLayer l1;
        l1.id = 9;
        l1.fileName = "sub/a.png";   // 相对名（含子目录）；像素不入配置
        l1.transform.x = 3; l1.transform.y = -2;
        l1.transform.layerPivot = generate::ImageAnchor::BottomRight;
        l1.zOrder = 1;
        st = {l0, l1};
        cfg.layers = st;
    }

    annotation::AnnotationSet layer = makeSet();

    // ---- 往返：带标注保存 → 全新对象加载。----
    CHECK(engine::saveEngineConfig(file.string(), cfg, &layer));
    engine::EngineConfig got;
    annotation::AnnotationSet gotLayer;
    CHECK(engine::loadEngineConfig(file.string(), got, &gotLayer));

    // 配置字段一致。
    CHECK(got.source.width == 800 && got.source.height == 600);
    CHECK(got.preprocess.size() == 2);
    CHECK(std::holds_alternative<preprocess::ResizeOp>(got.preprocess.items()[0]));
    CHECK(std::get<preprocess::ResizeOp>(got.preprocess.items()[0]).width == 400);
    CHECK(std::get<preprocess::RotateOp>(got.preprocess.items()[1]).angle == 90);
    CHECK(got.cut.tier == engine::Tier::Grid && got.cut.generator == engine::CutGenerator::Grid);
    CHECK(got.cut.grid.cellWidth == 100 && got.cut.grid.remainder == engine::RemainderPolicy::Pad);
    CHECK(got.selectedCells.size() == 2 && got.selectedCells[1] == 4);
    CHECK(got.exportParams.layout == engine::MergeLayout::Rearrange && got.exportParams.cols == 3);

    // layers 段往返：配方层（含逐层 preprocess + transform）/ 文件层（仅相对名，像素不入配置）。
    CHECK(got.layers.has_value());
    if (got.layers.has_value()) {
        const auto& layers = *got.layers;
        CHECK(layers.size() == 2);
        if (layers.size() == 2) {
            const canvas::ImageLayer& r0 = layers[0];
            CHECK(r0.backdrop.has_value());
            CHECK(r0.backdrop.has_value() && r0.backdrop->kind == generate::PatternKind::Checkerboard && r0.backdrop->period == 4);
            CHECK(r0.transform.width == 8 && r0.transform.height == 8);
            CHECK(r0.transform.inCanvasAnchor == generate::ImageAnchor::TopLeft);
            CHECK(r0.transform.opacity == 180);   // 不透明度往返一致
            CHECK(r0.zOrder == 0 && r0.preprocess.size() == 1);
            CHECK(std::holds_alternative<preprocess::GrayOp>(r0.preprocess.items()[0]));
            const canvas::ImageLayer& r1 = layers[1];
            CHECK(!r1.backdrop.has_value() && !r1.fileName.empty());
            CHECK(r1.fileName == "sub/a.png");
            CHECK(r1.decodedPixels.empty());   // 像素不经配置还原
            CHECK(r1.transform.x == 3 && r1.transform.y == -2);
            CHECK(r1.transform.layerPivot == generate::ImageAnchor::BottomRight);
            CHECK(r1.zOrder == 1);
        }
    }

    // 标注数量与顺序一致。
    CHECK(gotLayer.annotations().size() == 4);
    // 数量不符即为核心性失败（后续逐字段断言会越界）：跳过余下断言，但先清临时目录再退。
    if (gotLayer.annotations().size() != 4) {
        fs::remove_all(dir, ec);
        return;
    }
    const auto& annos = gotLayer.annotations();

    // zOrder 往返：导出逐条写入且与位置一致，加载重排规范化后 zOrder 恒等于下标 0..3。
    CHECK(annos[0].zOrder == 0 && annos[1].zOrder == 1 && annos[2].zOrder == 2 && annos[3].zOrder == 3);

    // ① 矩形：类型/圆角/样式 + 变换整体快照（矩阵与累积参数逐项相等）。
    {
        const annotation::Annotation& a = annos[0];
        CHECK(a.shape->type() == geometry::ShapeType::Rectangle);
        CHECK(a.style.color == core::Color(255, 140, 0, 255) && a.style.fillColor == core::Color(0, 0, 0, 64));
        CHECK(a.style.strokeWidth == 3 && a.style.fill);
        const auto* poly = dynamic_cast<const geometry::PolygonShape*>(a.shape.get());
        CHECK(poly != nullptr);
        if (poly) {
            CHECK(poly->points().size() == 4);
            CHECK(nearEq(poly->cornerRadius(), 8.0));
            CHECK(poly->closed());
            const auto* src = dynamic_cast<const geometry::PolygonShape*>(layer.annotations()[0].shape.get());
            CHECK(src != nullptr);
            if (src) {
                for (std::size_t i = 0; i < poly->points().size(); ++i) {
                    CHECK(nearEq(poly->points()[i].x, src->points()[i].x));
                    CHECK(nearEq(poly->points()[i].y, src->points()[i].y));
                }
                // 世界路径一致（平移分量也由矩阵快照复现）。
                const geometry::Path want = src->worldPath();
                const geometry::Path have = poly->worldPath();
                CHECK(want.segments().size() == have.segments().size());
                for (std::size_t i = 0; i < want.segments().size(); ++i) {
                    CHECK(want.segments()[i].coords.size() == have.segments()[i].coords.size());
                    for (std::size_t j = 0; j < want.segments()[i].coords.size(); ++j)
                        CHECK(nearEq(want.segments()[i].coords[j], have.segments()[i].coords[j]));
                }
            }
            CHECK(nearEq(a.shape->obbScaleX(), 1.5));
            CHECK(nearEq(a.shape->obbScaleY(), 2.0));
            CHECK(nearEq(a.shape->obbRotationDeg(), 30.0));
        }
    }
    // ② 椭圆：包围盒与类型。
    {
        const annotation::Annotation& a = annos[1];
        CHECK(a.shape->type() == geometry::ShapeType::Ellipse);
        CHECK(a.style.color == core::kRed && !a.style.fill);
        const auto* ell = dynamic_cast<const geometry::EllipseShape*>(a.shape.get());
        CHECK(ell != nullptr);
        if (ell) {
            CHECK(nearEq(ell->centerX(), 140.0) && nearEq(ell->centerY(), 70.0));
            CHECK(nearEq(ell->radiusX(), 40.0) && nearEq(ell->radiusY(), 20.0));
            CHECK(ell->transform().isIdentity());
        }
    }
    // ③ 文字：位置/内容/字号（TextShape 的 x/y 访问器路径）。
    {
        const auto* text = dynamic_cast<const geometry::TextShape*>(annos[2].shape.get());
        CHECK(text != nullptr);
        if (text) {
            CHECK(text->text() == "你好 abc");
            CHECK(nearEq(text->x(), 33.5) && nearEq(text->y(), 44.25));
            CHECK(nearEq(text->fontSize(), 22.5));
        }
        CHECK(annos[2].style.strokeWidth == 1);
    }
    // ④ 路径：五类段与全部坐标。
    {
        const auto* pathShape = dynamic_cast<const geometry::PathShape*>(annos[3].shape.get());
        CHECK(pathShape != nullptr);
        if (pathShape) {
            const auto& want = dynamic_cast<const geometry::PathShape&>(*layer.annotations()[3].shape).path();
            const auto& have = pathShape->path();
            CHECK(want.segments().size() == have.segments().size() && have.segments().size() == 5);
            for (std::size_t i = 0; i < want.segments().size(); ++i) {
                CHECK(want.segments()[i].type == have.segments()[i].type);
                CHECK(want.segments()[i].coords == have.segments()[i].coords);
            }
        }
    }

    // ---- 向后兼容：不传集合保存 → 文件无 annotations 段；不传集合加载 → 标注段被忽略。----
    const fs::path plainFile = dir / "plain.json";
    CHECK(engine::saveEngineConfig(plainFile.string(), cfg));
    {
        std::ifstream ifs(plainFile);
        const std::string text((std::istreambuf_iterator(ifs)), std::istreambuf_iterator<char>());
        CHECK(text.find("\"annotations\"") == std::string::npos);
    }
    CHECK(engine::loadEngineConfig(plainFile.string(), got)); // 缺省尾参的调用形态仍可编译（默认参数）。

    // ---- 真相源语义：文件无 annotations 段 + 传集合加载 → 清空既有标注。----
    annotation::AnnotationSet keep = makeSet();
    CHECK(engine::loadEngineConfig(plainFile.string(), got, &keep));
    CHECK(keep.annotations().empty());

    // ---- 空集合保存 → 省略 annotations 段。----
    const fs::path emptyFile = dir / "empty.json";
    annotation::AnnotationSet emptyLayer;
    CHECK(engine::saveEngineConfig(emptyFile.string(), cfg, &emptyLayer));
    {
        std::ifstream ifs(emptyFile);
        const std::string text((std::istreambuf_iterator(ifs)), std::istreambuf_iterator<char>());
        CHECK(text.find("\"annotations\"") == std::string::npos);
    }

    // ---- 底图 generate 段（同构 ImageLayer）往返：纯图案底 + 落位声明底（不存文件路径、box 尺寸由 source 派生不落盘）。----
    {
        // ① 纯图案底（图案元素、无图片元素）：序列化后 generate 段仅 backdrop 对象（无 file/落位），解析回 backdrop。
        const fs::path patFile = dir / "base_pattern.json";
        engine::EngineConfig pc;
        pc.source = engine::SourceInfo{64, 48};
        canvas::ImageLayer pb;
        generate::PatternParams ppp;
        ppp.kind = generate::PatternKind::Stripes;
        ppp.period = 6;
        pb.backdrop = ppp;
        pc.base = pb;
        CHECK(engine::saveEngineConfig(patFile.string(), pc));
        {
            std::ifstream ifs(patFile);
            const std::string text((std::istreambuf_iterator(ifs)), std::istreambuf_iterator<char>());
            CHECK(text.find("\"generate\"") != std::string::npos);
            CHECK(text.find("\"file\"") == std::string::npos);   // 纯图案底不含 file/落位键
        }
        engine::EngineConfig pg;
        CHECK(engine::loadEngineConfig(patFile.string(), pg));
        CHECK(pg.base.has_value());
        if (pg.base.has_value()) {
            CHECK(pg.base->backdrop.has_value());
            CHECK(pg.base->backdrop.has_value() && pg.base->backdrop->kind == generate::PatternKind::Stripes && pg.base->backdrop->period == 6);
            CHECK(!pg.base->hasImagePlacement);
            CHECK(pg.base->fileName.empty());
            CHECK(pg.base->transform.opacity == 255);   // 底图整体不透明度：默认 255 往返（无显式赋值时）
        }

        // ② 落位声明底（图片元素内层落位 + 图案元素）：仅落位键往返；文件路径与位图均不入配置（运行时由 --input 供位图）。
        const fs::path fileBase = dir / "base_file.json";
        engine::EngineConfig fc;
        fc.source = engine::SourceInfo{200, 100};
        canvas::ImageLayer fb;
        fb.fileName = "photo.png";
        fb.hasImagePlacement = true;
        fb.imageFit = canvas::ImageFit::Stretch;
        fb.boxWidth = 200; fb.boxHeight = 100;
        fb.imageAnchor = generate::ImageAnchor::TopLeft;
        fb.imagePivot = generate::ImageAnchor::BottomRight;
        fb.imageX = 7; fb.imageY = -3;
        generate::PatternParams back;
        back.kind = generate::PatternKind::Solid;
        back.background = core::Color(10, 20, 30, 255);
        fb.backdrop = back;
        fb.decodedPixels = core::Image(2, 2, core::ImageFormat::Rgba);   // 位图不入配置：往返后应为空。
        fb.transform.opacity = 64;   // 底图整体不透明度（非默认）：验证随 generate 段 opacity 键往返。
        fc.base = fb;
        CHECK(engine::saveEngineConfig(fileBase.string(), fc));
        {
            std::ifstream ifs(fileBase);
            const std::string text((std::istreambuf_iterator(ifs)), std::istreambuf_iterator<char>());
            CHECK(text.find("boxWidth") == std::string::npos);   // 底图落位框尺寸不落盘（恒＝source，载入由 source 回填）
            CHECK(text.find("\"anchor\"") != std::string::npos); // 落位键位于 image 元素内
        }
        engine::EngineConfig fg;
        CHECK(engine::loadEngineConfig(fileBase.string(), fg));
        CHECK(fg.base.has_value());
        if (fg.base.has_value()) {
            const canvas::ImageLayer& b = *fg.base;
            CHECK(b.backdrop.has_value());   // 图案元素（落位声明底可带图案元素）
            CHECK(b.fileName.empty());                               // 文件路径不入配置
            CHECK(b.decodedPixels.empty());   // 位图不经配置还原
            CHECK(b.hasImagePlacement);
            CHECK(b.imageFit == canvas::ImageFit::Stretch);
            CHECK(b.boxWidth == 200 && b.boxHeight == 100);
            CHECK(b.imageAnchor == generate::ImageAnchor::TopLeft);
            CHECK(b.imagePivot == generate::ImageAnchor::BottomRight);
            CHECK(b.imageX == 7 && b.imageY == -3);
            CHECK(b.backdrop.has_value() && b.backdrop->background == core::Color(10, 20, 30, 255));
            CHECK(b.transform.opacity == 64);   // 底图整体不透明度往返一致
        }

        // ③ 顶层 kind 平铺（非本段键）：不识别、按未知键静默忽略。
        const fs::path legacyGen = dir / "legacy_gen.json";
        {
            std::ofstream ofs(legacyGen);
            ofs << R"J({"source":{"width":16,"height":16},"generate":{"kind":"grid-lines","period":8}})J";
        }
        engine::EngineConfig lg;
        CHECK(engine::loadEngineConfig(legacyGen.string(), lg));   // 载入成功（不再拒载）
        CHECK(lg.base.has_value());                                // 有 generate 段 → 底图声明在
        if (lg.base.has_value()) {
            CHECK(!lg.base->backdrop.has_value());                 // 平铺图案键被忽略：无图案元素
            CHECK(!canvas::hasImageElement(*lg.base));             // 亦无图片元素
        }
    }

    // ---- 元素外观几何步分化往返：图案元素外观仅色彩步（几何步写出过滤/还原丢弃），图片元素外观可含几何步 ----
    {
        const fs::path f = dir / "elem_appearance.json";
        engine::EngineConfig w;
        w.source = engine::SourceInfo{16, 16};
        canvas::ImageLayer l1;   // 图案元素外观：rotate(几何) + gray(色彩) → 往返后仅 gray
        l1.backdrop = generate::PatternParams{};
        l1.backdropPreprocess.add(preprocess::RotateOp{90});
        l1.backdropPreprocess.add(preprocess::GrayOp{});
        canvas::ImageLayer l2;   // 图片元素外观：rotate(几何) + invert(色彩) → 全保留
        l2.hasImagePlacement = true;
        l2.boxWidth = 8; l2.boxHeight = 8;
        l2.imagePreprocess.add(preprocess::RotateOp{90});
        l2.imagePreprocess.add(preprocess::InvertOp{"111"});
        w.layers = std::vector{l1, l2};
        CHECK(engine::saveEngineConfig(f.string(), w));
        engine::EngineConfig r;
        CHECK(engine::loadEngineConfig(f.string(), r));
        CHECK(r.layers.has_value() && r.layers->size() == 2);
        if (r.layers.has_value() && r.layers->size() == 2) {
            CHECK((*r.layers)[0].backdropPreprocess.items().size() == 1);
            CHECK(std::holds_alternative<preprocess::GrayOp>((*r.layers)[0].backdropPreprocess.items().front()));
            CHECK((*r.layers)[1].imagePreprocess.items().size() == 2);   // 几何+色彩全保留
        }
    }

    // ---- 画布级顶层 opacity（EngineConfig::canvasOpacity）往返：非默认值命中 + 读侧越界钳制 + 旧配置无键缺省 255。----
    {
        const fs::path canvasOpFile = dir / "canvas_opacity.json";
        engine::EngineConfig cc;
        cc.source = engine::SourceInfo{32, 32};
        cc.canvasOpacity = 96;
        CHECK(engine::saveEngineConfig(canvasOpFile.string(), cc));
        {
            std::ifstream ifs(canvasOpFile);
            const std::string text((std::istreambuf_iterator(ifs)), std::istreambuf_iterator<char>());
            CHECK(text.find("\"opacity\"") != std::string::npos);   // 顶层 opacity 键写出
        }
        engine::EngineConfig cg;
        CHECK(engine::loadEngineConfig(canvasOpFile.string(), cg));
        CHECK(cg.canvasOpacity == 96);   // 画布级不透明度往返一致

        // 读侧越界钳制：opacity 300 → 夹为 255。
        const fs::path clampFile = dir / "canvas_opacity_clamp.json";
        {
            std::ofstream ofs(clampFile);
            ofs << R"J({"source":{"width":8,"height":8},"opacity":300})J";
        }
        engine::EngineConfig clamped;
        CHECK(engine::loadEngineConfig(clampFile.string(), clamped));
        CHECK(clamped.canvasOpacity == 255);

        // 缺顶层 opacity → 缺省 255、行为不变；generate 段无 preprocess → 底图预处理为空（backdrop 元素形态）。
        const fs::path legacyNoOp = dir / "no_opacity.json";
        {
            std::ofstream ofs(legacyNoOp);
            ofs << R"J({"source":{"width":8,"height":8},"generate":{"backdrop":{"kind":"solid"}}})J";
        }
        engine::EngineConfig noOp;
        CHECK(engine::loadEngineConfig(legacyNoOp.string(), noOp));
        CHECK(noOp.canvasOpacity == 255);
        CHECK(noOp.base.has_value() && noOp.base->preprocess.empty());
    }

    // ---- 底图 generate 段 preprocess（色彩步）往返：仅色彩步保留、几何/resize 步写出时被过滤（底图预处理不改画布尺寸）。----
    {
        const fs::path basePreFile = dir / "base_preprocess.json";
        engine::EngineConfig bp;
        bp.source = engine::SourceInfo{40, 40};
        canvas::ImageLayer pb;
        generate::PatternParams ppp;
        ppp.kind = generate::PatternKind::Solid;
        pb.backdrop = ppp;
        pb.preprocess.add(preprocess::GrayOp{});                        // 色彩步：保留
        pb.preprocess.add(preprocess::InvertOp{std::string("110")});    // 色彩步：保留
        pb.preprocess.add(preprocess::RotateOp{90});                    // 几何步：写出时被过滤
        bp.base = pb;
        CHECK(engine::saveEngineConfig(basePreFile.string(), bp));
        {
            std::ifstream ifs(basePreFile);
            const std::string text((std::istreambuf_iterator(ifs)), std::istreambuf_iterator<char>());
            CHECK(text.find("\"preprocess\"") != std::string::npos);
            CHECK(text.find("\"rotate\"") == std::string::npos);   // 几何步不写出
        }
        engine::EngineConfig bg;
        CHECK(engine::loadEngineConfig(basePreFile.string(), bg));
        CHECK(bg.base.has_value());
        if (bg.base.has_value()) {
            CHECK(bg.base->preprocess.size() == 2);   // 仅色彩步还原
            if (bg.base->preprocess.size() == 2) {
                CHECK(std::holds_alternative<preprocess::GrayOp>(bg.base->preprocess.items()[0]));
                CHECK(std::holds_alternative<preprocess::InvertOp>(bg.base->preprocess.items()[1]));
                CHECK(std::get<preprocess::InvertOp>(bg.base->preprocess.items()[1]).mask == "110");
            }
        }
    }

    fs::remove_all(dir, ec);
    std::cout << "[engine-config-json] OK\n";
}

// 标注 JSON 文本段级解析（parseAnnotationsFromJsonText）：单对象 / 数组 / 累积 / 语法错误回滚 / 退化 / 空串。
void testAnnoParseText() {
    using namespace idc;

    // ① 单条对象 → 追加 1 条（矩形，锚点形式，带样式）。
    {
        std::vector<annotation::Annotation> out;
        std::string err;
        CHECK(annotation::parseAnnotationsFromJsonText(
            R"J({"type":"rectangle","x1":0,"y1":0,"x2":10,"y2":10,"color":"#FF112233","fill":true,"strokeWidth":3})J",
            out, err));
        CHECK(out.size() == 1 && out[0].shape != nullptr);
        CHECK(out[0].shape->type() == geometry::ShapeType::Rectangle);
        CHECK(out[0].style.strokeWidth == 3 && out[0].style.fill);
        CHECK(err.empty());
    }

    // ② 根为数组 → 多条。
    {
        std::vector<annotation::Annotation> out;
        std::string err;
        CHECK(annotation::parseAnnotationsFromJsonText(
            R"J([{"type":"circle","x1":10,"y1":10,"x2":20,"y2":10},{"type":"text","x":5,"y":5,"text":"hi","fontSize":10}])J",
            out, err));
        CHECK(out.size() == 2);
        CHECK(out[0].shape->type() == geometry::ShapeType::Circle);
        CHECK(out[1].shape->type() == geometry::ShapeType::Text);
        // 无显式 zOrder 字段 → 回退为数组下标 0/1。
        CHECK(out[0].zOrder == 0 && out[1].zOrder == 1);
    }

    // ②b 显式乱序 zOrder → 批内按 zOrder 稳定重排（等值保序），并规范化回下标。
    //     入档顺序 strokeWidth 1(zOrder 5) / 9(zOrder 0) → 排序后 9(底) 在前、1(顶) 在后。
    {
        std::vector<annotation::Annotation> out;
        std::string err;
        CHECK(annotation::parseAnnotationsFromJsonText(
            R"J([{"type":"line","points":[[0,0],[1,1]],"strokeWidth":1,"zOrder":5},{"type":"line","points":[[2,2],[3,3]],"strokeWidth":9,"zOrder":0}])J",
            out, err));
        CHECK(out.size() == 2);
        CHECK(out[0].style.strokeWidth == 9 && out[1].style.strokeWidth == 1);
        // 批内排序后由调用方（restore）会规范化 zOrder＝下标；此处仅验证相对顺序。
    }

    // ③ 多次调用累积（追加语义）：先 1 条后 2 条 → 共 3。
    {
        std::vector<annotation::Annotation> out;
        std::string err;
        CHECK(annotation::parseAnnotationsFromJsonText(R"J({"type":"line","points":[[0,0],[9,9]]})J", out, err));
        CHECK(out.size() == 1);
        CHECK(annotation::parseAnnotationsFromJsonText(
            R"J([{"type":"line","points":[[0,0],[1,1]]},{"type":"line","points":[[2,2],[3,3]]}])J", out, err));
        CHECK(out.size() == 3);
    }

    // ④ 语法错误 → false，且回滚（不污染已累积条目）。
    {
        std::vector<annotation::Annotation> out;
        std::string err;
        CHECK(annotation::parseAnnotationsFromJsonText(R"J({"type":"line","points":[[0,0],[1,1]]})J", out, err));
        const std::size_t n = out.size();
        CHECK(!annotation::parseAnnotationsFromJsonText("{ not valid json ", out, err));
        CHECK(out.size() == n);
        CHECK(!err.empty());
    }

    // ⑤ 锚点退化（矩形 dx=0，无法构成非退化形状）→ false（强制校验拒绝）。
    {
        std::vector<annotation::Annotation> out;
        std::string err;
        CHECK(!annotation::parseAnnotationsFromJsonText(R"J({"type":"rectangle","x1":10,"y1":10,"x2":10,"y2":30})J", out, err));
        CHECK(out.empty());
        CHECK(!err.empty());
    }

    // ⑦ 逐条强制校验负例：任一非法即整体失败且 out 不回溯污染。
    {
        const std::vector<std::string> bad = {
            R"J({"type":"hexagon","x1":0,"y1":0,"x2":5,"y2":5})J",                 // 未知 type
            R"J({"x1":0,"y1":0,"x2":5,"y2":5})J",                                    // 缺 type
            R"J({"type":"line","points":[[0,0],[1,1],[2,2]]})J",                     // 直线点数≠ 2
            R"J({"type":"ellipse","x1":0,"y1":0,"x2":8,"y2":0})J",                  // 椭圆半轴 dy=0 退化
            R"J({"type":"circle","x1":5,"y1":5,"x2":5,"y2":5})J",                   // 圆半径 0 退化
            R"J({"type":"line","points":[[0,0],[1,1]],"color":"#ZZ"})J",            // 非法 hex
        };
        for (const std::string& item : bad) {
            std::vector<annotation::Annotation> out;
            std::string err;
            CHECK(!annotation::parseAnnotationsFromJsonText(item, out, err));
            CHECK(out.empty());
            CHECK(!err.empty());
        }
        // 批内合法 + 非法混合 → 整体失败，out 保持调用前不变。
        std::vector<annotation::Annotation> out;
        std::string err;
        CHECK(!annotation::parseAnnotationsFromJsonText(
            R"J([{"type":"line","points":[[0,0],[1,1]]},{"type":"line","points":[[0,0]]}])J", out, err));
        CHECK(out.empty()); // 合法一条也不应落盘（任一条非法即整批失败）。
        CHECK(!err.empty());
    }

    // ⑥ 空串 → false。
    {
        std::vector<annotation::Annotation> out;
        std::string err;
        CHECK(!annotation::parseAnnotationsFromJsonText("", out, err));
    }

    // ⑧ 几何百分比（段级入口）：按给定基准分方向换算（x 用宽、y 用高）；>100% 与负值合法（与 §7 标注段同口径）。
    {
        std::vector<annotation::Annotation> out;
        std::string err;
        CHECK(annotation::parseAnnotationsFromJsonText(R"J([
            {"type":"rectangle","x1":"0%","y1":"0%","x2":"50%","y2":"100%"},
            {"type":"line","points":[["-10%","-10%"],["150%","150%"]]},
            {"type":"path","segments":[{"t":"M","coords":["25%","50%"]},{"t":"L","coords":["100%","100%"]}]}])J",
            out, err, 200, 100));
        CHECK(out.size() == 3);
        if (out.size() == 3) {
            const auto* rect = dynamic_cast<const geometry::PolygonShape*>(out[0].shape.get());
            CHECK(rect != nullptr);
            if (rect != nullptr) {
                double minx = 1e18, miny = 1e18, maxx = -1e18, maxy = -1e18;
                for (const core::Point2D& p : rect->points()) {
                    minx = std::min(minx, p.x); miny = std::min(miny, p.y);
                    maxx = std::max(maxx, p.x); maxy = std::max(maxy, p.y);
                }
                CHECK(nearEq(minx, 0.0) && nearEq(miny, 0.0) && nearEq(maxx, 100.0) && nearEq(maxy, 100.0));
            }
            const auto* line = dynamic_cast<const geometry::PolygonShape*>(out[1].shape.get());
            CHECK(line != nullptr && line->points().size() == 2);
            if (line != nullptr && line->points().size() == 2) {
                CHECK(nearEq(line->points()[0].x, -20.0) && nearEq(line->points()[0].y, -10.0));
                CHECK(nearEq(line->points()[1].x, 300.0) && nearEq(line->points()[1].y, 150.0));
            }
            const auto* path = dynamic_cast<const geometry::PathShape*>(out[2].shape.get());
            CHECK(path != nullptr);
            if (path != nullptr) {
                CHECK(nearEq(path->path().segments()[0].coords[0], 50.0));   // 25% × 200
                CHECK(nearEq(path->path().segments()[0].coords[1], 50.0));   // 50% × 100
                CHECK(nearEq(path->path().segments()[1].coords[0], 200.0));
                CHECK(nearEq(path->path().segments()[1].coords[1], 100.0));
            }
        }
    }

    // ⑨ 写百分比却未给基准（缺省 0）→ 拒绝且报因定位到字段，不静默归零（回归护栏）。
    {
        std::vector<annotation::Annotation> out;
        std::string err;
        CHECK(!annotation::parseAnnotationsFromJsonText(
            R"J({"type":"rectangle","x1":"0%","y1":"0%","x2":"50%","y2":"50%"})J", out, err));
        CHECK(out.empty());
        CHECK(err.find("'x1'") != std::string::npos);
    }

    // ⑩ 既非整数也非百分比串的几何字段 → 仍报「缺少或非法数值字段」。
    {
        std::vector<annotation::Annotation> out;
        std::string err;
        CHECK(!annotation::parseAnnotationsFromJsonText(
            R"J({"type":"line","points":[["wide%","0%"],["1","1"]]})J", out, err, 100, 100));
        CHECK(err.find("'points[0][0]'") != std::string::npos);
    }

    std::cout << "[anno-parse-text] OK\n";
}

// 图层 JSON 文本段级解析（parseImageLayersFromJsonText）：单对象 / 数组 / 累积 / 根类型非法 / 语法错误 / 空串。
void testLayerParseText() {
    using namespace idc;

    // ① 单条对象 → 追加 1 条（file 源 + transform + zOrder）。
    {
        std::vector<canvas::ImageLayer> out;
        std::string err;
        CHECK(engine::parseImageLayersFromJsonText(
            R"J({"image":{"file":"sub/a.png"},"transform":{"x":10,"y":-5,"opacity":128},"zOrder":2})J", out, err));
        CHECK(out.size() == 1);
        CHECK(out[0].fileName == "sub/a.png" && out[0].zOrder == 2);
        CHECK(out[0].transform.x == 10 && out[0].transform.y == -5 && out[0].transform.opacity == 128);
    }

    // ② 根数组 → 按序追加 2 条（纯图案层与图片层混排，元素分块）。
    {
        std::vector<canvas::ImageLayer> out;
        std::string err;
        CHECK(engine::parseImageLayersFromJsonText(
            R"J([{"backdrop":{"kind":"checkerboard","period":4},"zOrder":1},
                {"image":{"file":"b.png"}}])J", out, err));
        CHECK(out.size() == 2);
        CHECK(out[0].backdrop.has_value() && out[0].zOrder == 1);
        CHECK(out[1].fileName == "b.png" && out[1].zOrder == 0);   // 未给 zOrder → 缺省 0
    }

    // ③ 累积：二次调用追加到既有列表之后（CLI --layer 可重复的同构语义）。
    {
        std::vector<canvas::ImageLayer> out;
        std::string err;
        CHECK(engine::parseImageLayersFromJsonText(R"J({"image":{"file":"a.png"}})J", out, err));
        CHECK(engine::parseImageLayersFromJsonText(R"J({"image":{"file":"b.png"}})J", out, err));
        CHECK(out.size() == 2 && out[1].fileName == "b.png");
    }

    // ④ 根类型非法（标量）→ 失败且 err 非空、out 不动。
    {
        std::vector<canvas::ImageLayer> out{{}};
        std::string err;
        CHECK(!engine::parseImageLayersFromJsonText(R"J(42)J", out, err));
        CHECK(!err.empty() && out.size() == 1);   // 失败回滚：不改动 out
    }

    // ⑤ JSON 语法错误 → 失败透传 parse_error 详情。
    {
        std::vector<canvas::ImageLayer> out;
        std::string err;
        CHECK(!engine::parseImageLayersFromJsonText("{ not json", out, err));
        CHECK(!err.empty());
    }

    // ⑥ 空串 → 失败且 err 非空。
    {
        std::vector<canvas::ImageLayer> out;
        std::string err;
        CHECK(!engine::parseImageLayersFromJsonText("", out, err));
        CHECK(!err.empty());
    }

    // ⑦ transform 百分比（段级入口）：x/width 用宽、y/height 用高；>100% 与负值合法。
    {
        std::vector<canvas::ImageLayer> out;
        std::string err;
        CHECK(engine::parseImageLayersFromJsonText(
            R"J({"image":{"file":"a.png"},"transform":{"x":"150%","y":"-10%","width":"25%","height":"200%"}})J",
            out, err, 200, 100));
        CHECK(out.size() == 1);
        if (out.size() == 1) {
            CHECK(out[0].transform.x == 300 && out[0].transform.y == -10);
            CHECK(out[0].transform.width == 50 && out[0].transform.height == 200);
        }
    }

    // ⑧ 写百分比却未给画布尺寸 → 报错（回归护栏：须拒绝，不得静默当 0）。
    {
        std::vector<canvas::ImageLayer> out;
        std::string err;
        CHECK(!engine::parseImageLayersFromJsonText(
            R"J({"image":{"file":"a.png"},"transform":{"x":"50%"}})J", out, err));
        CHECK(out.empty());
        CHECK(err.find("transform.x") != std::string::npos);
    }

    std::cout << "[layer-parse-text] OK\n";
}

// 约束图元锚点往返（计划核心卡点）：buildShape(锚点) → shapeToJson(反解锚点) → shapeFromJson(buildShape)
//   顶点级一致。7 种约束多边形（矩形/正方形/菱形/等腰/等边/直角/等腰直角）各取含正/负象限的锚点，
//   验证“锚点→规范顶点→反解锚点→再生成顶点”不漂移（圆角一并保留）；另加圆/椭圆两例，
//   因其承载为 EllipseShape，改比较包围盒中心与半径。
void testAnnoAnchorRoundTrip() {
    using namespace idc;
    namespace fs = std::filesystem;

    const fs::path dir = fs::temp_directory_path() / "idc_anno_anchor_test";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir, ec);
    const fs::path file = dir / "anchor.json";

    struct Case { geometry::ShapeType type; core::Point2D p1; core::Point2D p2; };
    const std::vector<Case> cases = {
        {geometry::ShapeType::Rectangle,         {10, 20},  {60, 70}},
        {geometry::ShapeType::Square,            {0, 0},    {-10, 6}},    // 负象限生长
        {geometry::ShapeType::Rhombus,           {50, 50},  {80, 30}},
        {geometry::ShapeType::IsosTriangle,     {100, 100},{130, 60}},   // 顶角朝上
        {geometry::ShapeType::IsosTriangle,     {40, 40},  {70, 90}},    // 顶角朝下
        {geometry::ShapeType::EquTriangle,      {50, 50},  {80, 90}},
        {geometry::ShapeType::RectTriangle,     {10, 20},  {50, 40}},
        {geometry::ShapeType::EquRectTriangle, {0, 0},    {-6, 10}},
    };

    // 源集合：逐 case 经 buildShape 生成规范顶点；先抽取期望顶点/圆角（shape 将被移入集合）。
    std::vector<std::vector<core::Point2D>> expectedPts;
    std::vector<double> expectedRadius;
    std::vector<annotation::Annotation> items;
    for (const auto&[type, p1, p2] : cases) {
        annotation::ShapeRequest req;
        req.type = type;
        req.p1 = p1;
        req.p2 = p2;
        req.cornerRadius = 3.0;
        annotation::Annotation a;
        a.shape = annotation::buildShape(req);
        const auto* poly = dynamic_cast<const geometry::PolygonShape*>(a.shape.get());
        CHECK(poly != nullptr);
        expectedPts.push_back(poly ? poly->points() : std::vector<core::Point2D>{});
        expectedRadius.push_back(poly ? poly->cornerRadius() : 0.0);
        items.push_back(std::move(a));
    }

    annotation::AnnotationSet layer;
    layer.restore(std::move(items));

    engine::EngineConfig cfg;
    CHECK(engine::saveEngineConfig(file.string(), cfg, &layer));
    engine::EngineConfig got;
    annotation::AnnotationSet gotLayer;
    CHECK(engine::loadEngineConfig(file.string(), got, &gotLayer));

    CHECK(gotLayer.annotations().size() == expectedPts.size());
    for (std::size_t i = 0; i < gotLayer.annotations().size() && i < expectedPts.size(); ++i) {
        const auto* poly = dynamic_cast<const geometry::PolygonShape*>(gotLayer.annotations()[i].shape.get());
        CHECK(poly != nullptr);
        if (!poly) continue;
        CHECK(poly->points().size() == expectedPts[i].size());
        const std::size_t n = std::min(poly->points().size(), expectedPts[i].size());
        for (std::size_t k = 0; k < n; ++k) {
            CHECK(std::fabs(poly->points()[k].x - expectedPts[i][k].x) < 1e-6);
            CHECK(std::fabs(poly->points()[k].y - expectedPts[i][k].y) < 1e-6);
        }
        CHECK(std::fabs(poly->cornerRadius() - expectedRadius[i]) < 1e-9);
    }

    // 圆 / 椭圆锚点往返：buildShape(锚点)→shapeToJson(反解)→shapeFromJson(buildShape)，比较包围盒中心与半径。
    //   圆：p1=圆心、p2=圆上点（半径=|p1p2|）；椭圆：p1=圆心、半轴=(|Δx|,|Δy|)。往返仅包围盒稳定（p2 方向会被规范化）。
    struct EllipseCase { geometry::ShapeType type; core::Point2D p1; core::Point2D p2; };
    const std::vector<EllipseCase> ellCases = {
        {geometry::ShapeType::Circle,  {30, 40}, {55, 40}},   // 圆心 + 水平圆上点（半径 25）
        {geometry::ShapeType::Circle,  {10, 10}, {10, 22}},   // 圆上点非水平（验证距离语义）
        {geometry::ShapeType::Ellipse, {50, 60}, {80, 35}},   // 半轴 (30,25)
    };
    struct EllipseExpect { double cx, cy, rx, ry; };
    std::vector<EllipseExpect> ellExpect;
    std::vector<annotation::Annotation> ellItems;
    for (const auto&[type, p1, p2] : ellCases) {
        annotation::ShapeRequest req;
        req.type = type;
        req.p1 = p1;
        req.p2 = p2;
        annotation::Annotation a;
        a.shape = annotation::buildShape(req);
        const auto* ell = dynamic_cast<const geometry::EllipseShape*>(a.shape.get());
        CHECK(ell != nullptr);
        if (ell) ellExpect.push_back({ell->centerX(), ell->centerY(), ell->radiusX(), ell->radiusY()});
        ellItems.push_back(std::move(a));
    }
    annotation::AnnotationSet ellLayer;
    ellLayer.restore(std::move(ellItems));
    CHECK(engine::saveEngineConfig(file.string(), cfg, &ellLayer));
    engine::EngineConfig got2;
    annotation::AnnotationSet gotEllLayer;
    CHECK(engine::loadEngineConfig(file.string(), got2, &gotEllLayer));
    CHECK(gotEllLayer.annotations().size() == ellExpect.size());
    for (std::size_t i = 0; i < gotEllLayer.annotations().size() && i < ellExpect.size(); ++i) {
        const auto* ell = dynamic_cast<const geometry::EllipseShape*>(gotEllLayer.annotations()[i].shape.get());
        CHECK(ell != nullptr);
        if (!ell) continue;
        CHECK(std::fabs(ell->centerX() - ellExpect[i].cx) < 1e-6);
        CHECK(std::fabs(ell->centerY() - ellExpect[i].cy) < 1e-6);
        CHECK(std::fabs(ell->radiusX() - ellExpect[i].rx) < 1e-6);
        CHECK(std::fabs(ell->radiusY() - ellExpect[i].ry) < 1e-6);
    }

    fs::remove_all(dir, ec);
    std::cout << "[anno-anchor-roundtrip] OK\n";
}

// §7 配置严格 schema 校验（读侧守卫）：present-but-invalid（枚举拼错 / 颜色非法 / 类型不符 / 容器错型）一律拒绝，
//   且携定位报因（CoreError.message 非空、code=ConfigParseFailed）；缺失→默认、越界→clamp、合法别名与显式默认 token 仍放行；
//   文件级语法错误 message 留空（两层契约）。
void testEngineConfigStrict() {
    using namespace idc;
    namespace fs = std::filesystem;

    const fs::path dir = fs::temp_directory_path() / "idc_config_strict_test";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir, ec);

    // 写一段 JSON 到独立文件并加载，回传 expected（供逐例断言成功/失败与 message）；cfg 经出参回填。
    auto loadText = [&](const char* name, const std::string& text, engine::EngineConfig& cfg) {
        const fs::path f = dir / name;
        { std::ofstream ofs(f); ofs << text; }
        return engine::loadEngineConfig(f.string(), cfg);
    };

    // ① 缺失 → 默认（向后兼容不变）：仅 source，其余全缺，仍成功。
    {
        engine::EngineConfig cfg;
        CHECK(loadText("min.json", R"J({"source":{"width":8,"height":8}})J", cfg).has_value());
    }

    // ② 越界 → clamp（不拦）：opacity:300 成功且夹为 255。
    {
        engine::EngineConfig cfg;
        const auto r = loadText("clamp.json", R"J({"source":{"width":8,"height":8},"opacity":300})J", cfg);
        CHECK(r.has_value());
        CHECK(cfg.canvasOpacity == 255);
    }

    // ③ 合法别名（jpg）与显式默认 token（erase/rect/remove/merged）一律放行。
    {
        engine::EngineConfig cfg;
        CHECK(loadText("alias.json",
            R"J({"cut":{"tier":"erase","generator":"rect","polarity":"remove"},"export":{"format":"jpg","mode":"merged"}})J", cfg).has_value());
    }

    // ④ 枚举拼错（mode）→ 拒绝 + 定位报因非空 + code=ConfigParseFailed。
    {
        engine::EngineConfig cfg;
        const auto r = loadText("badmode.json", R"J({"export":{"mode":"sepreate"}})J", cfg);
        CHECK(!r.has_value());
        if (!r) {
            CHECK(r.error().code == core::CoreError::Code::ConfigParseFailed);
            CHECK(!r.error().message.empty());
        }
    }
    // ⑤ tier 拼错 → 拒绝。
    {
        engine::EngineConfig cfg;
        CHECK(!loadText("badtier.json", R"J({"cut":{"tier":"grdi"}})J", cfg).has_value());
    }
    // ⑥ 颜色非法 → 拒绝（present 但 hex 非法）。
    {
        engine::EngineConfig cfg;
        CHECK(!loadText("badcolor.json", R"J({"export":{"merge":{"padColor":"#ZZZZZZZZ"}}})J", cfg).has_value());
    }
    // ⑦ 枚举字段给了数字 → 拒绝（类型不符）。
    {
        engine::EngineConfig cfg;
        CHECK(!loadText("badenumtype.json", R"J({"cut":{"tier":5}})J", cfg).has_value());
    }
    // ⑧ 数值字段给了字符串 → 拒绝。
    {
        engine::EngineConfig cfg;
        CHECK(!loadText("badnum.json", R"J({"source":{"width":"big","height":8}})J", cfg).has_value());
    }
    // ⑨ 该为对象却给数组 → 拒绝。
    {
        engine::EngineConfig cfg;
        CHECK(!loadText("badobj.json", R"J({"cut":[]})J", cfg).has_value());
    }
    // ⑩ 非本段键（layers[0].source.recipe）：不识别、按未知键静默忽略，层退化为空占位（无元素）。
    {
        engine::EngineConfig cfg;
        CHECK(loadText("badrecipe.json", R"J({"layers":[{"source":{"recipe":{"kind":"chekboard"}}}]})J", cfg).has_value());
        CHECK(cfg.layers.has_value() && !cfg.layers->empty());
        if (cfg.layers.has_value() && !cfg.layers->empty()) {
            CHECK(!(*cfg.layers)[0].backdrop.has_value());
            CHECK(!canvas::hasImageElement((*cfg.layers)[0]));
        }
    }
    // ⑪ 预处理 op 拼错 → 拒绝。
    {
        engine::EngineConfig cfg;
        CHECK(!loadText("badop.json", R"J({"preprocess":[{"op":"grayy"}]})J", cfg).has_value());
    }
    // ⑫ 合法嵌套（grid-lines 图案元素 + 底图色彩步）仍放行。
    {
        engine::EngineConfig cfg;
        CHECK(loadText("goodnest.json",
            R"J({"generate":{"backdrop":{"kind":"grid-lines","period":8},"preprocess":[{"op":"gray"}]}})J", cfg).has_value());
    }

    // ⑫b generate.image 显式写 file / box* → 不是本段合法键，拒载报因（防静默丢字段）。
    {
        engine::EngineConfig cfg;
        const auto rf = loadText("badgenimgfile.json",
            R"J({"source":{"width":8,"height":8},"generate":{"image":{"fit":"crop","file":"a.png"}}})J", cfg);
        CHECK(!rf.has_value());
        const auto rb = loadText("badgenimgbox.json",
            R"J({"source":{"width":8,"height":8},"generate":{"image":{"fit":"crop","boxWidth":4}}})J", cfg);
        CHECK(!rb.has_value());
    }

    // ⑬ 网格参数给小数 → 拒绝（坐标键另接受百分比字符串，但仍不收裸小数）。
    {
        engine::EngineConfig cfg;
        CHECK(!loadText("fracgrid.json",
            R"J({"cut":{"tier":"grid","generator":"grid","params":{"originX":10.5,"originY":0,"cellWidth":100,"cellHeight":100}}})J", cfg).has_value());
    }
    // ⑭ 切割矩形给小数 → 拒绝（同上：比例须写成 "50%" 字符串，不能是裸小数）。
    {
        engine::EngineConfig cfg;
        CHECK(!loadText("fracrect.json",
            R"J({"cut":{"tier":"extract","generator":"rect","params":{"x1":1.5,"y1":0,"x2":10,"y2":10}}})J", cfg).has_value());
    }
    // ⑮ 负基准点仍放行（基准点只作相位，可为负）。
    {
        engine::EngineConfig cfg;
        CHECK(loadText("neggrid.json",
            R"J({"cut":{"tier":"grid","generator":"grid","params":{"originX":-5,"originY":-7,"cellWidth":100,"cellHeight":100}}})J", cfg).has_value());
        CHECK(cfg.cut.grid.originX == -5 && cfg.cut.grid.originY == -7);
    }

    // ⑯ source 尺寸给小数 → 拒绝（source.width/height 恒为整数）。
    {
        engine::EngineConfig cfg;
        CHECK(!loadText("fracsrc.json", R"J({"source":{"width":8.5,"height":8}})J", cfg).has_value());
    }
    // ⑰ 顶层 opacity 给小数 → 拒绝（整数；越界（如 300）仍 clamp 不拦）。
    {
        engine::EngineConfig cfg;
        CHECK(!loadText("fracopacity.json", R"J({"source":{"width":8,"height":8},"opacity":200.5})J", cfg).has_value());
    }
    // ⑱ 底图 opacity 给小数 → 拒绝（同顶层 opacity 口径）。
    {
        engine::EngineConfig cfg;
        CHECK(!loadText("fracgenopacity.json", R"J({"generate":{"kind":"solid","opacity":100.5}})J", cfg).has_value());
    }
    // ⑲ order.select 单元序号给小数 → 拒绝（序号恒为整数）。
    {
        engine::EngineConfig cfg;
        CHECK(!loadText("fracselect.json", R"J({"order":{"select":[1.5]}})J", cfg).has_value());
    }
    // ⑳ export.quality 给小数 → 拒绝。
    {
        engine::EngineConfig cfg;
        CHECK(!loadText("fracquality.json", R"J({"export":{"quality":90.5}})J", cfg).has_value());
    }
    // ㉑ export.merge 的 cols/rows/cellWidth/cellHeight 给小数 → 拒绝（行列数恒为整数；
    //    cellWidth/cellHeight 只接受整数或百分比字符串）。
    {
        engine::EngineConfig cfg;
        CHECK(!loadText("fracmerge.json", R"J({"export":{"merge":{"layout":"rearrange","cols":2.5}}})J", cfg).has_value());
    }

    // ㉒ 文件级语法错误 → 失败但 message 留空（上层自产文案）。
    {
        const fs::path f = dir / "syntax.json";
        { std::ofstream ofs(f); ofs << "{ not valid json "; }
        engine::EngineConfig cfg;
        const auto r = engine::loadEngineConfig(f.string(), cfg);
        CHECK(!r.has_value());
        if (!r) CHECK(r.error().message.empty());
    }

    fs::remove_all(dir, ec);
    std::cout << "[engine-config-strict] OK\n";
}

// 百分比几何坐标验收（SPEC §7 数值类型契约的几何例外）："N%" 依基准换算为像素（画布 / 工作图 / source）、
//   保留比例回写、非百分比串与无基准拒绝；非几何数值键（opacity / quality / 行列数）仍只收整数。
void testPercentCoords() {
    using namespace idc;
    namespace fs = std::filesystem;

    const fs::path dir = fs::temp_directory_path() / "idc_percent_coords_test";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir, ec);

    const auto readText = [](const fs::path& p) {
        std::ifstream ifs(p);
        return std::string(std::istreambuf_iterator(ifs), std::istreambuf_iterator<char>());
    };
    // 写一段 JSON 到独立文件并加载（可选收集百分比记录）。
    auto loadText = [&](const char* name, const std::string& text, engine::EngineConfig& cfg,
                        annotation::AnnotationSet* set = nullptr, core::PercentHints* hints = nullptr) {
        const fs::path f = dir / name;
        { std::ofstream ofs(f); ofs << text; }
        return engine::loadEngineConfig(f.string(), cfg, set, nullptr, hints);
    };

    // ① 各白名单几何键按各自基准换算：source 200×100 → 位置/尺寸按画布宽高；resize 目标尺寸以 source 为基准。
    {
        engine::EngineConfig cfg;
        core::PercentHints hints;
        CHECK(loadText("basic.json", R"J({
  "source": {"width": 200, "height": 100},
  "preprocess": [{"op": "resize", "width": "50%", "height": "50%"}],
  "cut": {"tier": "extract", "generator": "rect", "params": {"x1": "10%", "y1": "20%", "x2": "50%", "y2": "80%"}},
  "export": {"merge": {"cellWidth": "50%", "cellHeight": "25%"}},
  "generate": {"backdrop": {"kind": "solid"}, "image": {"fit": "crop", "x": "10%", "y": "50%"}},
  "layers": [{"transform": {"x": "50%", "y": "50%", "width": "25%", "height": "25%"}}]
})J", cfg, nullptr, &hints).has_value());
        CHECK(cfg.cut.rect.left == 20 && cfg.cut.rect.top == 20);
        CHECK(cfg.cut.rect.right == 100 && cfg.cut.rect.bottom == 80);
        CHECK(cfg.exportParams.cellWidth.has_value() && *cfg.exportParams.cellWidth == 100);
        CHECK(cfg.exportParams.cellHeight.has_value() && *cfg.exportParams.cellHeight == 25);
        CHECK(cfg.base.has_value() && cfg.base->imageX == 20 && cfg.base->imageY == 50);
        CHECK(cfg.layers.has_value() && cfg.layers->size() == 1);
        if (cfg.layers.has_value() && cfg.layers->size() == 1) {
            const auto& t = (*cfg.layers)[0].transform;
            CHECK(t.x == 100 && t.y == 50 && t.width == 50 && t.height == 25);
        }
        CHECK(cfg.preprocess.size() == 1);
        if (cfg.preprocess.size() == 1) {
            CHECK(std::get<preprocess::ResizeOp>(cfg.preprocess.items()[0]).width == 100);
            CHECK(std::get<preprocess::ResizeOp>(cfg.preprocess.items()[0]).height == 50);
        }
        // 每处换算记一条记录，路径与落盘结构一致（保存时据此回写）。
        CHECK(hints.size() == 14);
        bool sawRect = false, sawLayer = false, sawResize = false;
        for (const auto&[pointer, ratio, resolved] : hints) {
            if (pointer == "/cut/params/x1") { sawRect = true; CHECK(nearEq(ratio, 0.1) && nearEq(resolved, 20.0)); }
            if (pointer == "/layers/0/transform/width") sawLayer = true;
            if (pointer == "/preprocess/0/width") sawResize = true;
        }
        CHECK(sawRect && sawLayer && sawResize);
    }

    // ② 换算四舍五入（与交互坐标同口径）：12.5% × 100 → 13。
    {
        engine::EngineConfig cfg;
        CHECK(loadText("round.json", R"J({"source":{"width":100,"height":100},"cut":{"generator":"rect","params":{"x2":"12.5%"}}})J", cfg).has_value());
        CHECK(cfg.cut.rect.right == 13);
    }

    // ③ 负比例可写（网格基准点只作相位）："-10%" × 100 → -10。
    {
        engine::EngineConfig cfg;
        CHECK(loadText("negorigin.json", R"J({"source":{"width":100,"height":100},"cut":{"tier":"grid","generator":"grid","params":{"originX":"-10%","originY":"0%","cellWidth":"50%","cellHeight":"50%"}}})J", cfg).has_value());
        CHECK(cfg.cut.grid.originX == -10 && cfg.cut.grid.originY == 0);
        CHECK(cfg.cut.grid.cellWidth == 50 && cfg.cut.grid.cellHeight == 50);
    }

    // ④ 保存回写：未编辑的比例原样保留为 "N%"，编辑过的字段写像素。
    {
        engine::EngineConfig cfg;
        core::PercentHints hints;
        CHECK(loadText("roundtrip.json", R"J({"source":{"width":200,"height":100},"cut":{"generator":"rect","params":{"x1":"10%","y1":"20%","x2":"50%","y2":"80%"}}})J", cfg, nullptr, &hints).has_value());
        cfg.cut.rect.left = 30;   // 只改一处
        const fs::path out = dir / "roundtrip_out.json";
        CHECK(engine::saveEngineConfig(out.string(), cfg, nullptr, &hints));
        const std::string text = readText(out);
        CHECK(text.find("\"x1\": 30") != std::string::npos);
        CHECK(text.find("\"y1\": \"20%\"") != std::string::npos);
        CHECK(text.find("\"x2\": \"50%\"") != std::string::npos);
        CHECK(text.find("\"y2\": \"80%\"") != std::string::npos);
    }

    // ⑤ 标注几何的比例：基准＝工作图尺寸（rotate 90 交换宽高，resize 覆盖后再换算）。
    {
        engine::EngineConfig cfg;
        annotation::AnnotationSet set;
        CHECK(loadText("anno.json", R"J({
  "source": {"width": 200, "height": 100},
  "preprocess": [{"op": "rotate", "deg": 90}, {"op": "resize", "width": "50%", "height": "50%"}],
  "annotations": [
    {"type": "rectangle", "x1": "0%", "y1": "0%", "x2": "100%", "y2": "100%"},
    {"type": "line", "points": [["0%", "0%"], ["100%", "100%"]]},
    {"type": "text", "x": "50%", "y": "50%", "text": "hi", "fontSize": 10}
  ]
})J", cfg, &set).has_value());
        CHECK(set.annotations().size() == 3);
        if (set.annotations().size() == 3) {
            const auto* poly = dynamic_cast<const geometry::PolygonShape*>(set.annotations()[0].shape.get());
            CHECK(poly != nullptr);
            if (poly) {
                double minx = 1e18, miny = 1e18, maxx = -1e18, maxy = -1e18;
                for (const core::Point2D& p : poly->points()) {
                    minx = std::min(minx, p.x); miny = std::min(miny, p.y);
                    maxx = std::max(maxx, p.x); maxy = std::max(maxy, p.y);
                }
                CHECK(nearEq(minx, 0.0) && nearEq(miny, 0.0) && nearEq(maxx, 100.0) && nearEq(maxy, 50.0));
            }
            const auto* line = dynamic_cast<const geometry::PolygonShape*>(set.annotations()[1].shape.get());
            CHECK(line != nullptr && line->points().size() == 2);
            if (line && line->points().size() == 2) {
                CHECK(nearEq(line->points()[1].x, 100.0) && nearEq(line->points()[1].y, 50.0));
            }
            const auto* text = dynamic_cast<const geometry::TextShape*>(set.annotations()[2].shape.get());
            CHECK(text != nullptr);
            if (text) CHECK(nearEq(text->x(), 50.0) && nearEq(text->y(), 25.0));
        }
    }

    // ⑥ 标注比例同样原样回写（数组序与 zOrder 一致时路径稳定）。
    {
        engine::EngineConfig cfg;
        annotation::AnnotationSet set;
        core::PercentHints hints;
        CHECK(loadText("anno_rt.json", R"J({"source":{"width":100,"height":100},"annotations":[{"type":"rectangle","x1":"0%","y1":"0%","x2":"50%","y2":"50%"}]})J", cfg, &set, &hints).has_value());
        const fs::path out = dir / "anno_rt_out.json";
        CHECK(engine::saveEngineConfig(out.string(), cfg, &set, &hints));
        const std::string text = readText(out);
        CHECK(text.find("\"x1\": \"0%\"") != std::string::npos);
        CHECK(text.find("\"x2\": \"50%\"") != std::string::npos);
    }

    // ⑦ 拒绝：非百分比串、百分比无基准、坐标键给小数、非几何数值键给百分比。
    {
        engine::EngineConfig cfg;
        const auto r = loadText("badpct.json", R"J({"source":{"width":100,"height":100},"cut":{"generator":"rect","params":{"x1":"wide%"}}})J", cfg);
        CHECK(!r.has_value());
        if (!r) CHECK(!r.error().message.empty());
    }
    {
        engine::EngineConfig cfg;
        CHECK(!loadText("nobasis.json", R"J({"cut":{"generator":"rect","params":{"x1":"10%"}}})J", cfg).has_value());
    }
    {
        engine::EngineConfig cfg;
        CHECK(!loadText("fraccoord.json", R"J({"source":{"width":100,"height":100},"cut":{"generator":"rect","params":{"x1":1.5}}})J", cfg).has_value());
    }
    {
        engine::EngineConfig cfg;
        CHECK(!loadText("pctopacity.json", R"J({"source":{"width":100,"height":100},"opacity":"50%"})J", cfg).has_value());
    }

    fs::remove_all(dir, ec);
    std::cout << "[percent-coords] OK\n";
}
