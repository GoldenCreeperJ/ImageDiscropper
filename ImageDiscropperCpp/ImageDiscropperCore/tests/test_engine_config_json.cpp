
// ============================================================================
// 文件：tests/test_engine_config_json.cpp
// 作用：配置文件存取验收（SPEC §7）——EngineConfig ↔ JSON 往返、preprocess 队列（含 resize）、
//       可选 annotations 段的带样式标注往返（几何/样式/变换整体快照），以及向后兼容行为
//       （不传集合指针时忽略标注段、无段载入清空集合）。
// 分块依据：本文件只测「序列化语义」，不触碰引擎计算与导出落盘（那些见 test_engine_pipeline /
//       test_engine_export）；写入系统临时目录并在结束时清理。
// ============================================================================
#include "test_harness.h"

#include <cmath>
#include <filesystem>
#include <fstream>
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
            geometry::ShapeType::RECTANGLE, true, 8.0);
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
        a.shape = std::make_unique<geometry::EllipseShape>(100, 50, 80, 40, geometry::ShapeType::ELLIPSE);
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
        a.shape = std::make_unique<geometry::PathShape>(std::move(p), geometry::ShapeType::PATH);
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

    // ---- 基座配置：source + 声明式队列（resize→rotate）+ cut/emit 非默认值。----
    engine::EngineConfig cfg;
    cfg.source = engine::SourceInfo{800, 600};
    cfg.preprocess.add(preprocess::ResizeOp{400, 300});
    cfg.preprocess.add(preprocess::RotateOp{90});
    cfg.cut.tier = engine::Tier::L3;
    cfg.cut.generator = engine::CutGenerator::GRID;
    cfg.cut.grid = engine::GridParams{0, 0, 100, 100, engine::RemainderPolicy::PAD};
    cfg.cut.polarity = engine::Polarity::KEEP;
    cfg.selectedCells = {0, 4};
    cfg.emitParams.mode = engine::EmitMode::MERGED;
    cfg.emitParams.layout = engine::MergeLayout::REARRANGE;
    cfg.emitParams.cols = 3;
    cfg.emitParams.padColor = core::Color(0, 0, 0, 0);

    // 多层画布 layers 段：一配方层（带逐层 preprocess + transform）+ 一文件层（仅相对名/落位）。
    {
        std::vector<canvas::ImageLayer> st;
        canvas::ImageLayer l0;
        l0.id = 7;
        l0.sourceKind = canvas::LayerSourceKind::Recipe;
        generate::PatternParams pp;
        pp.kind = generate::PatternKind::Checkerboard;
        pp.period = 4;
        pp.background = core::kWhite;
        pp.foreground = core::kBlack;
        l0.recipe = pp;
        l0.transform.width = 8; l0.transform.height = 8;
        l0.transform.inCanvasAnchor = generate::ImageAnchor::TopLeft;
        l0.transform.opacity = 180;   // 层不透明度（合成期 alpha 乘子）：验证往返。
        l0.zOrder = 0;
        l0.preprocess.add(preprocess::GrayOp{});
        canvas::ImageLayer l1;
        l1.id = 9;
        l1.sourceKind = canvas::LayerSourceKind::File;
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
    CHECK(got.cut.tier == engine::Tier::L3 && got.cut.generator == engine::CutGenerator::GRID);
    CHECK(got.cut.grid.cellWidth == 100 && got.cut.grid.remainder == engine::RemainderPolicy::PAD);
    CHECK(got.selectedCells.size() == 2 && got.selectedCells[1] == 4);
    CHECK(got.emitParams.layout == engine::MergeLayout::REARRANGE && got.emitParams.cols == 3);

    // layers 段往返：配方层（含逐层 preprocess + transform）/ 文件层（仅相对名，像素不入配置）。
    CHECK(got.layers.has_value());
    if (got.layers.has_value()) {
        const auto& layers = *got.layers;
        CHECK(layers.size() == 2);
        if (layers.size() == 2) {
            const canvas::ImageLayer& r0 = layers[0];
            CHECK(r0.sourceKind == canvas::LayerSourceKind::Recipe);
            CHECK(r0.recipe.has_value() && r0.recipe->kind == generate::PatternKind::Checkerboard && r0.recipe->period == 4);
            CHECK(r0.transform.width == 8 && r0.transform.height == 8);
            CHECK(r0.transform.inCanvasAnchor == generate::ImageAnchor::TopLeft);
            CHECK(r0.transform.opacity == 180);   // 不透明度往返一致
            CHECK(r0.zOrder == 0 && r0.preprocess.size() == 1);
            CHECK(std::holds_alternative<preprocess::GrayOp>(r0.preprocess.items()[0]));
            const canvas::ImageLayer& r1 = layers[1];
            CHECK(r1.sourceKind == canvas::LayerSourceKind::File);
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
        CHECK(a.shape->type() == geometry::ShapeType::RECTANGLE);
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
        CHECK(a.shape->type() == geometry::ShapeType::ELLIPSE);
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
    CHECK(engine::loadEngineConfig(plainFile.string(), got)); // 旧调用形态仍可编译（默认参数）。

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

    // ---- 旧配置迁移：图层 preprocess 内 "opacity" 步 → transform.opacity（mul!=0 取 mul+add，mul==0 取 add）；
    //      顶层画布级 "opacity" 步无对应 → 丢弃（preprocessFromJson 未知 op 返回 false 被跳过）。----
    {
        const fs::path legacyFile = dir / "legacy.json";
        std::ofstream ofs(legacyFile);
        ofs << R"J({
  "source": {"width": 10, "height": 10},
  "preprocess": [ {"op": "opacity", "mul": 200, "add": 0} ],
  "layers": [
    { "source": {"kind": "empty"}, "transform": {}, "zOrder": 0,
      "preprocess": [ {"op": "gray"}, {"op": "opacity", "mul": 128, "add": 10} ] },
    { "source": {"kind": "empty"}, "transform": {"opacity": 255}, "zOrder": 1,
      "preprocess": [ {"op": "opacity", "mul": 0, "add": 90} ] }
  ]
})J";
        ofs.close();
        engine::EngineConfig legacy;
        CHECK(engine::loadEngineConfig(legacyFile.string(), legacy));
        CHECK(legacy.preprocess.size() == 0);   // 顶层 opacity 步丢弃
        if (legacy.layers.has_value() && legacy.layers->size() == 2) {
            const auto& L0 = (*legacy.layers)[0];
            CHECK(L0.preprocess.size() == 1);   // 仅 gray 保留，opacity 迁移出去
            CHECK(std::holds_alternative<preprocess::GrayOp>(L0.preprocess.items()[0]));
            CHECK(L0.transform.opacity == 138);  // mul=128→mul+add=138
            const auto& L1 = (*legacy.layers)[1];
            CHECK(L1.preprocess.size() == 0);
            CHECK(L1.transform.opacity == 90);   // mul=0→取 add=90
        } else {
            CHECK(false);
        }
    }

    // ---- 底图 generate 段（同构 ImageLayer）往返：纯图案底（与旧 generate 逐字段等价）+ 落位声明底（不存文件路径）+ 旧配置兼容。----
    {
        // ① 纯图案 Recipe 底：序列化后 generate 段仅图案键（无 file/落位），解析回 recipe。
        const fs::path patFile = dir / "base_pattern.json";
        engine::EngineConfig pc;
        pc.source = engine::SourceInfo{64, 48};
        canvas::ImageLayer pb;
        pb.sourceKind = canvas::LayerSourceKind::Recipe;
        generate::PatternParams ppp;
        ppp.kind = generate::PatternKind::Stripes;
        ppp.period = 6;
        pb.recipe = ppp;
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
            CHECK(pg.base->sourceKind == canvas::LayerSourceKind::Recipe);
            CHECK(pg.base->recipe.has_value() && pg.base->recipe->kind == generate::PatternKind::Stripes && pg.base->recipe->period == 6);
            CHECK(!pg.base->hasImagePlacement);
            CHECK(pg.base->fileName.empty());
            CHECK(pg.base->transform.opacity == 255);   // 底图整体不透明度：默认 255 往返（无显式赋值时）
        }

        // ② 落位声明底（携内层落位 + backdrop）：仅落位键往返；文件路径与位图均不入配置（运行时由 --input 供位图）。
        const fs::path fileBase = dir / "base_file.json";
        engine::EngineConfig fc;
        fc.source = engine::SourceInfo{200, 100};
        canvas::ImageLayer fb;
        fb.sourceKind = canvas::LayerSourceKind::File;
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
        fb.boxBackdrop = back;
        fb.decodedPixels = core::Image(2, 2, core::ImageFormat::RGBA);   // 位图不入配置：往返后应为空。
        fb.transform.opacity = 64;   // 底图整体不透明度（非默认）：验证随 generate 段 opacity 键往返。
        fc.base = fb;
        CHECK(engine::saveEngineConfig(fileBase.string(), fc));
        engine::EngineConfig fg;
        CHECK(engine::loadEngineConfig(fileBase.string(), fg));
        CHECK(fg.base.has_value());
        if (fg.base.has_value()) {
            const canvas::ImageLayer& b = *fg.base;
            CHECK(b.sourceKind == canvas::LayerSourceKind::Empty);   // 无配方→不回退为 File（底图仅存落位声明）
            CHECK(b.fileName.empty());                               // 文件路径不入配置
            CHECK(b.decodedPixels.empty());   // 位图不经配置还原
            CHECK(b.hasImagePlacement);
            CHECK(b.imageFit == canvas::ImageFit::Stretch);
            CHECK(b.boxWidth == 200 && b.boxHeight == 100);
            CHECK(b.imageAnchor == generate::ImageAnchor::TopLeft);
            CHECK(b.imagePivot == generate::ImageAnchor::BottomRight);
            CHECK(b.imageX == 7 && b.imageY == -3);
            CHECK(b.boxBackdrop.has_value() && b.boxBackdrop->background == core::Color(10, 20, 30, 255));
            CHECK(b.transform.opacity == 64);   // 底图整体不透明度往返一致
        }

        // ③ 旧配置向后兼容：仅图案键的 generate 段 → Recipe 底（等价旧行为）。
        const fs::path legacyGen = dir / "legacy_gen.json";
        {
            std::ofstream ofs(legacyGen);
            ofs << R"J({"source":{"width":16,"height":16},"generate":{"kind":"grid-lines","period":8}})J";
        }
        engine::EngineConfig lg;
        CHECK(engine::loadEngineConfig(legacyGen.string(), lg));
        CHECK(lg.base.has_value() && lg.base->sourceKind == canvas::LayerSourceKind::Recipe);
        if (lg.base.has_value()) {
            CHECK(lg.base->recipe.has_value() && lg.base->recipe->kind == generate::PatternKind::GridLines && lg.base->recipe->period == 8);
            CHECK(!lg.base->hasImagePlacement);
            CHECK(lg.base->transform.opacity == 255);   // 旧配置无 opacity 键 → 缺省 255（行为不变）
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

        // 旧配置无顶层 opacity → 缺省 255（向后兼容、行为不变）；旧 generate 段无 preprocess → 底图预处理为空。
        const fs::path legacyNoOp = dir / "legacy_no_opacity.json";
        {
            std::ofstream ofs(legacyNoOp);
            ofs << R"J({"source":{"width":8,"height":8},"generate":{"kind":"solid"}})J";
        }
        engine::EngineConfig noOp;
        CHECK(engine::loadEngineConfig(legacyNoOp.string(), noOp));
        CHECK(noOp.canvasOpacity == 255);
        CHECK(noOp.base.has_value() && noOp.base->preprocess.size() == 0);
    }

    // ---- 底图 generate 段 preprocess（色彩步）往返：仅色彩步保留、几何/resize 步写出时被过滤（底图预处理不改画布尺寸）。----
    {
        const fs::path basePreFile = dir / "base_preprocess.json";
        engine::EngineConfig bp;
        bp.source = engine::SourceInfo{40, 40};
        canvas::ImageLayer pb;
        pb.sourceKind = canvas::LayerSourceKind::Recipe;
        generate::PatternParams ppp;
        ppp.kind = generate::PatternKind::Solid;
        pb.recipe = ppp;
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
        CHECK(out[0].shape->type() == geometry::ShapeType::RECTANGLE);
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
        CHECK(out[0].shape->type() == geometry::ShapeType::CIRCLE);
        CHECK(out[1].shape->type() == geometry::ShapeType::TEXT);
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

    std::cout << "[anno-parse-text] OK\n";
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
        {geometry::ShapeType::RECTANGLE,         {10, 20},  {60, 70}},
        {geometry::ShapeType::SQUARE,            {0, 0},    {-10, 6}},    // 负象限生长
        {geometry::ShapeType::RHOMBUS,           {50, 50},  {80, 30}},
        {geometry::ShapeType::ISOS_TRIANGLE,     {100, 100},{130, 60}},   // 顶角朝上
        {geometry::ShapeType::ISOS_TRIANGLE,     {40, 40},  {70, 90}},    // 顶角朝下
        {geometry::ShapeType::EQU_TRIANGLE,      {50, 50},  {80, 90}},
        {geometry::ShapeType::RECT_TRIANGLE,     {10, 20},  {50, 40}},
        {geometry::ShapeType::EQU_RECT_TRIANGLE, {0, 0},    {-6, 10}},
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
        {geometry::ShapeType::CIRCLE,  {30, 40}, {55, 40}},   // 圆心 + 水平圆上点（半径 25）
        {geometry::ShapeType::CIRCLE,  {10, 10}, {10, 22}},   // 圆上点非水平（验证距离语义）
        {geometry::ShapeType::ELLIPSE, {50, 60}, {80, 35}},   // 半轴 (30,25)
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
