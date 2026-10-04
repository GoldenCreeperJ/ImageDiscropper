// ============================================================================
// 文件：examples/demo_main.cpp
// 作用：综合演示 ImageDiscropperCore 库的典型使用方式，覆盖以下模块：
//       - core：Image / Color 基础操作
//       - geometry + annotation：构造形状、光栅化到底图、合成
//       - preprocess：PreprocessPipeline 顺序编排与像素核（旋转/翻转经 applyOne；
//         灰度、缩放、颜色拾取核已落户 core::Image 成员）
//       - view_transform：屏幕 ↔ 逻辑坐标换算、以指定点缩放
//       - engine：真正跑通 Grid-Selection-Emit 流水线（L2 剔除坍缩 / L3 网格重排）并导出到磁盘
// 分块依据：每一步聚焦一个模块或一个典型场景，便于按需拷贝到实际项目。
// ============================================================================
#include <filesystem>
#include <iostream>
#include <optional>
#include <vector>

#include "core/color.h"
#include "core/image.h"
#include "core/point.h"
#include "engine/engine.h"
#include "engine/engine_config_json.h"
#include "annotation/annotation_set.h"
#include "annotation/shape_factory.h"
#include "annotation/view_transform.h"
#include "preprocess/preprocess_pipeline.h"

// 打印图像基本信息，便于观察每一步的结果。
static void printInfo(const char* label, const idc::core::Image& img) {
    std::cout << "[" << label << "] "
              << img.width() << " x " << img.height()
              << "  format=" << (img.isGray() ? "GRAY"
                                              : img.format() == idc::core::ImageFormat::RGBA ? "RGBA" : "RGB")
              << "\n";
}

int main() {
    using namespace idc;

    // ------------------------------------------------------------------
    // 1. 构造一张 200x120 的白底 RGBA 图像，作为标注的底图
    // ------------------------------------------------------------------
    core::Image base(200, 120, core::ImageFormat::RGBA);
    base.fill(core::kWhite);
    printInfo("base", base);

    // ------------------------------------------------------------------
    // 2. 使用 ShapeFactory 构造若干形状，组装为 Annotation 记录提交到 AnnotationSet
    //    （集合不持底图：烧录经 burnIn(base) 显式传入；编辑模式/选中/撤销重做等会话态归 GUI AnnotationEditor）
    // ------------------------------------------------------------------
    annotation::AnnotationSet annoSet;

    // 2.1 画一条红色直线：(20,20) → (100,80)
    {
        annotation::ShapeRequest req;
        req.p1 = {20, 20};
        req.p2 = {100, 80};
        req.type = geometry::ShapeType::LINE;
        annotation::Annotation a;
        a.shapeType = req.type;
        a.style.color = core::kRed;
        a.style.strokeWidth = 3;
        a.shape = annotation::buildShape(req);
        annoSet.add(std::move(a));
    }

    // 2.2 画一个蓝色填充矩形：(110,20) → (180,70)
    {
        annotation::ShapeRequest req;
        req.p1 = {110, 20};
        req.p2 = {180, 70};
        req.type = geometry::ShapeType::RECTANGLE;
        annotation::Annotation a;
        a.shapeType = req.type;
        a.style.color = core::kBlue;
        a.style.fillColor = core::kBlue; // 会话中联动模板由 editor 负责，集合侧逐条显式填写
        a.style.strokeWidth = 2;
        a.style.fill = true;
        a.shape = annotation::buildShape(req);
        annoSet.add(std::move(a));
    }

    // 2.3 画一个绿色圆形：圆心 (60, 90)，半径 20
    {
        annotation::ShapeRequest req;
        req.p1 = {60, 90};
        req.p2 = {80, 90}; // 距离 = 20，作为半径
        req.type = geometry::ShapeType::CIRCLE;
        annotation::Annotation a;
        a.shapeType = req.type;
        a.style.color = core::kGreen;
        a.style.strokeWidth = 2;
        a.shape = annotation::buildShape(req);
        annoSet.add(std::move(a));
    }

    std::cout << "shapes committed: " << annoSet.annotations().size() << "\n";

    // 2.4 合成：把底图与所有形状合并为一张最终图像（底图显式传入，集合不自持）
    core::Image composed = annoSet.burnIn(base);
    printInfo("composed", composed);

    // ------------------------------------------------------------------
    // 3. 弹尾 / 还原演示：popBack 交还顶层标注（绿色圆形），供上层撤销/重做栈编排；
    //    再把弹出项连同剩余列表 restore 回集合（对应 GUI 重做路径）。
    //    （会话态撤销已随拆分归 GUI AnnotationEditor，此处仅展示集合级弹尾/还原原语）
    // ------------------------------------------------------------------
    std::vector<annotation::Annotation> poppedItems;
    std::optional<annotation::Annotation> popped = annoSet.popBack();
    if (popped.has_value()) {
        std::cout << "popBack -> shape #" << popped->zOrder
                  << " type=" << geometry::shapeTypeName(popped->shapeType) << "\n";
    }
    std::cout << "after popBack: " << annoSet.annotations().size() << " shapes\n";
    for (const auto& a : annoSet.annotations()) poppedItems.push_back(a); // 剩余项在前
    if (popped.has_value()) poppedItems.push_back(std::move(*popped));    // 弹出项回顶
    annoSet.restore(std::move(poppedItems));                              // 整体还原（弹尾不丢）
    std::cout << "after restore: " << annoSet.annotations().size() << " shapes\n";

    // ------------------------------------------------------------------
    // 4. 命中检测演示：判断点 (60, 90) 是否落在某个形状上
    // ------------------------------------------------------------------
    if (auto idx = annoSet.hitTest({60, 90})) {
        std::cout << "hitTest(60,90) -> shape #" << *idx
                  << " type=" << geometry::shapeTypeName(annoSet.annotations()[*idx].shapeType)
                  << "\n";
    } else {
        std::cout << "hitTest(60,90) -> miss\n";
    }

    // ------------------------------------------------------------------
    // 5. 图像处理流水线演示：灰度 → 反色
    //    两个步骤都是预处理流水线的核心能力
    // ------------------------------------------------------------------
    core::Image work = composed.clone();
    printInfo("pipeline input", work);

    preprocess::PreprocessPipeline pipe;
    pipe.add(preprocess::GrayOp{});
    pipe.add(preprocess::InvertOp{});

    core::Image pipelineOut = pipe.apply(work);
    printInfo("pipeline output", pipelineOut);

    // ------------------------------------------------------------------
    // 6. 几何变换演示：旋转 90°、水平翻转、缩放 0.5x
    // ------------------------------------------------------------------
    core::Image rot90 = preprocess::applyOne(pipelineOut, preprocess::RotateOp{90});
    printInfo("rotate 90", rot90);

    core::Image flipped = preprocess::applyOne(rot90, preprocess::FlipOp{true});
    printInfo("flip H", flipped);

    core::Image half = flipped.scaled(0.5, core::ResampleMode::BILINEAR);
    printInfo("scale 0.5x", half);

    // ------------------------------------------------------------------
    // 7. 颜色选取演示：拾取 (10, 10) 处像素颜色
    // ------------------------------------------------------------------
    if (auto c = composed.colorAt(10, 10)) {
        std::cout << "pickColor(10,10) = R" << static_cast<int>(c->r)
                  << " G" << static_cast<int>(c->g)
                  << " B" << static_cast<int>(c->b)
                  << " A" << static_cast<int>(c->a) << "\n";
    }

    // ------------------------------------------------------------------
    // 8. 视图变换演示：屏幕坐标 ↔ 逻辑坐标，以指定点缩放
    // ------------------------------------------------------------------
    annotation::ViewTransform vt;
    vt.fitToViewport(composed.width(), composed.height(), 400, 300);
    std::cout << "fitToViewport -> scale=" << vt.scale()
              << " offset=(" << vt.offset().x << "," << vt.offset().y << ")\n";

    const core::Point2D screen(200, 150);
    const core::Point2D logical = vt.screenToLogical(screen);
    std::cout << "screenToLogical(200,150) -> (" << logical.x << "," << logical.y << ")\n";

    vt.scaleAboutPoint(-1.0, screen); // 放大一档
    std::cout << "after scaleAboutPoint -> scale=" << vt.scale() << "\n";

    // ------------------------------------------------------------------
    // 9. 引擎实战演示：真正跑通 Grid-Selection-Emit 流水线并导出到磁盘。
    //    9.1 L2 十字剔除 → 坍缩合并单图；9.2 同作业分离导出到文件夹；
    //    9.3 L3 网格 → 选择四角单元 → 重排合并；9.4 配置 JSON 存取（NFR-4）。
    //    输出写入 ./demo_output/。
    // ------------------------------------------------------------------
    namespace fs = std::filesystem;
    const fs::path outDir = "demo_output";
    std::error_code ec;
    fs::create_directories(outDir, ec);

    // 9.1 / 9.2 L2 反向剔除（十字）：坍缩合并单图 + 分离导出到文件夹。
    {
        engine::EngineConfig cfg;
        cfg.source = engine::SourceInfo{composed.width(), composed.height()};
        cfg.cut.tier = engine::Tier::L2;                 // 反向剔除模式
        cfg.cut.generator = engine::CutGenerator::RECT;  // 单矩形诱导十字切割线
        cfg.cut.rect = engine::RectRegion(60, 30, 140, 90);
        cfg.cut.polarity = engine::Polarity::REMOVE;     // 剔除十字 → 保留四角
        cfg.emitParams.mode = engine::EmitMode::MERGED;
        cfg.emitParams.layout = engine::MergeLayout::COLLAPSE; // 坍缩式合并（SPEC §4.2）

        const auto [ok, error, kept, composition, collapsible] = engine::runEngine(composed, cfg);
        std::cout << "[L2 collapse] ok=" << ok
                  << " collapsible=" << collapsible
                  << " kept=" << kept.size()
                  << " canvas=" << composition.canvasWidth << "x" << composition.canvasHeight
                  << "\n";
        if (ok) {
            const fs::path out = outDir / "l2_collapsed.png";
            const bool w = engine::exportImage(composition, composed, out.string());
            std::cout << "  exported -> " << out.string() << " (" << (w ? "OK" : "FAIL") << ")\n";
        } else {
            std::cout << "  error: " << error << "\n";
        }

        // 9.2 同一作业改为分离导出 → 文件夹（四角 4 张 PNG，直接写入，不压缩）。
        cfg.emitParams.mode = engine::EmitMode::SEPARATE;
        if (const engine::EngineResult rs = engine::runEngine(composed, cfg); rs.ok) {
            const fs::path folder = outDir / "l2_corners"; // 目标文件夹（不存在会自动创建）
            const bool w = engine::exportImage(rs.composition, composed, folder.string());
            std::cout << "[L2 separate] kept=" << rs.kept.size()
                      << " -> " << folder.string() << "/ (" << (w ? "OK" : "FAIL") << ")\n";
        }

        // 9.4 把该 L2 作业配置序列化为 JSON 并回读（FR-L3.8 / NFR-4）。
        if (const fs::path jf = outDir / "l2_config.json"; engine::saveEngineConfig(jf.string(), cfg)) {
            engine::EngineConfig loaded;
            const bool ok1 = engine::loadEngineConfig(jf.string(), loaded);
            std::cout << "[config json] saved -> " << jf.string() << " reload=" << ok1
                      << " polarity="
                      << (loaded.cut.polarity == engine::Polarity::REMOVE ? "remove" : "keep") << "\n";
        }
    }

    // 9.3 L3 网格分割：composed 为 200×120，单元 50×40 → 4×3 = 12 格；
    //     选择四角单元 {0,3,8,11}，按横优先重排到 2 列画布。
    {
        engine::EngineConfig cfg;
        cfg.source = engine::SourceInfo{composed.width(), composed.height()};
        cfg.cut.tier = engine::Tier::L3;
        cfg.cut.generator = engine::CutGenerator::GRID;
        cfg.cut.grid = engine::GridParams{0, 0, 50, 40, engine::RemainderPolicy::DISCARD};
        cfg.cut.polarity = engine::Polarity::KEEP;
        cfg.selectedCells = {0, 3, 8, 11};               // 4×3 网格的四角单元
        cfg.order.strategy = engine::SortStrategy::ROW_MAJOR;
        cfg.emitParams.mode = engine::EmitMode::MERGED;
        cfg.emitParams.layout = engine::MergeLayout::REARRANGE; // 重排式合并（SPEC §4.3）
        cfg.emitParams.cols = 2;

        const engine::EngineResult r = engine::runEngine(composed, cfg);
        std::cout << "[L3 rearrange] ok=" << r.ok
                  << " kept=" << r.kept.size()
                  << " canvas=" << r.composition.canvasWidth << "x" << r.composition.canvasHeight
                  << "\n";
        if (r.ok) {
            const fs::path out = outDir / "l3_rearranged.png";
            const bool w = engine::exportImage(r.composition, composed, out.string());
            std::cout << "  exported -> " << out.string() << " (" << (w ? "OK" : "FAIL") << ")\n";
        } else {
            std::cout << "  error: " << r.error << "\n";
        }
    }

    std::cout << "demo finished.\n";
    return 0;
}
