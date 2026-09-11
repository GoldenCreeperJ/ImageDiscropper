// ============================================================================
// 文件：tests/test_main.cpp
// 作用：极简冒烟测试，覆盖 core / geometry / processing / annotation / preprocess /
//       history / engine 七大模块的关键行为。使用 CHECK 宏 + 标准输出，不依赖第三方框架。
//       其中 engine 仅测试 Grid-Selection-Emit 骨架中已可用的数据结构，
//       不触及桩实现的切割/剔除/网格/导出算法（那些留待 MVP 及后续阶段）。
// 分块依据：每个模块一个 testXxx() 函数，main 依次调用；便于定位失败范围。
// ============================================================================
#include <cmath>
#include <cstdlib>
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
#include "history/history_manager.h"
#include "preprocess/preprocess_pipeline.h"
#include "processing/color_ops.h"
#include "processing/geometric_ops.h"

// 简易断言宏：失败时打印文件 / 行号 / 表达式。
#define CHECK(expr)                                                            \
    do {                                                                       \
        if (!(expr)) {                                                         \
            std::cerr << "CHECK failed: " #expr " @ " << __FILE__ << ":"       \
                      << __LINE__ << "\n";                                     \
            std::exit(1);                                                      \
        }                                                                      \
    } while (0)

// ---------------------------------------------------------------------------
// core 模块测试：颜色运算、图像读写、格式转换
// ---------------------------------------------------------------------------
static void testCore() {
    using namespace idc::core;

    // 反色
    Color c(10, 20, 30, 255);
    Color inv = inverse(c);
    CHECK(inv.r == 245 && inv.g == 235 && inv.b == 225 && inv.a == 255);

    // 感知反色：亮色 → 黑，暗色 → 白
    CHECK(perceivedInverse(Color(255, 255, 255)).r == 0);
    CHECK(perceivedInverse(Color(0, 0, 0)).r == 255);

    // 互补色：红色的互补色应接近青色
    Color comp = complementary(Color(255, 0, 0));
    CHECK(comp.r < 32 && comp.g > 200 && comp.b > 200);

    // 图像读写与格式转换
    Image img(4, 3, ImageFormat::RGBA);
    img.fill(Color(100, 150, 200, 255));
    CHECK(img.getPixel(0, 0) == Color(100, 150, 200, 255));
    CHECK(img.getPixel(3, 2) == Color(100, 150, 200, 255));

    Image gray = img.toGray();
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
    RectShape rect(10, 20, 30, 40);
    BoundingBox bb = rect.bounds();
    CHECK(bb.x == 10 && bb.y == 20 && bb.width == 30 && bb.height == 40);
    CHECK(rect.contains(core::Point2D{25, 40}));
    CHECK(!rect.contains(core::Point2D{5, 5}));
    CHECK(rect.controlPoints().size() == 4);

    // 椭圆包围盒与内部判定
    EllipseShape ell(0, 0, 100, 50);
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
// processing 模块测试：灰度、旋转、翻转、缩放、通道反色 / 分离、颜色拾取
// ---------------------------------------------------------------------------
static void testProcessing() {
    using namespace idc;

    // 构造一张 8x8 的渐变彩色图
    core::Image img(8, 8, core::ImageFormat::RGBA);
    for (int y = 0; y < 8; ++y) {
        for (int x = 0; x < 8; ++x) {
            img.setPixel(x, y, core::Color(x * 32, y * 32, 128, 255));
        }
    }

    // 灰度化
    core::Image gray = processing::toGray(img);
    CHECK(gray.isGray());
    CHECK(gray.width() == 8 && gray.height() == 8);

    // 旋转 90°：宽高互换
    core::Image rot = processing::rotate(img, 90);
    CHECK(rot.width() == 8 && rot.height() == 8);
    // 旋转 180° 后回到原尺寸
    core::Image rot180 = processing::rotate(img, 180);
    CHECK(rot180.width() == 8 && rot180.height() == 8);
    // 旋转 360° 等价于原图
    core::Image rot360 = processing::rotate(img, 360);
    CHECK(rot360.getPixel(0, 0) == img.getPixel(0, 0));

    // 水平翻转：(0,0) 与 (w-1,0) 交换
    core::Image flipH = processing::flip(img, true);
    CHECK(flipH.getPixel(0, 0) == img.getPixel(7, 0));
    CHECK(flipH.getPixel(7, 0) == img.getPixel(0, 0));

    // 缩放：放大到 16x16
    core::Image scaled = processing::resize(img, 16, 16, processing::ResampleMode::BILINEAR);
    CHECK(scaled.width() == 16 && scaled.height() == 16);

    // 通道反色：R 通道反色后 (0,0) 处 R 应变为 255 - 0 = 255
    core::Image invR = processing::invertChannels(img, "100");
    CHECK(invR.getPixel(0, 0).r == 255);
    CHECK(invR.getPixel(0, 0).g == 0);   // G 未反色，仍为原值 0

    // 通道分离：只保留 B 通道，R/G 应为 0
    core::Image onlyB = processing::splitChannels(img, "001");
    CHECK(onlyB.getPixel(3, 3).r == 0);
    CHECK(onlyB.getPixel(3, 3).g == 0);
    CHECK(onlyB.getPixel(3, 3).b == 128);

    // 颜色拾取：合法坐标返回 optional 有值，越界返回 nullopt
    CHECK(processing::pickColor(img, 0, 0).has_value());
    CHECK(!processing::pickColor(img, 100, 100).has_value());

    std::cout << "[processing] OK\n";
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

    core::Image out = pipe.apply(img);
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
    auto popped = h.popToRedo();
    CHECK(popped.has_value() && *popped == 3);
    CHECK(h.undoSize() == 2 && h.redoSize() == 1);
    CHECK(h.undoStack().back() == 2);

    // popFromRedo：从重做栈弹回到主栈，返回该条目
    auto restored = h.popFromRedo();
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

    // Selection：点选 / 全选 / 反选（resolve 为桩实现，不在此断言其结果）
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
    cfg.emit.mode = EmitMode::MERGED;
    cfg.emit.layout = MergeLayout::COLLAPSE;
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
    testProcessing();
    testAnnotation();
    testPreprocess();
    testHistory();
    testEngine();
    std::cout << "all tests passed.\n";
    return 0;
}
