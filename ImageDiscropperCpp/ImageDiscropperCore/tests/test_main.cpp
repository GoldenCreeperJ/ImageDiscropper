// ============================================================================
// 文件：tests/test_main.cpp
// 作用：极简冒烟测试入口，覆盖 core / geometry / pixel_ops / annotation / preprocess /
//       history / engine 七大模块的关键行为，并调用引擎验收测试段（pipeline / export /
//       boundary，定义于独立 test_engine_*.cpp）。使用共享 CHECK 宏，不依赖第三方框架。
// 分块依据：每个模块一个 testXxx() 函数，main 依次调用；引擎的完整流水线/导出/边界
//       验收拆分到 test_engine_*.cpp，经 test_harness.h 声明后在此统一调度，避免 God File。
// ============================================================================
#include <cstdlib>
#include <iostream>

#include "test_harness.h"

#include "core/color.h"
#include "core/image.h"
#include "core/point.h"
#include "engine/engine.h"
#include "annotation/annotation_layer.h"
#include "annotation/rasterizer.h"
#include "annotation/shape_factory.h"
#include "annotation/view_transform.h"
#include "geometry/shapes.h"
#include "history/history_manager.h"
#include "preprocess/preprocess_pipeline.h"
#include "pixel_ops/color_ops.h"
#include "pixel_ops/geometric_ops.h"

// ---------------------------------------------------------------------------
// core 模块测试：颜色运算、图像读写、格式转换
// ---------------------------------------------------------------------------
static void testCore() {
    using namespace idc::core;

    // 反色
    constexpr Color c(10, 20, 30, 255);
    const Color inv = inverse(c);
    CHECK(inv.r == 245 && inv.g == 235 && inv.b == 225 && inv.a == 255);

    // 感知反色：亮色 → 黑，暗色 → 白
    CHECK(perceivedInverse(Color(255, 255, 255)).r == 0);
    CHECK(perceivedInverse(Color(0, 0, 0)).r == 255);

    // 互补色：红色的互补色应接近青色
    const Color comp = complementary(Color(255, 0, 0));
    CHECK(comp.r < 32 && comp.g > 200 && comp.b > 200);

    // 图像读写与格式转换
    Image img(4, 3, ImageFormat::RGBA);
    img.fill(Color(100, 150, 200, 255));
    CHECK(img.getPixel(0, 0) == Color(100, 150, 200, 255));
    CHECK(img.getPixel(3, 2) == Color(100, 150, 200, 255));

    const Image gray = img.toGray();
    CHECK(gray.isGray());
    // 灰度值 ≈ 0.299*100 + 0.587*150 + 0.114*200 ≈ 141
    CHECK(std::abs(static_cast<int>(gray.getGray(0, 0)) - 141) <= 1);

    std::cout << "[core] OK\n";
}

// ---------------------------------------------------------------------------
// geometry 模块测试：Path 命中检测、形状包围盒、控制点
// ---------------------------------------------------------------------------
static void testGeometry() {
    using namespace idc;
    using namespace idc::geometry;

    // 矩形包围盒
    const RectShape rect(10, 20, 30, 40);
    const auto [x, y, width, height] = rect.bounds();
    CHECK(x == 10 && y == 20 && width == 30 && height == 40);
    CHECK(rect.contains(core::Point2D{25, 40}));
    CHECK(!rect.contains(core::Point2D{5, 5}));
    CHECK(rect.controlPoints().size() == 4);

    // 椭圆包围盒与内部判定
    const EllipseShape ell(0, 0, 100, 50);
    CHECK(ell.contains(core::Point2D{50, 25}));   // 中心
    CHECK(!ell.contains(core::Point2D{99, 49}));  // 角落外

    // 路径命中检测
    Path p;
    p.moveTo(0, 0);
    p.lineTo(100, 0);
    p.lineTo(100, 100);
    p.lineTo(0, 100);
    p.closePath();
    CHECK(p.contains(50, 50));
    CHECK(!p.contains(150, 50));
    CHECK(p.distanceToOutline(50, 50) > 40.0); // 中心离边框较远
    CHECK(p.distanceToOutline(0, 50) < 1.0);   // 边框上

    std::cout << "[geometry] OK\n";
}

// ---------------------------------------------------------------------------
// pixel_ops 模块测试：灰度、旋转、翻转、缩放（含纯色等价性）、通道反色 / 分离（含灰度反色）、颜色拾取
// ---------------------------------------------------------------------------
static void testPixelOps() {
    using namespace idc;

    // 构造一张 8x8 的渐变彩色图
    core::Image img(8, 8, core::ImageFormat::RGBA);
    for (int y = 0; y < 8; ++y) {
        for (int x = 0; x < 8; ++x) {
            img.setPixel(x, y, core::Color(x * 32, y * 32, 128, 255));
        }
    }

    // 灰度化
    core::Image gray = pixel_ops::toGray(img);
    CHECK(gray.isGray());
    CHECK(gray.width() == 8 && gray.height() == 8);

    // 旋转 90°：宽高互换
    core::Image rot = pixel_ops::rotate(img, 90);
    CHECK(rot.width() == 8 && rot.height() == 8);
    // 旋转 180° 后回到原尺寸
    core::Image rot180 = pixel_ops::rotate(img, 180);
    CHECK(rot180.width() == 8 && rot180.height() == 8);
    // 旋转 360° 等价于原图
    core::Image rot360 = pixel_ops::rotate(img, 360);
    CHECK(rot360.getPixel(0, 0) == img.getPixel(0, 0));

    // 水平翻转：(0,0) 与 (w-1,0) 交换
    core::Image flipH = pixel_ops::flip(img, true);
    CHECK(flipH.getPixel(0, 0) == img.getPixel(7, 0));
    CHECK(flipH.getPixel(7, 0) == img.getPixel(0, 0));

    // 缩放：放大到 16x16
    core::Image scaled = pixel_ops::resize(img, 16, 16, pixel_ops::ResampleMode::BILINEAR);
    CHECK(scaled.width() == 16 && scaled.height() == 16);

    // 通道反色：R 通道反色后 (0,0) 处 R 应变为 255 - 0 = 255
    core::Image invR = pixel_ops::invertChannels(img, "100");
    CHECK(invR.getPixel(0, 0).r == 255);
    CHECK(invR.getPixel(0, 0).g == 0);   // G 未反色，仍为原值 0

    // 通道分离：只保留 B 通道，R/G 应为 0
    core::Image onlyB = pixel_ops::splitChannels(img, "001");
    CHECK(onlyB.getPixel(3, 3).r == 0);
    CHECK(onlyB.getPixel(3, 3).g == 0);
    CHECK(onlyB.getPixel(3, 3).b == 128);

    // 颜色拾取：合法坐标返回 optional 有值，越界返回 nullopt
    CHECK(pixel_ops::pickColor(img, 0, 0).has_value());
    CHECK(!pixel_ops::pickColor(img, 100, 100).has_value());

    // ---- 灰度图反色（黑白后仍可反色）：对单一亮度通道取 255 - v，格式仍为 GRAY ----
    core::Image gimg(4, 4, core::ImageFormat::GRAY);
    for (int y = 0; y < 4; ++y)
        for (int x = 0; x < 4; ++x)
            gimg.setGray(x, y, static_cast<std::uint8_t>(x * 40 + y * 10)); // 值域 0..150
    const core::Image ginv = pixel_ops::invertChannels(gimg, "111");
    CHECK(ginv.isGray());
    CHECK(ginv.width() == 4 && ginv.height() == 4);
    CHECK(ginv.getGray(0, 0) == 255);                      // 255 - 0
    CHECK(ginv.getGray(3, 3) == 255 - gimg.getGray(3, 3)); // 逐像素取反
    // 灰度反色忽略 mask：任意 mask 都整体反相
    CHECK(pixel_ops::invertChannels(gimg, "100").getGray(2, 1) == 255 - gimg.getGray(2, 1));
    // 反色为对合：两次反色回到原值
    CHECK(pixel_ops::invertChannels(ginv, "111").getGray(1, 2) == gimg.getGray(1, 2));

    // ---- 灰度图色道分离无意义：返回副本（像素不变、仍为 GRAY）----
    const core::Image gsplit = pixel_ops::splitChannels(gimg, "001");
    CHECK(gsplit.isGray());
    CHECK(gsplit.getGray(2, 2) == gimg.getGray(2, 2));

    // ---- 彩色反色 "111"：R/G/B 全反、alpha 保留 ----
    core::Image aimg(2, 2, core::ImageFormat::RGBA);
    aimg.fill(core::Color(10, 200, 30, 77));
    const core::Image ainv = pixel_ops::invertChannels(aimg, "111");
    CHECK(ainv.getPixel(0, 0) == core::Color(245, 55, 225, 77)); // alpha=77 不变

    // ---- 彩色分离 "010"：仅留 G，R/B 置 0，alpha 保留 ----
    const core::Image asplit = pixel_ops::splitChannels(aimg, "010");
    CHECK(asplit.getPixel(1, 1) == core::Color(0, 200, 0, 77));

    // ---- resize 等价性：纯色图缩放后仍为同一纯色（NEAREST / BILINEAR 均不应引入偏差；2× 放大→权重为二进制精确值）----
    core::Image solid(6, 4, core::ImageFormat::RGBA);
    solid.fill(core::Color(12, 34, 56, 78));
    const core::Image solidNear = pixel_ops::resize(solid, 12, 8, pixel_ops::ResampleMode::NEAREST);
    const core::Image solidBil = pixel_ops::resize(solid, 12, 8, pixel_ops::ResampleMode::BILINEAR);
    CHECK(solidNear.width() == 12 && solidNear.height() == 8);
    CHECK(solidNear.getPixel(0, 0) == core::Color(12, 34, 56, 78));
    CHECK(solidNear.getPixel(11, 7) == core::Color(12, 34, 56, 78));
    CHECK(solidBil.getPixel(0, 0) == core::Color(12, 34, 56, 78));
    CHECK(solidBil.getPixel(5, 3) == core::Color(12, 34, 56, 78));   // 内部点双线性仍为纯色
    CHECK(solidBil.getPixel(11, 7) == core::Color(12, 34, 56, 78));

    // ---- resize 最近邻：2x2 放大到 4x4，四象限精确复制（验证采样坐标无偏移）----
    core::Image quad(2, 2, core::ImageFormat::RGB);
    quad.setPixel(0, 0, core::Color(255, 0, 0, 255));   // 左上 红
    quad.setPixel(1, 0, core::Color(0, 255, 0, 255));   // 右上 绿
    quad.setPixel(0, 1, core::Color(0, 0, 255, 255));   // 左下 蓝
    quad.setPixel(1, 1, core::Color(255, 255, 0, 255)); // 右下 黄
    const core::Image quad4 = pixel_ops::resize(quad, 4, 4, pixel_ops::ResampleMode::NEAREST);
    CHECK(quad4.getPixel(0, 0) == core::Color(255, 0, 0, 255));
    CHECK(quad4.getPixel(3, 0) == core::Color(0, 255, 0, 255));
    CHECK(quad4.getPixel(0, 3) == core::Color(0, 0, 255, 255));
    CHECK(quad4.getPixel(3, 3) == core::Color(255, 255, 0, 255));

    // ---- resize 灰度：GRAY 输入缩放后仍为 GRAY，纯色保持（单通道路径）----
    core::Image gsolid(4, 4, core::ImageFormat::GRAY);
    for (int y = 0; y < 4; ++y)
        for (int x = 0; x < 4; ++x)
            gsolid.setGray(x, y, 200);
    const core::Image gscaled = pixel_ops::resize(gsolid, 8, 8, pixel_ops::ResampleMode::BILINEAR);
    CHECK(gscaled.isGray());
    CHECK(gscaled.getGray(3, 5) == 200);

    std::cout << "[pixel_ops] OK\n";
}

// ---------------------------------------------------------------------------
// annotation 模块测试：ShapeFactory、Rasterizer、AnnotationLayer、ViewTransform
// ---------------------------------------------------------------------------
static void testAnnotation() {
    using namespace idc;

    // ShapeFactory：矩形
    annotation::ShapeRequest req;
    req.p1 = {10, 10};
    req.p2 = {50, 40};
    req.type = geometry::ShapeType::RECTANGLE;
    auto rect = annotation::buildShape(req);
    CHECK(rect != nullptr);
    CHECK(rect->type() == geometry::ShapeType::RECTANGLE);

    // ShapeFactory：圆
    req.type = geometry::ShapeType::CIRCLE;
    req.p1 = {100, 100};
    req.p2 = {120, 100}; // 半径 20
    auto circle = annotation::buildShape(req);
    CHECK(circle != nullptr);
    CHECK(circle->type() == geometry::ShapeType::CIRCLE);

    // Rasterizer：在 200x200 白底上画红色矩形
    core::Image canvas(200, 200, core::ImageFormat::RGBA);
    canvas.fill(core::kWhite);
    annotation::PaintStyle style;
    style.color = core::kRed;
    style.strokeWidth = 2;
    style.fill = true;
    annotation::rasterize(canvas, *rect, style);
    // 矩形内部 (30, 25) 应被染红
    core::Color inside = canvas.getPixel(30, 25);
    CHECK(inside.r > 200 && inside.g < 50 && inside.b < 50);

    // AnnotationLayer：提交形状 + 撤销 + 重做 + 清除
    annotation::AnnotationLayer panel;
    panel.setImage(canvas);
    panel.changeColor(core::kBlue);
    panel.addAnnotation(std::move(rect));
    CHECK(panel.annotations().size() == 1);
    panel.revoke();
    CHECK(panel.annotations().empty());
    panel.redo();
    CHECK(panel.annotations().size() == 1);
    panel.clear();
    CHECK(panel.annotations().empty());

    // AnnotationLayer：EDIT 模式下修改选中形状的颜色
    annotation::ShapeRequest r2;
    r2.p1 = {10, 10};
    r2.p2 = {60, 60};
    r2.type = geometry::ShapeType::RECTANGLE;
    panel.changeColor(core::kRed);
    panel.addAnnotation(annotation::buildShape(r2));
    panel.setEditType(annotation::EditType::EDIT);
    panel.selectAnnotation(0);
    panel.changeColor(core::kGreen);
    CHECK(panel.annotations()[0].color == core::kGreen);

    // AnnotationLayer：删除指定下标标注（removeAnnotation）——加第二个矩形后删 index 0，
    // 应仅剩原第 2 个；且删除作为新操作会清空重做栈，选中下标同步修正。
    annotation::ShapeRequest r3;
    r3.p1 = {70, 70};
    r3.p2 = {90, 90};
    r3.type = geometry::ShapeType::RECTANGLE;
    panel.setEditType(annotation::EditType::DRAW);
    panel.changeColor(core::kBlue);
    panel.addAnnotation(annotation::buildShape(r3));
    CHECK(panel.annotations().size() == 2);
    panel.selectAnnotation(1);            // 选中第二个
    panel.removeAnnotation(0);            // 删除第一个
    CHECK(panel.annotations().size() == 1);
    CHECK(panel.annotations()[0].color == core::kBlue);      // 剩下的是原第 2 个（蓝色）
    CHECK(panel.selectedAnnotationIndex().has_value() && *panel.selectedAnnotationIndex() == 0); // 选中下标前移
    panel.removeAnnotation(99);           // 越界删除应被忽略
    CHECK(panel.annotations().size() == 1);

    // Shape::translate：就地平移保留具体类型（不再退化为通用 PATH）。
    // 矩形：平移后 type() 仍为 RECTANGLE，包围盒按 (dx,dy) 偏移、宽高不变。
    annotation::ShapeRequest r4;
    r4.p1 = {10, 10};
    r4.p2 = {30, 20};
    r4.type = geometry::ShapeType::RECTANGLE;
    auto movable = annotation::buildShape(r4);
    const auto [x0, y0, width0, height0] = movable->bounds();
    movable->translate(5, 7);
    const auto [x1, y1, width1, height1] = movable->bounds();
    CHECK(movable->type() == geometry::ShapeType::RECTANGLE);            // 类型未退化
    CHECK(std::abs(x1 - x0 - 5.0) < 1e-6);
    CHECK(std::abs(y1 - y0 - 7.0) < 1e-6);
    CHECK(std::abs(width1 - width0) < 1e-6 && std::abs(height1 - height0) < 1e-6);

    // 文字：平移后仍为 TEXT（保留字形渲染所需的类型身份），基线锚点随之偏移。
    annotation::ShapeRequest r5;
    r5.type = geometry::ShapeType::TEXT;
    r5.p1 = {40, 40};
    r5.p2 = {40, 40};
    r5.text = "hi";
    r5.fontSize = 16;
    auto txt = annotation::buildShape(r5);
    const geometry::BoundingBox t0 = txt->bounds();
    txt->translate(-3, 4);
    const geometry::BoundingBox t1 = txt->bounds();
    CHECK(txt->type() == geometry::ShapeType::TEXT);                     // 未退化为 PATH
    CHECK(std::abs(t1.x - t0.x + 3.0) < 1e-6);
    CHECK(std::abs(t1.y - t0.y - 4.0) < 1e-6);

    // 多边形（正方形）：平移后仍为 SQUARE，逐顶点偏移。
    annotation::ShapeRequest r6;
    r6.type = geometry::ShapeType::SQUARE;
    r6.p1 = {0, 0};
    r6.p2 = {10, 0};
    auto sq = annotation::buildShape(r6);
    const geometry::BoundingBox s0 = sq->bounds();
    sq->translate(2, 2);
    const geometry::BoundingBox s1 = sq->bounds();
    CHECK(sq->type() == geometry::ShapeType::SQUARE);
    CHECK(std::abs(s1.x - s0.x - 2.0) < 1e-6 && std::abs(s1.y - s0.y - 2.0) < 1e-6);

    // ViewTransform：屏幕 ↔ 逻辑坐标
    annotation::ViewTransform vt;
    vt.fitToViewport(100, 100, 200, 200);
    CHECK(std::abs(vt.scale() - 2.0) < 0.01);
    core::Point2D logical = vt.screenToLogical({100, 100});
    CHECK(std::abs(logical.x - 50.0) < 0.5);
    CHECK(std::abs(logical.y - 50.0) < 0.5);

    std::cout << "[annotation] OK\n";
}

// ---------------------------------------------------------------------------
// preprocess 模块测试：流水线按顺序执行
// ---------------------------------------------------------------------------
static void testPreprocess() {
    using namespace idc;

    core::Image img(16, 16, core::ImageFormat::RGBA);
    img.fill(core::Color(128, 64, 32, 255));

    preprocess::PreprocessPipeline pipe;
    pipe.add(preprocess::GrayOp{});
    pipe.add(preprocess::RotateOp{90});
    pipe.add(preprocess::FlipOp{true});

    const core::Image out = pipe.apply(img);
    CHECK(out.isGray());
    CHECK(out.width() == 16 && out.height() == 16);

    // 步骤管理
    CHECK(pipe.size() == 3);
    // moveUp(2) 把 FlipOp 从 index 2 上移到 index 1
    pipe.moveUp(2);
    // FlipOp 在 PreprocessConfig variant 中的序号是 4
    // （GrayOp=0, SplitOp=1, InvertOp=2, RotateOp=3, FlipOp=4）
    CHECK(pipe.steps()[1].index() == 4);
    pipe.remove(0);
    CHECK(pipe.size() == 2);

    // configType 与 preprocessTypeName 辅助函数
    CHECK(preprocess::configType(preprocess::GrayOp{}) == preprocess::PreprocessType::GRAY);
    CHECK(std::string(preprocess::preprocessTypeName(preprocess::PreprocessType::FLIP)) == "Flip");

    std::cout << "[preprocess] OK\n";
}

// ---------------------------------------------------------------------------
// history 模块测试：泛型撤销 / 重做栈
// ---------------------------------------------------------------------------
static void testHistory() {
    idc::history::HistoryManager<int> h;
    CHECK(!h.canUndo() && !h.canRedo());

    h.push(1);
    h.push(2);
    h.push(3);
    CHECK(h.undoSize() == 3);
    CHECK(h.undoStack().back() == 3);

    // popToRedo：弹出栈顶到重做栈，返回该条目
    const auto popped = h.popToRedo();
    CHECK(popped.has_value() && *popped == 3);
    CHECK(h.undoSize() == 2 && h.redoSize() == 1);
    CHECK(h.undoStack().back() == 2);

    // popFromRedo：从重做栈弹回到主栈，返回该条目
    const auto restored = h.popFromRedo();
    CHECK(restored.has_value() && *restored == 3);
    CHECK(h.undoSize() == 3 && h.redoSize() == 0);
    CHECK(h.undoStack().back() == 3);

    // 新 push 会清空重做栈
    h.popToRedo();
    CHECK(h.redoSize() == 1);
    h.push(99);
    CHECK(h.redoSize() == 0);
    CHECK(h.undoStack().back() == 99);

    std::cout << "[history] OK\n";
}

// ---------------------------------------------------------------------------
// engine 模块测试：Grid-Selection-Emit 引擎的数据结构骨架（不含算法逻辑）
// ---------------------------------------------------------------------------
static void testEngine() {
    using namespace idc::engine;

    // RectRegion：左闭右开的宽高、面积与包含判定
    RectRegion r(10, 20, 30, 50);
    CHECK(r.width() == 20 && r.height() == 30);
    CHECK(r.area() == 600);
    CHECK(r.contains(10, 20) && !r.contains(30, 50)); // 右/下边界不含
    CHECK(RectRegion(5, 5, 5, 9).empty());            // 退化区域视为空

    // 水平带 / 垂直带构造
    CHECK(horizontalBand(10, 20, 100) == RectRegion(0, 10, 100, 20));
    CHECK(verticalBand(10, 20, 100) == RectRegion(10, 0, 20, 100));

    // CutLineSet：由竖线/横线集合推导行列数
    CutLineSet lines;
    lines.xs = {0, 10, 20, 100};
    lines.ys = {0, 50, 100};
    CHECK(lines.columns() == 3 && lines.rows() == 2);

    // RegionSet：追加、统计面积与包围盒
    RegionSet set;
    set.add(RectRegion(0, 0, 10, 10));
    set.add(RectRegion(20, 20, 30, 30));
    CHECK(set.size() == 2 && set.totalArea() == 200);
    CHECK(set.boundingBox() == RectRegion(0, 0, 30, 30));

    // Selection：点选 / 全选 / 反选 / 极性设置（resolve 的完整语义见 test_engine_pipeline）
    Selection sel;
    sel.resize(4);
    sel.select(1);
    CHECK(sel.selectedCount() == 1);
    sel.selectAll();
    CHECK(sel.selectedCount() == 4);
    sel.invert();
    CHECK(sel.selectedCount() == 0);
    sel.setPolarity(Polarity::REMOVE);
    CHECK(sel.polarity() == Polarity::REMOVE);

    // Sequence：自定义序列可直接设置
    Sequence seq;
    seq.setCustom({3, 1, 2, 0});
    CHECK(seq.size() == 4 && seq.order().front() == 3);

    // EngineConfig：三层模式共用的配置聚合可正常构造与赋值
    EngineConfig cfg;
    cfg.source = SourceInfo{1920, 1080};
    cfg.cut.tier = Tier::L2;
    cfg.cut.generator = CutGenerator::RECT;
    cfg.cut.rect = RectRegion(300, 200, 900, 700);
    cfg.cut.polarity = Polarity::REMOVE;
    cfg.emitParams.mode = EmitMode::MERGED;
    cfg.emitParams.layout = MergeLayout::COLLAPSE;
    CHECK(cfg.source.width == 1920 && cfg.cut.tier == Tier::L2);
    CHECK(cfg.cut.polarity == Polarity::REMOVE);

    std::cout << "[engine] OK\n";
}

// ---------------------------------------------------------------------------
// main：依次执行所有测试段
// ---------------------------------------------------------------------------
int main() {
    testCore();
    testGeometry();
    testPixelOps();
    testAnnotation();
    testPreprocess();
    testHistory();
    testEngine();          // 引擎数据结构骨架
    testEnginePipeline();  // 引擎流水线功能（§3.1）
    testEngineExport();    // 引擎导出落盘（§5）
    testEngineBoundary();  // 引擎边界异常（§6 / E-1~E-8）
    std::cout << "all tests passed.\n";
    return 0;
}
