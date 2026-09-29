// ============================================================================
// 文件：tests/test_engine_config_json.cpp
// 作用：配置文件存取验收（SPEC §7）——EngineConfig ↔ JSON 往返、preprocess 队列（含 resize）、
//       可选 annotations 段的带样式标注往返（几何/样式/变换整体快照），以及向后兼容行为
//       （nullptr 层忽略标注段、无段载入清空层）。
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

#include "annotation/annotation_layer.h"
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

// 构造覆盖四类具体形状 + 变换/样式的标注层。
idc::annotation::AnnotationLayer makeLayer() {
    using namespace idc;
    annotation::AnnotationLayer layer;
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
        a.shapeType = geometry::ShapeType::RECTANGLE;
        a.color = core::Color(255, 140, 0, 255);
        a.fillColor = core::Color(0, 0, 0, 64);
        a.strokeWidth = 3;
        a.fillType = true;
        a.shape = std::move(poly);
        items.push_back(std::move(a));
    }
    // ② 椭圆（EllipseShape）：恒等变换（transform 段应省略）。
    {
        annotation::Annotation a;
        a.shapeType = geometry::ShapeType::ELLIPSE;
        a.color = core::kRed;
        a.fillColor = core::kRed;
        a.strokeWidth = 2;
        a.shape = std::make_unique<geometry::EllipseShape>(100, 50, 80, 40, geometry::ShapeType::ELLIPSE);
        items.push_back(std::move(a));
    }
    // ③ 文字（TextShape）：位置 + 内容 + 字号。
    {
        annotation::Annotation a;
        a.shapeType = geometry::ShapeType::TEXT;
        a.color = core::kWhite;
        a.strokeWidth = 1;
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
        a.shapeType = geometry::ShapeType::PATH;
        a.color = core::kBlue;
        a.strokeWidth = 4;
        a.shape = std::make_unique<geometry::PathShape>(std::move(p), geometry::ShapeType::PATH);
        items.push_back(std::move(a));
    }
    layer.restoreAnnotations(std::move(items));
    return layer;
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

    annotation::AnnotationLayer layer = makeLayer();

    // ---- 往返：带标注保存 → 全新对象加载。----
    CHECK(engine::saveEngineConfig(file.string(), cfg, &layer));
    engine::EngineConfig got;
    annotation::AnnotationLayer gotLayer;
    CHECK(engine::loadEngineConfig(file.string(), got, &gotLayer));

    // 配置字段一致。
    CHECK(got.source.width == 800 && got.source.height == 600);
    CHECK(got.preprocess.size() == 2);
    CHECK(std::holds_alternative<preprocess::ResizeOp>(got.preprocess.steps()[0]));
    CHECK(std::get<preprocess::ResizeOp>(got.preprocess.steps()[0]).width == 400);
    CHECK(std::get<preprocess::RotateOp>(got.preprocess.steps()[1]).angle == 90);
    CHECK(got.cut.tier == engine::Tier::L3 && got.cut.generator == engine::CutGenerator::GRID);
    CHECK(got.cut.grid.cellWidth == 100 && got.cut.grid.remainder == engine::RemainderPolicy::PAD);
    CHECK(got.selectedCells.size() == 2 && got.selectedCells[1] == 4);
    CHECK(got.emitParams.layout == engine::MergeLayout::REARRANGE && got.emitParams.cols == 3);

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
        CHECK(a.shapeType == geometry::ShapeType::RECTANGLE);
        CHECK(a.color == core::Color(255, 140, 0, 255) && a.fillColor == core::Color(0, 0, 0, 64));
        CHECK(a.strokeWidth == 3 && a.fillType);
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
        CHECK(a.shapeType == geometry::ShapeType::ELLIPSE);
        CHECK(a.color == core::kRed && !a.fillType);
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
        CHECK(annos[2].strokeWidth == 1);
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

    // ---- 向后兼容：不传层保存 → 文件无 annotations 段；不传层加载 → 标注段被忽略。----
    const fs::path plainFile = dir / "plain.json";
    CHECK(engine::saveEngineConfig(plainFile.string(), cfg));
    {
        std::ifstream ifs(plainFile);
        const std::string text((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
        CHECK(text.find("\"annotations\"") == std::string::npos);
    }
    CHECK(engine::loadEngineConfig(plainFile.string(), got)); // 旧调用形态仍可编译（默认参数）。

    // ---- 真相源语义：文件无 annotations 段 + 传层加载 → 清空既有标注。----
    annotation::AnnotationLayer keep = makeLayer();
    CHECK(engine::loadEngineConfig(plainFile.string(), got, &keep));
    CHECK(keep.annotations().empty());

    // ---- 空层保存 → 省略 annotations 段。----
    const fs::path emptyFile = dir / "empty.json";
    annotation::AnnotationLayer emptyLayer;
    CHECK(engine::saveEngineConfig(emptyFile.string(), cfg, &emptyLayer));
    {
        std::ifstream ifs(emptyFile);
        const std::string text((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
        CHECK(text.find("\"annotations\"") == std::string::npos);
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
        CHECK(out[0].shapeType == geometry::ShapeType::RECTANGLE);
        CHECK(out[0].strokeWidth == 3 && out[0].fillType);
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
        CHECK(out[0].shapeType == geometry::ShapeType::CIRCLE);
        CHECK(out[1].shapeType == geometry::ShapeType::TEXT);
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
        CHECK(out[0].strokeWidth == 9 && out[1].strokeWidth == 1);
        // 批内排序后由调用层（restoreAnnotations）会规范化 zOrder＝下标；此处仅验证相对顺序。
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

    // 源层：逐 case 经 buildShape 生成规范顶点；先抽取期望顶点/圆角（shape 将被移入层）。
    std::vector<std::vector<core::Point2D>> expectedPts;
    std::vector<double> expectedRadius;
    std::vector<annotation::Annotation> items;
    for (const auto& c : cases) {
        annotation::ShapeRequest req;
        req.type = c.type;
        req.p1 = c.p1;
        req.p2 = c.p2;
        req.cornerRadius = 3.0;
        annotation::Annotation a;
        a.shapeType = c.type;
        a.shape = annotation::buildShape(req);
        const auto* poly = dynamic_cast<const geometry::PolygonShape*>(a.shape.get());
        CHECK(poly != nullptr);
        expectedPts.push_back(poly ? poly->points() : std::vector<core::Point2D>{});
        expectedRadius.push_back(poly ? poly->cornerRadius() : 0.0);
        items.push_back(std::move(a));
    }

    annotation::AnnotationLayer layer;
    layer.restoreAnnotations(std::move(items));

    engine::EngineConfig cfg;
    CHECK(engine::saveEngineConfig(file.string(), cfg, &layer));
    engine::EngineConfig got;
    annotation::AnnotationLayer gotLayer;
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
    for (const auto& c : ellCases) {
        annotation::ShapeRequest req;
        req.type = c.type;
        req.p1 = c.p1;
        req.p2 = c.p2;
        annotation::Annotation a;
        a.shapeType = c.type;
        a.shape = annotation::buildShape(req);
        const auto* ell = dynamic_cast<const geometry::EllipseShape*>(a.shape.get());
        CHECK(ell != nullptr);
        if (ell) ellExpect.push_back({ell->centerX(), ell->centerY(), ell->radiusX(), ell->radiusY()});
        ellItems.push_back(std::move(a));
    }
    annotation::AnnotationLayer ellLayer;
    ellLayer.restoreAnnotations(std::move(ellItems));
    CHECK(engine::saveEngineConfig(file.string(), cfg, &ellLayer));
    engine::EngineConfig got2;
    annotation::AnnotationLayer gotEllLayer;
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
