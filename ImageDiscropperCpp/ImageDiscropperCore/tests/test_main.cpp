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
#include "annotation/annotation_set.h"
#include "annotation/rasterizer.h"
#include "annotation/shape_factory.h"
#include "annotation/view_transform.h"
#include "geometry/shapes.h"
#include "core/history_manager.h"
#include "core/pipeline_manager.h"
#include "preprocess/preprocess_pipeline.h"
// 跨模块直编（unit_tests 已将 GUI annotation_editor.cpp 编入）：恢复标注会话态断言。
#include "model/annotation_editor.h"
// 跨模块直编（unit_tests 已将 GUI preprocess_session.cpp 编入）：预处理会话（零 Qt）的不变式/归一化/缓存断言。
#include "model/preprocess_session.h"

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
// annotation 模块测试：ShapeFactory、Rasterizer、AnnotationSet、ViewTransform
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

    // AnnotationSet：结构操作（add / removeAt / popBack / clear / restore）+ zOrder 不变式 + 命中检测
    // ——会话态（编辑模式 / 属性模板 / 选中 / 撤销重做）已拆至 GUI AnnotationEditor（下方专项断言，
    //   经跨模块直编 annotation_editor.cpp 回归）；本段只验无头侧成立的数据 / 合成原语。
    annotation::AnnotationSet set;
    {
        annotation::Annotation empty;
        CHECK(!set.add(std::move(empty)));                     // 空 shape 拒绝
    }
    {
        annotation::Annotation a;
        a.shapeType = geometry::ShapeType::RECTANGLE;
        a.style.color = core::kBlue;
        a.shape = std::move(rect);
        CHECK(set.add(std::move(a)));
    }
    CHECK(set.annotations().size() == 1);
    CHECK(set.annotations()[0].zOrder == 0);
    CHECK(!set.removeAt(99));                                   // 越界删除忽略
    CHECK(set.removeAt(0));
    CHECK(set.annotations().empty());
    CHECK(!set.popBack());                                      // 空集弹尾 nullopt
    {
        std::vector<annotation::Annotation> items;
        annotation::ShapeRequest rr;
        rr.type = geometry::ShapeType::RECTANGLE;
        for (const core::Color& c : {core::kRed, core::kGreen, core::kBlue}) {
            annotation::Annotation a;
            a.shapeType = geometry::ShapeType::RECTANGLE;
            a.style.color = c;
            rr.p1 = c == core::kRed ? core::Point2D{0, 0} : c == core::kGreen ? core::Point2D{20, 20} : core::Point2D{40, 40};
            rr.p2 = c == core::kRed ? core::Point2D{10, 10} : c == core::kGreen ? core::Point2D{30, 30} : core::Point2D{50, 50};
            a.shape = annotation::buildShape(rr);
            items.push_back(std::move(a));
        }
        set.restore(std::move(items));                          // [红,绿,蓝]，下标即 z 序（大＝顶）
    }
    CHECK(set.annotations().size() == 3);
    CHECK(set.annotations()[0].zOrder == 0 && set.annotations()[1].zOrder == 1 && set.annotations()[2].zOrder == 2);
    // 命中检测：蓝矩形 (40,40)-(50,50) 内 (45,45) 应命中顶层 index 2
    if (const auto hit = set.hitTest({45, 45}); hit.has_value()) {
        CHECK(*hit == 2);
    } else { CHECK(false); }
    CHECK(!set.hitTest({200, 200}).has_value());
    // moveItem：把 index 0（红）移到顶层（to=2）→ [绿,蓝,红]，zOrder 重新规范化＝新下标
    CHECK(set.moveItem(0, 2));
    CHECK(set.annotations()[0].style.color == core::kGreen);
    CHECK(set.annotations()[1].style.color == core::kBlue);
    CHECK(set.annotations()[2].style.color == core::kRed);
    CHECK(set.annotations()[0].zOrder == 0 && set.annotations()[1].zOrder == 1 && set.annotations()[2].zOrder == 2);
    // 移回底层（from=2 → to=0）：恢复 [红,绿,蓝]
    CHECK(set.moveItem(2, 0));
    CHECK(set.annotations()[0].style.color == core::kRed);
    CHECK(set.annotations()[2].style.color == core::kBlue);
    // 越界 / 自移动被忽略，列表不变
    CHECK(!set.moveItem(99, 0));
    CHECK(!set.moveItem(1, 1));
    CHECK(set.annotations().size() == 3);
    // popBack：弹出顶层（蓝）交还调用方（GUI 撤销据此入重做栈）
    if (auto popped = set.popBack(); popped.has_value()) {
        CHECK(popped->style.color == core::kBlue);
    } else { CHECK(false); }
    CHECK(set.annotations().size() == 2);
    // removeAt 后剩余项 zOrder 重新规范化＝新下标（删 [红,绿] 的底 → 剩 [绿] → zOrder 0）
    CHECK(set.removeAt(0));
    CHECK(set.annotations().size() == 1);
    CHECK(set.annotations()[0].style.color == core::kGreen && set.annotations()[0].zOrder == 0);
    set.clear();
    CHECK(set.annotations().empty());

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

    // ---- AnnotationEditor：编辑会话（GUI 半边，零 Qt，跨模块直编）的会话态断言 ----
    //   恢复拆分暂删的会话语义：属性模板 / change* 的 EDIT–DRAW 双分支 / 撤销重做条目级编排 /
    //   选中跟随 / 切模式清选中 / 空 base 合成口径。
    {
        using gui::AnnotationEditor;
        using gui::EditType;
        auto makeRect = [](const double x0, const double y0, const double x1, const double y1) {
            annotation::ShapeRequest r;
            r.type = geometry::ShapeType::RECTANGLE;
            r.p1 = {x0, y0};
            r.p2 = {x1, y1};
            return annotation::buildShape(r);
        };

        AnnotationEditor ed;
        // DRAW 模式：change* 作用当前属性模板；填充色初始与边框色联动，显式改填充后断联动。
        ed.setEditType(EditType::DRAW);
        ed.changeColor(core::kBlue);                      // 联动 → 填充色也变蓝
        CHECK(ed.currentColor() == core::kBlue && ed.currentFillColor() == core::kBlue);
        ed.changeFillColor(core::kRed);                   // 显式改填充 → 断联动
        ed.changeColor(core::kGreen);                     // 断联动后边框色不再带动填充色
        CHECK(ed.currentColor() == core::kGreen && ed.currentFillColor() == core::kRed);
        ed.changeStrokeWidth(0);                          // 线宽下限钳为 1
        CHECK(ed.currentStrokeWidth() == 1);
        ed.changeFill(true);
        CHECK(ed.currentFill());

        // 提交标注走当前模板；撤销弹尾入重做栈、重做回推；撤销后再画新标注清重做栈（切断“未来”）。
        ed.addAnnotation(makeRect(0, 0, 10, 10));
        ed.addAnnotation(makeRect(20, 20, 30, 30));
        CHECK(ed.annotations().size() == 2);
        CHECK(ed.annotations()[0].style.color == core::kGreen && ed.annotations()[0].style.fillColor == core::kRed);
        ed.revoke();
        CHECK(ed.annotations().size() == 1);
        ed.redo();
        CHECK(ed.annotations().size() == 2);
        ed.revoke();                                      // 弹尾（20,20）入重做 → 剩 1
        ed.addAnnotation(makeRect(40, 40, 50, 50));       // 新操作清重做栈
        ed.redo();                                        // 重做栈已清 → 无变化
        CHECK(ed.annotations().size() == 2);

        // EDIT 模式：change* 作用选中项、不污染属性模板；切模式清选中。
        ed.setEditType(EditType::EDIT);                   // 切模式 → 清选中
        CHECK(!ed.selectedAnnotationIndex().has_value());
        ed.selectAnnotation(0);
        ed.changeColor(core::kBlack);                     // 改选中项颜色
        CHECK(ed.annotations()[0].style.color == core::kBlack);
        CHECK(ed.currentColor() == core::kGreen);         // 模板未被选中项编辑污染

        // hitTest 委托集合：命中顶层填充矩形 (40,40)-(50,50) → index 1；EDIT 改其线宽。
        if (const auto hit = ed.hitTest({45, 45}); hit.has_value()) {
            ed.selectAnnotation(hit);
        } else { CHECK(false); }
        ed.changeStrokeWidth(8);
        CHECK(ed.selectedAnnotation() != nullptr && ed.selectedAnnotation()->style.strokeWidth == 8);

        // removeAnnotation：删选中项 → 取消选中；属新操作清重做。
        const std::size_t sel = ed.selectedAnnotationIndex().value();
        ed.removeAnnotation(sel);
        CHECK(!ed.selectedAnnotationIndex().has_value());
        CHECK(ed.annotations().size() == 1);

        // moveAnnotation：选中下标跟随被移动项。
        ed.addAnnotation(makeRect(0, 0, 10, 10));         // 现 2 项
        ed.selectAnnotation(1);                           // 选顶层
        ed.moveAnnotation(1, 0);                          // 顶 → 底，选中跟随到 0
        CHECK(ed.selectedAnnotationIndex().has_value() && *ed.selectedAnnotationIndex() == 0);

        // restoreAnnotations：整体替换 + 清重做栈 + 取消选中。
        std::vector<annotation::Annotation> restoreItems;
        {
            annotation::Annotation a;
            a.shapeType = geometry::ShapeType::RECTANGLE;
            a.shape = makeRect(60, 60, 70, 70);
            restoreItems.push_back(std::move(a));
        }
        ed.setEditType(EditType::DRAW);
        ed.selectAnnotation(0);
        ed.restoreAnnotations(std::move(restoreItems));
        CHECK(ed.annotations().size() == 1);
        CHECK(!ed.selectedAnnotationIndex().has_value());

        // burnIn(base)：空 base 退化为 1×1 透明图（与集合口径一致）。
        const core::Image burned = ed.burnIn(core::Image{});
        CHECK(burned.width() == 1 && burned.height() == 1);
    }

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
    CHECK(pipe.items()[1].index() == 4);
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
// PreprocessSession 测试（GUI 会话半边，零 Qt，跨模块直编）：geo→color 不变式 / 移动限幅 /
//   载入归一化 / 有效管线 / geomSig / producesGray / base_ 缓存失效——先于换线验证与控制器 1:1。
// ---------------------------------------------------------------------------
static void testPreprocessSession() {
    using namespace idc;
    using gui::PreprocessSession;
    using gui::ResizeTarget;

    // ---- geo→color 不变式追加：几何插几何块尾、色彩落队尾 ----
    preprocess::PreprocessPipeline p;
    PreprocessSession::appendKeepingInvariant(p, preprocess::RotateOp{90});   // geo → [R]
    PreprocessSession::appendKeepingInvariant(p, preprocess::GrayOp{});        // color → [R,G]
    PreprocessSession::appendKeepingInvariant(p, preprocess::FlipOp{true});    // geo 插几何块尾 → [R,F,G]
    PreprocessSession::appendKeepingInvariant(p, preprocess::InvertOp{});      // color 队尾 → [R,F,G,I]
    CHECK(p.size() == 4);
    CHECK(PreprocessSession::geoBlockLen(p) == 2);                            // 前二为几何块
    CHECK(preprocess::isGeometryStep(p.items()[0]) && preprocess::isGeometryStep(p.items()[1]));
    CHECK(!preprocess::isGeometryStep(p.items()[2]) && !preprocess::isGeometryStep(p.items()[3]));

    // ---- 块内换序限幅：同类可移、跨块拒绝、边界与越界守卫 ----
    CHECK(PreprocessSession::moveWithinBlock(p, 0, true) == true);            // R↔F（同几何块）→ [F,R,G,I]
    CHECK(std::holds_alternative<preprocess::FlipOp>(p.items()[0])
              && std::holds_alternative<preprocess::RotateOp>(p.items()[1]));
    CHECK(PreprocessSession::moveWithinBlock(p, 1, true) == false);           // R(geo) 与 G(color) 跨块 → 不动
    CHECK(PreprocessSession::moveWithinBlock(p, 0, false) == false);          // 首位上移越界
    CHECK(PreprocessSession::moveWithinBlock(p, 3, true) == false);           // 末位下移越界
    CHECK(PreprocessSession::moveWithinBlock(p, 9, true) == false);           // 下标越界

    // ---- 载入归一化：散落 ResizeOp 抽出取最后一个正尺寸、geo 前 color 后、绝不入轻队列 ----
    preprocess::PreprocessPipeline raw;
    raw.add(preprocess::GrayOp{});           // color
    raw.add(preprocess::ResizeOp{100, 50});  // resize1
    raw.add(preprocess::RotateOp{90});       // geo
    raw.add(preprocess::ResizeOp{0, 0});     // 非正 → 丢弃
    raw.add(preprocess::FlipOp{true});       // geo
    raw.add(preprocess::ResizeOp{200, 120}); // resize2（最后一个正尺寸）
    raw.add(preprocess::SplitOp{"010"});     // color
    const auto [steps, resize] = PreprocessSession::normalizeLoaded(raw);
    CHECK(resize.has_value() && resize->width == 200 && resize->height == 120);
    CHECK(steps.size() == 4);           // 任何 ResizeOp 都不入轻队列
    for (const auto& cfg : steps.items())
        CHECK(!std::holds_alternative<preprocess::ResizeOp>(cfg));
    // geo 段 [Rotate,Flip] 在前、color 段 [Gray,Split] 在后（各保相对序）。
    CHECK(std::holds_alternative<preprocess::RotateOp>(steps.items()[0])
              && std::holds_alternative<preprocess::FlipOp>(steps.items()[1])
              && std::holds_alternative<preprocess::GrayOp>(steps.items()[2])
              && std::holds_alternative<preprocess::SplitOp>(steps.items()[3]));

    // ---- 有效管线 = [resize?]+steps（resize 恒置首）与 geomSig 纯元数据推演 ----
    preprocess::PreprocessPipeline st;
    st.add(preprocess::RotateOp{90});
    constexpr std::optional rz = ResizeTarget{60, 40};
    const auto eff = PreprocessSession::makeEffective(rz, st);
    CHECK(eff.size() == 2 && std::holds_alternative<preprocess::ResizeOp>(eff.items()[0]));
    PreprocessSession sess;
    sess.setResize(ResizeTarget{60, 40});
    sess.setSteps(st);                       // [Resize60×40, Rotate90]
    const auto [gw, gh, gq] = sess.geomSig(1000, 1000);   // resize 后 rotate90 → 交换宽高、象限 1
    CHECK(gw == 40 && gh == 60 && gq == 1);

    // ---- producesGray：任一 GrayOp 使输出恒灰度 ----
    PreprocessSession gs;
    CHECK(!gs.producesGray());
    preprocess::PreprocessPipeline withGray; withGray.add(preprocess::GrayOp{});
    gs.setSteps(withGray);
    CHECK(gs.producesGray());

    // ---- base_ 缓存失效（baseDirtyFor：无前缀相等 → 脏）----
    preprocess::PreprocessPipeline pref1;
    pref1.add(preprocess::ResizeOp{10, 10});
    preprocess::PreprocessPipeline pref2;
    pref2.add(preprocess::ResizeOp{20, 20});
    PreprocessSession bs;
    CHECK(bs.baseDirtyFor(pref1));                          // 无缓存 → 脏
    bs.setBase(core::Image(1, 1, core::ImageFormat::RGBA), pref1);
    CHECK(!bs.baseDirtyFor(pref1));                         // 同前缀 → 复用
    CHECK(bs.baseDirtyFor(pref2));                          // 异前缀 → 脏
    bs.clearBase();
    CHECK(bs.baseDirtyFor(pref1));                          // 释放后 → 脏

    // ---- clear：会话失效（换图/重置复用），清 steps_/resize_/base_ ----
    PreprocessSession c;
    c.setResize(ResizeTarget{5, 5});
    preprocess::PreprocessPipeline cSteps; cSteps.add(preprocess::GrayOp{});
    c.setSteps(cSteps);
    c.setBase(core::Image(1, 1, core::ImageFormat::RGBA), preprocess::PreprocessPipeline{});
    CHECK(c.hasPreprocess() && c.hasBase());
    c.clear();
    CHECK(!c.hasPreprocess() && !c.hasBase() && !c.hasResize());

    std::cout << "[preprocess_session] OK\n";
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
// pipeline 能力测试：泛型有序队列容器 core::Pipeline<T> 的增/删/移/改 + 越界钳制/忽略边界策略
//（PreprocessPipeline 的组合骨架；这些策略正是其「非空壳」的核心价值，故直接锁定）
// ---------------------------------------------------------------------------
static void testPipeline() {
    idc::core::Pipeline<int> p;
    CHECK(p.empty() && p.empty());

    // add 逐字追加。
    p.add(10); p.add(20); p.add(30);
    CHECK(p.size() == 3 && !p.empty());
    CHECK(p.items() == (std::vector{10, 20, 30}));

    // insert：中间位置正常插入；index >= size 钳制为追加队尾。
    p.insert(1, 15);                                   // → [10,15,20,30]
    CHECK(p.items() == (std::vector{10, 15, 20, 30}));
    p.insert(99, 99);                                  // index 越界 → 追加
    CHECK(p.items() == (std::vector{10, 15, 20, 30, 99}));

    // remove：合法删除；越界忽略（不动不抛）。
    p.remove(4);                                       // → [10,15,20,30]
    CHECK(p.size() == 4);
    p.remove(100);                                     // 越界忽略
    CHECK(p.size() == 4);

    // replaceAt：就地替换；越界忽略。
    p.replaceAt(0, 11);
    CHECK(p.items().front() == 11);
    p.replaceAt(100, 0);                               // 越界忽略
    CHECK(p.size() == 4);

    // moveUp：首位/越界忽略；正常与上一项换序。
    p.moveUp(0);                                       // 首位忽略 → 不变
    CHECK(p.items() == (std::vector{11, 15, 20, 30}));
    p.moveUp(2);                                       // 20 上移 → [11,20,15,30]
    CHECK(p.items() == (std::vector{11, 20, 15, 30}));

    // moveDown：末位/越界忽略；正常与下一项换序。
    p.moveDown(3);                                     // 末位忽略
    CHECK(p.items() == (std::vector{11, 20, 15, 30}));
    p.moveDown(0);                                     // 11 下移 → [20,11,15,30]
    CHECK(p.items() == (std::vector{20, 11, 15, 30}));

    // operator==：逐条相等比较。
    idc::core::Pipeline<int> q;
    q.add(20); q.add(11); q.add(15); q.add(30);
    CHECK(p == q);
    q.moveUp(1);
    CHECK(!(p == q));

    // 范围 for 直接迭代（begin/end 转发到底层 vector）。
    int sum = 0;
    for (const int v : p) sum += v;
    CHECK(sum == 20 + 11 + 15 + 30);

    // clear。
    p.clear();
    CHECK(p.empty());

    std::cout << "[pipeline] OK\n";
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
    testPreprocessSession();  // 预处理会话（零 Qt，跨模块直编）不变式/归一化/baseDirty/签名断言
    testHistory();
    testPipeline();
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
