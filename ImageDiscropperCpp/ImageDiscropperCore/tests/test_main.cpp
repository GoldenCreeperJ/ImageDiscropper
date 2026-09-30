// ============================================================================
// 文件：tests/test_main.cpp
// 作用：极简冒烟测试入口，覆盖 core / geometry / annotation / preprocess /
//       history / engine 七大模块的关键行为，并调用引擎验收测试段（pipeline / export /
//       boundary，定义于独立 test_engine_*.cpp）。使用共享 CHECK 宏，不依赖第三方框架。
// 分块依据：每个模块一个 testXxx() 函数，main 依次调用；引擎的完整流水线/导出/边界
//       验收拆分到 test_engine_*.cpp，经 test_harness.h 声明后在此统一调度，避免 God File。
// ============================================================================
#include <array>
#include <cstdlib>
#include <iostream>
#include <random>
#include <string>

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
#include "core/history_manager.h"
#include "preprocess/preprocess_pipeline.h"

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

    // 矩形（已并入 PolygonShape：四点闭合多边形）包围盒 / 内部判定 / 控制点
    const PolygonShape rect({{10, 20}, {40, 20}, {40, 60}, {10, 60}}, ShapeType::RECTANGLE, true);
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

    // 文本宽度估算：按字符数（非字节数）——ASCII 半宽、多字节字符全宽，中文不再被高估。
    const TextShape asciiText("abc", 0, 0, 10);
    const auto [ax, ay, aw, ah] = asciiText.bounds();
    CHECK(aw == 15.0);                       // 3 个 ASCII × 0.5 × 10
    const TextShape cjkText("中文", 0, 0, 10);
    const auto [cx, cy, cw, ch] = cjkText.bounds();
    CHECK(cw == 20.0);                       // 2 个中文字符 × 1.0 × 10（而非 6 字节 × 0.5 × 10 = 30）
    const TextShape mixedText("中a", 0, 0, 10);
    const auto [mx, my, mw, mh] = mixedText.bounds();
    CHECK(mw == 15.0);                       // 1.0 + 0.5 = 1.5 × 10

    // 转角圆角（倒角）：半径>0 时 toPath 含二次贝塞尔段；AABB 与直角时一致（bounds 取控制点=原顶点）。
    // 注：先将 toPath() 存为命名 Path 再遍历（直接 x.toPath().segments() 会使临时量悬挂）。
    auto hasQuadSeg = [](const Path& p) {
        for (const auto&[type, coords] : p.segments()) {
            if (type == PathSegmentType::QUAD_TO) return true;
        }
        return false;
    };
    const PolygonShape rrect({{0, 0}, {100, 0}, {100, 100}, {0, 100}}, ShapeType::RECTANGLE, true, 20.0);
    const Path rrectPath = rrect.toPath();
    CHECK(hasQuadSeg(rrectPath));
    const auto [rx, ry, rw, rh] = rrect.bounds();
    CHECK(rx == 0 && ry == 0 && rw == 100 && rh == 100);
    CHECK(rrect.cornerRadius() == 20.0);

    // setter 往返 + 负值钳为 0；clone 保留圆角半径。
    PolygonShape mut({{0, 0}, {10, 0}, {10, 10}}, ShapeType::ISOS_TRIANGLE, true);
    mut.setCornerRadius(5.0);
    CHECK(mut.cornerRadius() == 5.0);
    mut.setCornerRadius(-3.0);
    CHECK(mut.cornerRadius() == 0.0);
    mut.setCornerRadius(7.0);
    CHECK(mut.clone()->cornerRadius() == 7.0);

    // 开放折线：内部顶点倒角（含 QUAD）；现由 PolygonShape 直接表达（不再走 PathShape）。
    const PolygonShape openLine({{0, 0}, {50, 0}, {50, 50}, {100, 50}}, ShapeType::POLYLINE, false, 10.0);
    const Path openPath = openLine.toPath();
    CHECK(hasQuadSeg(openPath));

    std::cout << "[geometry] OK\n";
}

// ---------------------------------------------------------------------------
// preprocess 像素核测试（经公共入口 applyOne 逐步执行）：灰度、旋转、翻转、缩放（含纯色等价性）、通道反色 / 分离（含灰度反色）、颜色拾取
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
    core::Image gray = img.toGray();
    CHECK(gray.isGray());
    CHECK(gray.width() == 8 && gray.height() == 8);

    // 旋转 90°：宽高互换
    core::Image rot = preprocess::applyOne(img, preprocess::RotateOp{90});
    CHECK(rot.width() == 8 && rot.height() == 8);
    // 旋转 180° 后回到原尺寸
    core::Image rot180 = preprocess::applyOne(img, preprocess::RotateOp{180});
    CHECK(rot180.width() == 8 && rot180.height() == 8);
    // 旋转 360° 等价于原图
    core::Image rot360 = preprocess::applyOne(img, preprocess::RotateOp{360});
    CHECK(rot360.getPixel(0, 0) == img.getPixel(0, 0));

    // 水平翻转：(0,0) 与 (w-1,0) 交换
    core::Image flipH = preprocess::applyOne(img, preprocess::FlipOp{true});
    CHECK(flipH.getPixel(0, 0) == img.getPixel(7, 0));
    CHECK(flipH.getPixel(7, 0) == img.getPixel(0, 0));

    // 缩放：放大到 16x16
    core::Image scaled = img.resized(16, 16, core::ResampleMode::BILINEAR);
    CHECK(scaled.width() == 16 && scaled.height() == 16);

    // 通道反色：R 通道反色后 (0,0) 处 R 应变为 255 - 0 = 255
    core::Image invR = preprocess::applyOne(img, preprocess::InvertOp{std::string("100")});
    CHECK(invR.getPixel(0, 0).r == 255);
    CHECK(invR.getPixel(0, 0).g == 0);   // G 未反色，仍为原值 0

    // 通道分离：只保留 B 通道，R/G 应为 0
    core::Image onlyB = preprocess::applyOne(img, preprocess::SplitOp{std::string("001")});
    CHECK(onlyB.getPixel(3, 3).r == 0);
    CHECK(onlyB.getPixel(3, 3).g == 0);
    CHECK(onlyB.getPixel(3, 3).b == 128);

    // 颜色拾取：合法坐标返回 optional 有值，越界返回 nullopt
    CHECK(img.colorAt(0, 0).has_value());
    CHECK(!img.colorAt(100, 100).has_value());

    // ---- 灰度图反色（黑白后仍可反色）：对单一亮度通道取 255 - v，格式仍为 GRAY ----
    core::Image gimg(4, 4, core::ImageFormat::GRAY);
    for (int y = 0; y < 4; ++y)
        for (int x = 0; x < 4; ++x)
            gimg.setGray(x, y, static_cast<std::uint8_t>(x * 40 + y * 10)); // 值域 0..150
    const core::Image ginv = preprocess::applyOne(gimg, preprocess::InvertOp{std::string("111")});
    CHECK(ginv.isGray());
    CHECK(ginv.width() == 4 && ginv.height() == 4);
    CHECK(ginv.getGray(0, 0) == 255);                      // 255 - 0
    CHECK(ginv.getGray(3, 3) == 255 - gimg.getGray(3, 3)); // 逐像素取反
    // 灰度反色：除 mask 全 "000"（不反色）外，任意非全 0 mask 都整体反相
    CHECK(preprocess::applyOne(gimg, preprocess::InvertOp{std::string("100")}).getGray(2, 1) == 255 - gimg.getGray(2, 1));
    // mask 全 "000" 与彩色恒等语义对齐：灰度上也不反色（逐像素原值、仍 GRAY）
    CHECK(preprocess::applyOne(gimg, preprocess::InvertOp{std::string("000")}).isGray());
    CHECK(preprocess::applyOne(gimg, preprocess::InvertOp{std::string("000")}).getGray(2, 1) == gimg.getGray(2, 1));
    // 反色为对合：两次反色回到原值
    CHECK(preprocess::applyOne(ginv, preprocess::InvertOp{std::string("111")}).getGray(1, 2) == gimg.getGray(1, 2));

    // ---- 灰度图色道分离无意义：返回副本（像素不变、仍为 GRAY）----
    const core::Image gsplit = preprocess::applyOne(gimg, preprocess::SplitOp{std::string("001")});
    CHECK(gsplit.isGray());
    CHECK(gsplit.getGray(2, 2) == gimg.getGray(2, 2));

    // ---- 彩色反色 "111"：R/G/B 全反、alpha 保留 ----
    core::Image aimg(2, 2, core::ImageFormat::RGBA);
    aimg.fill(core::Color(10, 200, 30, 77));
    const core::Image ainv = preprocess::applyOne(aimg, preprocess::InvertOp{std::string("111")});
    CHECK(ainv.getPixel(0, 0) == core::Color(245, 55, 225, 77)); // alpha=77 不变

    // ---- 彩色分离 "010"：仅留 G，R/B 置 0，alpha 保留 ----
    const core::Image asplit = preprocess::applyOne(aimg, preprocess::SplitOp{std::string("010")});
    CHECK(asplit.getPixel(1, 1) == core::Color(0, 200, 0, 77));

    // ---- resize 等价性：纯色图缩放后仍为同一纯色（NEAREST / BILINEAR 均不应引入偏差；2× 放大→权重为二进制精确值）----
    core::Image solid(6, 4, core::ImageFormat::RGBA);
    solid.fill(core::Color(12, 34, 56, 78));
    const core::Image solidNear = solid.resized(12, 8, core::ResampleMode::NEAREST);
    const core::Image solidBil = solid.resized(12, 8, core::ResampleMode::BILINEAR);
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
    const core::Image quad4 = quad.resized(4, 4, core::ResampleMode::NEAREST);
    CHECK(quad4.getPixel(0, 0) == core::Color(255, 0, 0, 255));
    CHECK(quad4.getPixel(3, 0) == core::Color(0, 255, 0, 255));
    CHECK(quad4.getPixel(0, 3) == core::Color(0, 0, 255, 255));
    CHECK(quad4.getPixel(3, 3) == core::Color(255, 255, 0, 255));

    // ---- resize 灰度：GRAY 输入缩放后仍为 GRAY，纯色保持（单通道路径）----
    core::Image gsolid(4, 4, core::ImageFormat::GRAY);
    for (int y = 0; y < 4; ++y)
        for (int x = 0; x < 4; ++x)
            gsolid.setGray(x, y, 200);
    const core::Image gscaled = gsolid.resized(8, 8, core::ResampleMode::BILINEAR);
    CHECK(gscaled.isGray());
    CHECK(gscaled.getGray(3, 5) == 200);

    // ---- 灰度几何无损：旋转 / 翻转是纯位置置换，不得改变任何灰度值（回归护栏：
    //      旧实现经 getPixel→setPixel 的 BT.601 重压缩会使灰度值每漂移 ±1~2）。----
    core::Image gv(5, 3, core::ImageFormat::GRAY);
    for (int y = 0; y < 3; ++y)
        for (int x = 0; x < 5; ++x)
            gv.setGray(x, y, static_cast<std::uint8_t>(x * 7 + y * 13 + 11)); // 逐像素唯一且非平凡
    // 旋转 90°×4 应回到原图且值无损。
    core::Image grot = preprocess::applyOne(gv, preprocess::RotateOp{90});
    CHECK(grot.isGray() && grot.width() == 3 && grot.height() == 5);
    grot = preprocess::applyOne(grot, preprocess::RotateOp{90});
    grot = preprocess::applyOne(grot, preprocess::RotateOp{90});
    grot = preprocess::applyOne(grot, preprocess::RotateOp{90});
    CHECK(grot.width() == 5 && grot.height() == 3);
    for (int y = 0; y < 3; ++y)
        for (int x = 0; x < 5; ++x)
            CHECK(grot.getGray(x, y) == gv.getGray(x, y));   // 四次 90° = 恒等，值逐像素一致
    // 水平翻转两次 = 恒等（值无损）；并验证单次映射 (x,y)->(w-1-x,y) 搬运的是原灰度值。
    const core::Image gfh = preprocess::applyOne(gv, preprocess::FlipOp{true});
    CHECK(gfh.isGray());
    for (int y = 0; y < 3; ++y)
        for (int x = 0; x < 5; ++x)
            CHECK(gfh.getGray(4 - x, y) == gv.getGray(x, y));
    const core::Image gfh2 = preprocess::applyOne(gfh, preprocess::FlipOp{true});
    for (int y = 0; y < 3; ++y)
        for (int x = 0; x < 5; ++x)
            CHECK(gfh2.getGray(x, y) == gv.getGray(x, y));   // 翻两次无损回原

    std::cout << "[pixel_kernels] OK\n";
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

    // Rasterizer：200x200 白底上画填充矩形，验证填充色与边框色分离（rect 为 (10,10)-(50,40)）
    core::Image canvas(200, 200, core::ImageFormat::RGBA);
    canvas.fill(core::kWhite);
    annotation::PaintStyle style;
    style.color = core::kBlue;         // 边框 / 描边色
    style.fillColor = core::kRed;      // 填充色
    style.strokeWidth = 2;
    style.fill = true;
    annotation::rasterize(canvas, *rect, style);
    // 矩形内部 (30, 25) 应为填充色（红）
    core::Color inside = canvas.getPixel(30, 25);
    CHECK(inside.r > 200 && inside.g < 50 && inside.b < 50);
    // 顶边 (25, 10) 应为边框色（蓝）——描边晚于填充绘制，故边框像素为蓝色
    core::Color edge = canvas.getPixel(25, 10);
    CHECK(edge.b > 200 && edge.r < 50);

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

    // AnnotationLayer：层序调整（moveAnnotation）——列表下标即 z 序（大＝顶，与 hitTest/burnIn 口径一致）。
    // 新建独立层，DRAW 模式依次加红/绿/蓝三个矩形（下标 0/1/2）。
    annotation::AnnotationLayer lay;
    lay.setImage(canvas);
    lay.setEditType(annotation::EditType::DRAW);
    annotation::ShapeRequest mr;
    mr.type = geometry::ShapeType::RECTANGLE;
    lay.changeColor(core::kRed);   mr.p1 = {0, 0};   mr.p2 = {10, 10}; lay.addAnnotation(annotation::buildShape(mr));
    lay.changeColor(core::kGreen); mr.p1 = {20, 20}; mr.p2 = {30, 30}; lay.addAnnotation(annotation::buildShape(mr));
    lay.changeColor(core::kBlue);  mr.p1 = {40, 40}; mr.p2 = {50, 50}; lay.addAnnotation(annotation::buildShape(mr));
    CHECK(lay.annotations().size() == 3);
    CHECK(lay.annotations()[0].color == core::kRed);
    CHECK(lay.annotations()[2].color == core::kBlue);
    // zOrder 一等属性：新标注依次置顶，zOrder 恒等于列表下标（0＝最底、大＝顶）。
    CHECK(lay.annotations()[0].zOrder == 0 && lay.annotations()[1].zOrder == 1 && lay.annotations()[2].zOrder == 2);

    // 选中 index 0（红）→ 移到顶层（to=2）：顺序变 [绿,蓝,红]，选中下标跟随被移动项到 2。
    lay.selectAnnotation(0);
    lay.moveAnnotation(0, 2);
    CHECK(lay.annotations()[0].color == core::kGreen);
    CHECK(lay.annotations()[1].color == core::kBlue);
    CHECK(lay.annotations()[2].color == core::kRed);
    CHECK(lay.selectedAnnotationIndex().has_value() && *lay.selectedAnnotationIndex() == 2);
    // 重排后 zOrder 已重新规范化＝新下标（红从 0 升至 2）。
    CHECK(lay.annotations()[0].zOrder == 0 && lay.annotations()[1].zOrder == 1 && lay.annotations()[2].zOrder == 2);
    CHECK(lay.annotations()[2].color == core::kRed && lay.annotations()[2].zOrder == 2);

    // 移回底层（from=2 → to=0）：恢复 [红,绿,蓝]，选中跟随到 0。
    lay.moveAnnotation(2, 0);
    CHECK(lay.annotations()[0].color == core::kRed);
    CHECK(lay.annotations()[2].color == core::kBlue);
    CHECK(lay.selectedAnnotationIndex().has_value() && *lay.selectedAnnotationIndex() == 0);
    CHECK(lay.annotations()[0].zOrder == 0 && lay.annotations()[1].zOrder == 1 && lay.annotations()[2].zOrder == 2);

    // 区间内其它项的选中下标随移动方向平移：选中 index 1（绿）→ moveAnnotation(0,2) 把红移到底，
    // 结果 [绿,蓝,红]，绿从 1 前移到 0，选中应随之变为 0。
    lay.selectAnnotation(1);
    lay.moveAnnotation(0, 2);
    CHECK(lay.annotations()[0].color == core::kGreen);
    CHECK(lay.selectedAnnotationIndex().has_value() && *lay.selectedAnnotationIndex() == 0);

    // 越界 / 自移动被忽略，列表不变。
    lay.moveAnnotation(99, 0);
    lay.moveAnnotation(1, 1);
    CHECK(lay.annotations().size() == 3);
    CHECK(lay.annotations()[0].zOrder == 0 && lay.annotations()[1].zOrder == 1 && lay.annotations()[2].zOrder == 2);

    // removeAnnotation 后剩余项 zOrder 重新规范化＝新下标（删当前 [绿,蓝,红] 的底（index 0），剩 [蓝,红] → zOrder 0/1）。
    lay.removeAnnotation(0);
    CHECK(lay.annotations().size() == 2);
    CHECK(lay.annotations()[0].color == core::kBlue && lay.annotations()[0].zOrder == 0);
    CHECK(lay.annotations()[1].color == core::kRed && lay.annotations()[1].zOrder == 1);

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

    // 多边形（正方形）：落点为顶点、边长 max(dx,dy) 轴对齐；平移后仍为 SQUARE、逐顶点偏移。
    annotation::ShapeRequest r6;
    r6.type = geometry::ShapeType::SQUARE;
    r6.p1 = {0, 0};
    r6.p2 = {10, 6};   // dx=10、dy=6 → 边长 max = 10
    auto sq = annotation::buildShape(r6);
    const auto [x, y, width, height] = sq->bounds();
    // 轴对齐正方形：包围盒应为 10×10（边长取 max(dx,dy)，不随拖动倾斜）。
    CHECK(std::abs(width - 10.0) < 1e-6 && std::abs(height - 10.0) < 1e-6);
    sq->translate(2, 2);
    const geometry::BoundingBox s1 = sq->bounds();
    CHECK(sq->type() == geometry::ShapeType::SQUARE);
    CHECK(std::abs(s1.x - x - 2.0) < 1e-6 && std::abs(s1.y - y - 2.0) < 1e-6);

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

    // ResizeOp：目标尺寸重采样；configType / typeName / makeDefaultConfig 均覆盖 RESIZE。
    preprocess::PreprocessPipeline pipe2;
    pipe2.add(preprocess::ResizeOp{24, 12});
    const core::Image r2 = pipe2.apply(img);
    CHECK(r2.width() == 24 && r2.height() == 12);
    CHECK(preprocess::configType(preprocess::ResizeOp{}) == preprocess::PreprocessType::RESIZE);
    CHECK(std::string(preprocess::preprocessTypeName(preprocess::PreprocessType::RESIZE)) == "Resize");
    CHECK(std::holds_alternative<preprocess::ResizeOp>(preprocess::makeDefaultConfig(preprocess::PreprocessType::RESIZE)));
    // 非正尺寸 → 无操作（原样返回，不崩溃）。
    CHECK(preprocess::PreprocessPipeline{}.apply(img).width() == 16);
    {
        preprocess::PreprocessPipeline bad;
        bad.add(preprocess::ResizeOp{0, 0});
        CHECK(bad.apply(img).width() == 16 && bad.apply(img).height() == 16);
    }

    // geometryOutput：纯元数据推演尺寸/旋转象限，不搬像素。
    // 链 gray→resize60×40→rotate90（交换 40×60, rotQuad=1）→flip（不变）。
    preprocess::PreprocessPipeline pg;
    pg.add(preprocess::GrayOp{});
    pg.add(preprocess::ResizeOp{60, 40});
    pg.add(preprocess::RotateOp{90});
    pg.add(preprocess::FlipOp{true});
    const auto [width0, height0, rotQuad0] = pg.geometryOutput(1000, 1000);
    CHECK(width0 == 40 && height0 == 60 && rotQuad0 == 1);
    // 三次 rotate 累加：90+90+(-90归一270) → rotQuad=(1+1+3)%4=1；净 90° → 宽高交换。
    preprocess::PreprocessPipeline pg2;
    pg2.add(preprocess::RotateOp{90});
    pg2.add(preprocess::RotateOp{90});
    pg2.add(preprocess::RotateOp{-90});
    const auto [width1, height1, rotQuad1] = pg2.geometryOutput(10, 20);
    CHECK(rotQuad1 == 1 && width1 == 20 && height1 == 10);

    // ---- 折叠/执行融合回归队列：applyExec 与逐步 apply 逐像素等价（声明层 folded() 已删除）----
    // 非方形渐变图：区分 rotate90/270 的宽高互换、flip 的 H/V 轴向，且内容逐像素唯一（镜像/旋转可辨）。
    auto buildFoldImg = [] {
        core::Image im(6, 4, core::ImageFormat::RGBA);
        for (int y = 0; y < 4; ++y)
            for (int x = 0; x < 6; ++x)
                im.setPixel(x, y, core::Color(static_cast<std::uint8_t>(x * 40),
                                              static_cast<std::uint8_t>(y * 60),
                                              static_cast<std::uint8_t>((x + y) * 20), 255));
        return im;
    };
    auto imagesEqual = [](const core::Image& a, const core::Image& b) {
        if (a.width() != b.width() || a.height() != b.height() || a.format() != b.format()) return false;
        for (int y = 0; y < a.height(); ++y)
            for (int x = 0; x < a.width(); ++x)
                if (a.getPixel(x, y) != b.getPixel(x, y)) return false;
        return true;
    };
    const core::Image fbase = buildFoldImg();
    // 正确性口径（folded() 删除后）：applyExec（execPlan + channelMap/remapD4 单趟融合核）与逐步 apply 必逐像素等价（含尺寸/格式）。
    // 旧 folded() 的“趟数不增 / 幂等 / 折叠前后尺寸一致”类断言随声明层入口退役；下方回归队列 + 种子随机性质测试共同保证等价性。
    // 注：rotQuad 不作断言——geometryOutput 忽略 flip 累加，与像素执行序无关，applyExec 不改净几何签名。
    auto checkFold = [&](const preprocess::PreprocessPipeline& p) {
        CHECK(imagesEqual(p.apply(fbase), p.applyExec(fbase)));
    };
    using preprocess::GrayOp;
    using preprocess::InvertOp;
    using preprocess::SplitOp;
    using preprocess::RotateOp;
    using preprocess::FlipOp;
    using preprocess::ResizeOp;
    // 纯旋转累加（3→1）。
    { preprocess::PreprocessPipeline q; q.add(RotateOp{90}); q.add(RotateOp{90}); q.add(RotateOp{90}); checkFold(q); }
    // rotate×flip 非交换（共轭）：flipH 后 rotate90。
    { preprocess::PreprocessPipeline q; q.add(FlipOp{true}); q.add(RotateOp{90}); checkFold(q); }
    // 反序：rotate90 后 flipH（应与上者不同，但各自折叠均须等价）。
    { preprocess::PreprocessPipeline q; q.add(RotateOp{90}); q.add(FlipOp{true}); checkFold(q); }
    // 对角镜像 + 异轴翻转混序（含 flip V）。
    { preprocess::PreprocessPipeline q; q.add(FlipOp{false}); q.add(RotateOp{90}); q.add(FlipOp{true}); checkFold(q); }
    // 同轴翻转对消 + 旋转复位。
    { preprocess::PreprocessPipeline q; q.add(FlipOp{true}); q.add(FlipOp{true}); q.add(RotateOp{180}); q.add(RotateOp{180}); checkFold(q); }
    // 色彩与几何穿插：色彩不跨几何重排，每个几何步被色彩隔成独立长度为1的段 → 不压缩（趟数不变），逐像素仍等价。
    { preprocess::PreprocessPipeline q; q.add(GrayOp{}); q.add(RotateOp{90}); q.add(InvertOp{}); q.add(FlipOp{true}); q.add(SplitOp{"010"}); q.add(InvertOp{}); checkFold(q); }
    // 色彩隔断几何队列 [Rot90, Gray, Rot90]：execPlan 下几何可跨 gray 合并为单次 D4（不同于声明层折叠），逐像素等价仍成立。
    { preprocess::PreprocessPipeline q; q.add(RotateOp{90}); q.add(GrayOp{}); q.add(RotateOp{90}); checkFold(q); }
    // 连续几何压缩 [Gray, Rot90×3, FlipH]：三个 Rot90 + FlipH 归并为单次 D4，逐像素等价。
    { preprocess::PreprocessPipeline q; q.add(GrayOp{}); q.add(RotateOp{90}); q.add(RotateOp{90}); q.add(RotateOp{90}); q.add(FlipOp{true}); checkFold(q); }
    // 单 flip V：归并为单个 D4 元素，逐像素等价。
    { preprocess::PreprocessPipeline q; q.add(FlipOp{false}); checkFold(q); }
    // resize 屏障：resize 两侧色彩/几何不跨块合并，逐像素等价仍成立。
    { preprocess::PreprocessPipeline q; q.add(RotateOp{90}); q.add(GrayOp{}); q.add(ResizeOp{8, 5}); q.add(FlipOp{true}); q.add(FlipOp{true}); q.add(InvertOp{}); q.add(RotateOp{270}); checkFold(q); }

    // ---- 色彩队列回归：applyExec 与 apply 逐像素等价（base 为 RGBA，pre-gray 位映射合并成立）----
    // 双反色对消为恒等。
    { preprocess::PreprocessPipeline q; q.add(InvertOp{}); q.add(InvertOp{}); checkFold(q); }
    // split×2 逐通道与：仅留 B 再仅留 G → 全置零（K=000）。
    { preprocess::PreprocessPipeline q; q.add(SplitOp{"001"}); q.add(SplitOp{"010"}); checkFold(q); }
    // invert+split 复合→单次 channelMap（(v&K)^J），逐像素恒等。
    { preprocess::PreprocessPipeline q; q.add(InvertOp{"111"}); q.add(SplitOp{"001"}); checkFold(q); }
    // 5 步混合色彩链（无 gray）→ pre-gray 位映射合为单次 channelMap。
    { preprocess::PreprocessPipeline q; q.add(InvertOp{"100"}); q.add(SplitOp{"011"}); q.add(InvertOp{"010"}); q.add(SplitOp{"110"}); q.add(InvertOp{"001"}); checkFold(q); }
    // gray 幂等：连续三个 gray → 仅首个生效。
    { preprocess::PreprocessPipeline q; q.add(GrayOp{}); q.add(GrayOp{}); q.add(GrayOp{}); checkFold(q); }
    // 死步：gray 之后的 split 无意义（灰度态规整吞除）。
    { preprocess::PreprocessPipeline q; q.add(GrayOp{}); q.add(SplitOp{"001"}); checkFold(q); }
    // gray 之后 invert 按奇偶：两次抵消 → 仅剩 gray。
    { preprocess::PreprocessPipeline q; q.add(GrayOp{}); q.add(InvertOp{}); q.add(InvertOp{}); checkFold(q); }
    // 灰度态跨几何传播：[Gray, Rot90, Invert"100", Invert"010"] 末段已在灰度态，两个不同 mask 反色均整幅翻→抵消。
    { preprocess::PreprocessPipeline q; q.add(GrayOp{}); q.add(RotateOp{90}); q.add(InvertOp{"100"}); q.add(InvertOp{"010"}); checkFold(q); }
    // 跨 gray 色彩不合并：gray 前后 invert 分属多通道位映射 / 灰度态整幅翻两侧，均保留。
    { preprocess::PreprocessPipeline q; q.add(InvertOp{}); q.add(GrayOp{}); q.add(InvertOp{}); checkFold(q); }

    // ---- applyExec：执行层融合专项（channelMap 四态 / 几何跨 gray 合并 / GRAY 输入 / 种子随机性质）----
    // channelMap 强制255 四态（K=000∧J=111）：单 op 词表不可表达，仅 Split("000")+Invert("111") 两步可达；
    // execPlan 折成单次 channelMap，逐像素仍与两步 apply 一致（alpha 字节保留）。
    {
        preprocess::PreprocessPipeline q;
        q.add(SplitOp{"000"});
        q.add(InvertOp{"111"});
        const core::Image o = q.applyExec(fbase);
        CHECK(imagesEqual(q.apply(fbase), o));
        CHECK(o.getPixel(1, 1) == core::Color(255, 255, 255, 255));   // 三通道强制 255，非取反原值
    }
    // 几何跨 gray 合并：[Rot90, Gray, Rot90] 两段几何在 execPlan 下跨 gray 合成单次 D4 remap（色彩 gray 步照常发射），逐像素与逐步 apply 不变。
    { preprocess::PreprocessPipeline q; q.add(RotateOp{90}); q.add(GrayOp{}); q.add(RotateOp{90}); checkFold(q); }
    // alpha≠255 基图：融合位映射必原样保留 alpha 字节。
    auto buildAlphaImg = [] {
        core::Image im(5, 3, core::ImageFormat::RGBA);
        for (int y = 0; y < 3; ++y)
            for (int x = 0; x < 5; ++x)
                im.setPixel(x, y, core::Color(static_cast<std::uint8_t>(x * 50 + 7),
                                              static_cast<std::uint8_t>(y * 80 + 11),
                                              static_cast<std::uint8_t>((x * y + x + 1) * 13 % 256),
                                              static_cast<std::uint8_t>(30 + x * 40)));
        return im;
    };
    const core::Image ebase = buildAlphaImg();
    auto checkExecOn = [&](const preprocess::PreprocessPipeline& q, const core::Image& base) {
        CHECK(imagesEqual(q.apply(base), q.applyExec(base)));
    };
    // Split+Invert 链在 alpha 基图上等价（验证 channelMap 的 alpha 保留与掩码归一口径）。
    {
        preprocess::PreprocessPipeline q;
        q.add(SplitOp{"101"});
        q.add(InvertOp{"010"});
        q.add(InvertOp{"x"});           // 非 3 长掩码：K 归 "111"、J 归…按既有口径 "111"（与 applyOne 同路）
        q.add(RotateOp{270});
        checkExecOn(q, ebase);
    }
    // GRAY 输入基图：Split 全变死步、Invert 按整幅奇偶、几何/resize 保持灰度——计划按 inputIsGray 初始化灰度态。
    core::Image gbase(6, 4, core::ImageFormat::GRAY);
    for (int y = 0; y < 4; ++y)
        for (int x = 0; x < 6; ++x)
            gbase.setGray(x, y, static_cast<std::uint8_t>((x * 40 + y * 17) % 256));
    {
        preprocess::PreprocessPipeline q;   // 灰度输入 + 死步 Split + 双反色对消 + 几何交错 + 跨块反色
        q.add(SplitOp{"001"});
        q.add(InvertOp{"100"});
        q.add(RotateOp{90});
        q.add(InvertOp{"010"});
        q.add(ResizeOp{7, 4});
        q.add(GrayOp{});
        q.add(InvertOp{"111"});
        q.add(FlipOp{false});
        checkExecOn(q, gbase);
    }
    // 种子随机性质测试：200 条随机队列（六类 op 全参与、掩码含全0/非3长、角度含负值）× 三张基图轮换，
    // 每条断言 applyExec 与逐步 apply 逐像素等价（含尺寸/格式；resize 块界交错路径同时被压到）。
    std::mt19937 rng(20260927);
    const std::array<const core::Image*, 3> bases = {&fbase, &ebase, &gbase};
    auto randMask = [&rng] {
        switch (rng() % 6) {
            case 0: return std::string("000");
            case 1: return std::string("111");
            case 2: return std::string("101");
            case 3: return std::string("010");
            case 4: return std::string("11");   // 非 3 长 → 触发归一口径
            default: {
                std::string m(3, '0');
                for (char& c : m) c = rng() % 2 ? '1' : '0';
                return m;
            }
        }
    };
    for (int t = 0; t < 200; ++t) {
        preprocess::PreprocessPipeline q;
        const int len = 1 + static_cast<int>(rng() % 8);
        for (int i = 0; i < len; ++i) {
            switch (rng() % 6) {
                case 0: q.add(GrayOp{}); break;
                case 1: q.add(SplitOp{randMask()}); break;
                case 2: q.add(InvertOp{randMask()}); break;
                case 3: q.add(RotateOp{(static_cast<int>(rng() % 4) - 1) * 90}); break;   // -90..180
                case 4: q.add(FlipOp{rng() % 2 == 0}); break;
                default: q.add(ResizeOp{1 + static_cast<int>(rng() % 7), 1 + static_cast<int>(rng() % 5)}); break;
            }
        }
        checkExecOn(q, *bases[rng() % 3]);
    }

    std::cout << "[preprocess] OK\n";
}

// ---------------------------------------------------------------------------
// history 能力测试（原 history 模块、现属 core）：泛型撤销 / 重做栈
// ---------------------------------------------------------------------------
static void testHistory() {
    idc::core::HistoryManager<int> h;
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
    testEnginePipeline();  // 引擎流水线功能（SPEC §3）
    testEngineExport();    // 引擎导出落盘（SPEC §4）
    testEngineBoundary();  // 引擎边界异常（SPEC §5 / E-1~E-8）
    testEngineConfigJson(); // 配置 JSON 存取（SPEC §7，含 annotations 段）
    testAnnoParseText();    // 标注 JSON 文本段级解析（SPEC §7，供 CLI 复用）
    testAnnoAnchorRoundTrip(); // 约束图元锚点往返（buildShape ↔ shapeToJson/FromJson 顶点级一致）
    std::cout << "all tests passed.\n";
    return 0;
}
