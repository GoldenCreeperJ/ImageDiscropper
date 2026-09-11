// ============================================================================
// 文件：examples/demo_main.cpp
// 作用：综合演示 ImageDiscropperCore 库的典型使用方式，覆盖以下模块：
//       - core：Image / Color 基础操作
//       - geometry + annotation：构造形状、光栅化到底图、合成
//       - processing：灰度、通道反色、旋转、翻转、缩放、颜色拾取
//       - preprocess：PreprocessPipeline 顺序编排
//       - view_transform：屏幕 ↔ 逻辑坐标换算、以指定点缩放
//       - engine：Grid-Selection-Emit 引擎的配置骨架（仅构造数据结构，不触发桩算法）
// 分块依据：每一步聚焦一个模块或一个典型场景，便于按需拷贝到实际项目。
// ============================================================================
#include <iostream>

#include "core/color.h"
#include "core/image.h"
#include "core/point.h"
#include "engine/engine.h"
#include "annotation/annotation_layer.h"
#include "annotation/rasterizer.h"
#include "annotation/shape_factory.h"
#include "annotation/view_transform.h"
#include "geometry/shapes.h"
#include "preprocess/preprocess_pipeline.h"
#include "processing/color_ops.h"
#include "processing/geometric_ops.h"

// 打印图像基本信息，便于观察每一步的结果。
static void printInfo(const char* label, const idc::core::Image& img) {
    std::cout << "[" << label << "] "
              << img.width() << " x " << img.height()
              << "  format=" << (img.isGray() ? "GRAY"
                                              : (img.format() == idc::core::ImageFormat::RGBA ? "RGBA" : "RGB"))
              << "\n";
}

int main() {
    using namespace idc;

    // ------------------------------------------------------------------
    // 1. 构造一张 200x120 的白底 RGBA 图像，作为绘图的底图
    // ------------------------------------------------------------------
    core::Image base(200, 120, core::ImageFormat::RGBA);
    base.fill(core::kWhite);
    printInfo("base", base);

    // ------------------------------------------------------------------
    // 2. 使用 ShapeFactory 构造若干形状，并提交到 AnnotationLayer
    // ------------------------------------------------------------------
    annotation::AnnotationLayer panel;
    panel.setImage(base);

    // 2.1 画一条红色直线：(20,20) → (100,80)
    {
        annotation::ShapeRequest req;
        req.p1 = {20, 20};
        req.p2 = {100, 80};
        req.type = geometry::ShapeType::LINE;
        panel.changeColor(core::kRed);
        panel.changeStrokeWidth(3);
        panel.addAnnotation(annotation::buildShape(req));
    }

    // 2.2 画一个蓝色填充矩形：(110,20) → (180,70)
    {
        annotation::ShapeRequest req;
        req.p1 = {110, 20};
        req.p2 = {180, 70};
        req.type = geometry::ShapeType::RECTANGLE;
        panel.changeColor(core::kBlue);
        panel.changeFill(true);
        panel.addAnnotation(annotation::buildShape(req));
        panel.changeFill(false); // 还原，避免影响后续
    }

    // 2.3 画一个绿色圆形：圆心 (60, 90)，半径 20
    {
        annotation::ShapeRequest req;
        req.p1 = {60, 90};
        req.p2 = {80, 90}; // 距离 = 20，作为半径
        req.type = geometry::ShapeType::CIRCLE;
        panel.changeColor(core::kGreen);
        panel.changeStrokeWidth(2);
        panel.addAnnotation(annotation::buildShape(req));
    }

    std::cout << "shapes committed: " << panel.annotations().size() << "\n";

    // 2.4 合成：把底图与所有形状合并为一张最终图像
    core::Image composed = panel.burnIn();
    printInfo("composed", composed);

    // ------------------------------------------------------------------
    // 3. 撤销 / 重做演示
    // ------------------------------------------------------------------
    panel.revoke(); // 撤销最后一个（绿色圆形）
    std::cout << "after revoke: " << panel.annotations().size() << " shapes\n";
    panel.redo();   // 重做，恢复绿色圆形
    std::cout << "after redo:   " << panel.annotations().size() << " shapes\n";

    // ------------------------------------------------------------------
    // 4. 命中检测演示：判断点 (60, 90) 是否落在某个形状上
    // ------------------------------------------------------------------
    if (auto idx = panel.hitTest({60, 90})) {
        std::cout << "hitTest(60,90) -> shape #" << *idx
                  << " type=" << geometry::shapeTypeName(panel.annotations()[*idx].shapeType)
                  << "\n";
    } else {
        std::cout << "hitTest(60,90) -> miss\n";
    }

    // ------------------------------------------------------------------
    // 5. 图像处理流水线演示：灰度 → 反色
    //    两个步骤都是保留的核心 processing 能力
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
    core::Image rot90 = processing::rotate(pipelineOut, 90);
    printInfo("rotate 90", rot90);

    core::Image flipped = processing::flip(rot90, true);
    printInfo("flip H", flipped);

    core::Image half = processing::scale(flipped, 0.5, processing::ResampleMode::BILINEAR);
    printInfo("scale 0.5x", half);

    // ------------------------------------------------------------------
    // 7. 颜色选取演示：拾取 (10, 10) 处像素颜色
    // ------------------------------------------------------------------
    if (auto c = processing::pickColor(composed, 10, 10)) {
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
    // 9. 引擎配置骨架演示：构造一次“L2 反向剔除”作业的 EngineConfig。
    //    此处只组装数据结构（对应终稿 §9 的 JSON 配置），不调用尚未实现的
    //    切割/剔除/导出流水线函数，那些属于新项目 MVP 阶段的开发内容。
    // ------------------------------------------------------------------
    engine::EngineConfig cfg;
    cfg.source = engine::SourceInfo{composed.width(), composed.height()};
    cfg.cut.tier = engine::Tier::L2;                 // 反向剔除模式
    cfg.cut.generator = engine::CutGenerator::RECT;  // 单矩形选框诱导十字切割线
    cfg.cut.rect = engine::RectRegion(60, 30, 140, 90);
    cfg.cut.polarity = engine::Polarity::REMOVE;     // 剔除框内 → 保留四角
    cfg.emit.mode = engine::EmitMode::MERGED;
    cfg.emit.layout = engine::MergeLayout::COLLAPSE; // 坍缩式合并（§5.2）
    std::cout << "engine config: tier=L2 polarity=REMOVE rect=("
              << cfg.cut.rect.left << "," << cfg.cut.rect.top << ","
              << cfg.cut.rect.right << "," << cfg.cut.rect.bottom << ")\n";

    std::cout << "demo finished.\n";
    return 0;
}
