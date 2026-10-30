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
#include <filesystem>
#include <iostream>
#include <random>
#include <string>

#include "test_harness.h"

#include "core/color.h"
#include "core/image.h"
#include "core/point.h"
#include "core/composite.h"   // core::compositeOver（source-over 共享真相源，canvas 测试直接验证）
#include "engine/engine.h"
#include "annotation/annotation_set.h"
#include "annotation/rasterizer.h"
#include "annotation/shape_factory.h"
#include "annotation/view_transform.h"
#include "geometry/shapes.h"
#include "core/history_manager.h"
#include "core/pipeline.h"
#include "preprocess/preprocess_pipeline.h"
#include "generate/pattern.h"
#include "canvas/layer.h"   // 多层画布：ImageLayer / composeCanvas / materializeLayer
#include "engine/engine_config_json.h"   // saveEngineConfig / loadEngineConfig（配置资产字段往返）
#include "io/image_io.h"   // readImageFile / writeImageFile（资产解码 / 打包测试）
// 跨模块直编（unit_tests 已将 GUI annotation_editor.cpp 编入）：恢复标注会话态断言。
#include "model/annotation_editor.h"
#include "model/config_assets.h"   // GUI 配置资产助手（零 Qt，跨模块直编回归）：查找链 / 解码注入 / 打包
#include "canvas/drag_gesture.h"   // 平移拖拽手势（零 Qt，跨模块直编回归；见 canvas/pick_cycle.h 同纪律）
#include "canvas/pick_cycle.h"     // 同点连击循环选层（零 Qt，跨模块直编回归）
// 跨模块直编（unit_tests 已将 GUI preprocess_session.cpp 编入）：预处理会话（零 Qt）的不变式/归一化/缓存断言。
#include "model/preprocess_session.h"

// ---------------------------------------------------------------------------
// core 模块测试：图像读写、格式转换
// ---------------------------------------------------------------------------
static void testCore() {
    using namespace idc::core;

    // 图像读写与格式转换
    Image img(4, 3, ImageFormat::Rgba);
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

    // 矩形（PolygonShape：四点闭合多边形）包围盒 / 内部判定 / 控制点
    const PolygonShape rect({{10, 20}, {40, 20}, {40, 60}, {10, 60}}, ShapeType::Rectangle, true);
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

    // 文本宽度估算：按字符数（非字节数）——ASCII 半宽、多字节字符全宽，中文按全宽计（不逐字节高估）。
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
            if (type == PathSegmentType::QuadTo) return true;
        }
        return false;
    };
    const PolygonShape rrect({{0, 0}, {100, 0}, {100, 100}, {0, 100}}, ShapeType::Rectangle, true, 20.0);
    const Path rrectPath = rrect.toPath();
    CHECK(hasQuadSeg(rrectPath));
    const auto [rx, ry, rw, rh] = rrect.bounds();
    CHECK(rx == 0 && ry == 0 && rw == 100 && rh == 100);
    CHECK(rrect.cornerRadius() == 20.0);

    // setter 往返 + 负值钳为 0；clone 保留圆角半径。
    PolygonShape mut({{0, 0}, {10, 0}, {10, 10}}, ShapeType::IsosTriangle, true);
    mut.setCornerRadius(5.0);
    CHECK(mut.cornerRadius() == 5.0);
    mut.setCornerRadius(-3.0);
    CHECK(mut.cornerRadius() == 0.0);
    mut.setCornerRadius(7.0);
    CHECK(mut.clone()->cornerRadius() == 7.0);

    // 开放折线：内部顶点倒角（含 QuadTo）；由 PolygonShape 直接表达。
    const PolygonShape openLine({{0, 0}, {50, 0}, {50, 50}, {100, 50}}, ShapeType::Polyline, false, 10.0);
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
    core::Image img(8, 8, core::ImageFormat::Rgba);
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
    core::Image scaled = img.resized(16, 16, core::ResampleMode::Bilinear);
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
    core::Image gimg(4, 4, core::ImageFormat::Gray);
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
    core::Image aimg(2, 2, core::ImageFormat::Rgba);
    aimg.fill(core::Color(10, 200, 30, 77));
    const core::Image ainv = preprocess::applyOne(aimg, preprocess::InvertOp{std::string("111")});
    CHECK(ainv.getPixel(0, 0) == core::Color(245, 55, 225, 77)); // alpha=77 不变

    // ---- 彩色分离 "010"：仅留 G，R/B 置 0，alpha 保留 ----
    const core::Image asplit = preprocess::applyOne(aimg, preprocess::SplitOp{std::string("010")});
    CHECK(asplit.getPixel(1, 1) == core::Color(0, 200, 0, 77));

    // ---- resize 等价性：纯色图缩放后仍为同一纯色（Nearest / Bilinear 均不应引入偏差；2× 放大→权重为二进制精确值）----
    core::Image solid(6, 4, core::ImageFormat::Rgba);
    solid.fill(core::Color(12, 34, 56, 78));
    const core::Image solidNear = solid.resized(12, 8, core::ResampleMode::Nearest);
    const core::Image solidBil = solid.resized(12, 8, core::ResampleMode::Bilinear);
    CHECK(solidNear.width() == 12 && solidNear.height() == 8);
    CHECK(solidNear.getPixel(0, 0) == core::Color(12, 34, 56, 78));
    CHECK(solidNear.getPixel(11, 7) == core::Color(12, 34, 56, 78));
    CHECK(solidBil.getPixel(0, 0) == core::Color(12, 34, 56, 78));
    CHECK(solidBil.getPixel(5, 3) == core::Color(12, 34, 56, 78));   // 内部点双线性仍为纯色
    CHECK(solidBil.getPixel(11, 7) == core::Color(12, 34, 56, 78));

    // ---- resize 最近邻：2x2 放大到 4x4，四象限精确复制（验证采样坐标无偏移）----
    core::Image quad(2, 2, core::ImageFormat::Rgb);
    quad.setPixel(0, 0, core::Color(255, 0, 0, 255));   // 左上 红
    quad.setPixel(1, 0, core::Color(0, 255, 0, 255));   // 右上 绿
    quad.setPixel(0, 1, core::Color(0, 0, 255, 255));   // 左下 蓝
    quad.setPixel(1, 1, core::Color(255, 255, 0, 255)); // 右下 黄
    const core::Image quad4 = quad.resized(4, 4, core::ResampleMode::Nearest);
    CHECK(quad4.getPixel(0, 0) == core::Color(255, 0, 0, 255));
    CHECK(quad4.getPixel(3, 0) == core::Color(0, 255, 0, 255));
    CHECK(quad4.getPixel(0, 3) == core::Color(0, 0, 255, 255));
    CHECK(quad4.getPixel(3, 3) == core::Color(255, 255, 0, 255));

    // ---- resize 灰度：GRAY 输入缩放后仍为 GRAY，纯色保持（单通道路径）----
    core::Image gsolid(4, 4, core::ImageFormat::Gray);
    for (int y = 0; y < 4; ++y)
        for (int x = 0; x < 4; ++x)
            gsolid.setGray(x, y, 200);
    const core::Image gscaled = gsolid.resized(8, 8, core::ResampleMode::Bilinear);
    CHECK(gscaled.isGray());
    CHECK(gscaled.getGray(3, 5) == 200);

    // ---- 灰度几何无损：旋转 / 翻转是纯位置置换，不得改变任何灰度值（回归护栏：
    //      几何若经 getPixel→setPixel 的 BT.601 重压缩会使灰度值漂移 ±1~2）。----
    core::Image gv(5, 3, core::ImageFormat::Gray);
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
    req.type = geometry::ShapeType::Rectangle;
    auto rect = annotation::buildShape(req);
    CHECK(rect != nullptr);
    CHECK(rect->type() == geometry::ShapeType::Rectangle);

    // ShapeFactory：圆
    req.type = geometry::ShapeType::Circle;
    req.p1 = {100, 100};
    req.p2 = {120, 100}; // 半径 20
    auto circle = annotation::buildShape(req);
    CHECK(circle != nullptr);
    CHECK(circle->type() == geometry::ShapeType::Circle);

    // Rasterizer：200x200 白底上画填充矩形，验证填充色与边框色分离（rect 为 (10,10)-(50,40)）
    core::Image canvas(200, 200, core::ImageFormat::Rgba);
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
    // ——会话态（编辑模式 / 属性模板 / 选中 / 撤销重做）位于 GUI AnnotationEditor（下方专项断言，
    //   经跨模块直编 annotation_editor.cpp 回归）；本段只验无头侧成立的数据 / 合成原语。
    annotation::AnnotationSet set;
    {
        annotation::Annotation empty;
        CHECK(!set.add(std::move(empty)));                     // 空 shape 拒绝
    }
    {
        annotation::Annotation a;
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
        rr.type = geometry::ShapeType::Rectangle;
        for (const core::Color& c : {core::kRed, core::kGreen, core::kBlue}) {
            annotation::Annotation a;
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

    // Shape::translate：就地平移保留具体类型（不退化为通用 Path）。
    // 矩形：平移后 type() 仍为 Rectangle，包围盒按 (dx,dy) 偏移、宽高不变。
    annotation::ShapeRequest r4;
    r4.p1 = {10, 10};
    r4.p2 = {30, 20};
    r4.type = geometry::ShapeType::Rectangle;
    auto movable = annotation::buildShape(r4);
    const auto [x0, y0, width0, height0] = movable->bounds();
    movable->translate(5, 7);
    const auto [x1, y1, width1, height1] = movable->bounds();
    CHECK(movable->type() == geometry::ShapeType::Rectangle);            // 类型未退化
    CHECK(std::abs(x1 - x0 - 5.0) < 1e-6);
    CHECK(std::abs(y1 - y0 - 7.0) < 1e-6);
    CHECK(std::abs(width1 - width0) < 1e-6 && std::abs(height1 - height0) < 1e-6);

    // 文字：平移后仍为 Text（保留字形渲染所需的类型身份），基线锚点随之偏移。
    annotation::ShapeRequest r5;
    r5.type = geometry::ShapeType::Text;
    r5.p1 = {40, 40};
    r5.p2 = {40, 40};
    r5.text = "hi";
    r5.fontSize = 16;
    auto txt = annotation::buildShape(r5);
    const geometry::BoundingBox t0 = txt->bounds();
    txt->translate(-3, 4);
    const geometry::BoundingBox t1 = txt->bounds();
    CHECK(txt->type() == geometry::ShapeType::Text);                     // 未退化为 Path
    CHECK(std::abs(t1.x - t0.x + 3.0) < 1e-6);
    CHECK(std::abs(t1.y - t0.y - 4.0) < 1e-6);

    // 多边形（正方形）：落点为顶点、边长 max(dx,dy) 轴对齐；平移后仍为 Square、逐顶点偏移。
    annotation::ShapeRequest r6;
    r6.type = geometry::ShapeType::Square;
    r6.p1 = {0, 0};
    r6.p2 = {10, 6};   // dx=10、dy=6 → 边长 max = 10
    auto sq = annotation::buildShape(r6);
    const auto [x, y, width, height] = sq->bounds();
    // 轴对齐正方形：包围盒应为 10×10（边长取 max(dx,dy)，不随拖动倾斜）。
    CHECK(std::abs(width - 10.0) < 1e-6 && std::abs(height - 10.0) < 1e-6);
    sq->translate(2, 2);
    const geometry::BoundingBox s1 = sq->bounds();
    CHECK(sq->type() == geometry::ShapeType::Square);
    CHECK(std::abs(s1.x - x - 2.0) < 1e-6 && std::abs(s1.y - y - 2.0) < 1e-6);

    // ViewTransform：屏幕 ↔ 逻辑坐标
    annotation::ViewTransform vt;
    vt.fitToViewport(100, 100, 200, 200);
    CHECK(std::abs(vt.scale() - 2.0) < 0.01);
    core::Point2D logical = vt.screenToLogical({100, 100});
    CHECK(std::abs(logical.x - 50.0) < 0.5);
    CHECK(std::abs(logical.y - 50.0) < 0.5);

    // ---- AnnotationEditor：编辑会话（GUI 半边，零 Qt，跨模块直编）的会话态断言 ----
    //   覆盖会话语义：属性模板 / change* 的 EDIT–DRAW 双分支 / 撤销重做条目级编排 /
    //   选中跟随 / 切模式清选中 / 空 base 合成口径。
    {
        using gui::AnnotationEditor;
        using gui::EditType;
        auto makeRect = [](const double x0, const double y0, const double x1, const double y1) {
            annotation::ShapeRequest r;
            r.type = geometry::ShapeType::Rectangle;
            r.p1 = {x0, y0};
            r.p2 = {x1, y1};
            return annotation::buildShape(r);
        };

        AnnotationEditor ed;
        // Draw 模式：change* 作用当前属性模板；填充色初始与边框色联动，显式改填充后断联动。
        ed.setEditType(EditType::Draw);
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

        // Edit 模式：change* 作用选中项、不污染属性模板；切模式清选中。
        ed.setEditType(EditType::Edit);                   // 切模式 → 清选中
        CHECK(!ed.selectedAnnotationIndex().has_value());
        ed.selectAnnotation(0);
        ed.changeColor(core::kBlack);                     // 改选中项颜色
        CHECK(ed.annotations()[0].style.color == core::kBlack);
        CHECK(ed.currentColor() == core::kGreen);         // 模板未被选中项编辑污染

        // hitTest 委托集合：命中顶层填充矩形 (40,40)-(50,50) → index 1；Edit 改其线宽。
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
            a.shape = makeRect(60, 60, 70, 70);
            restoreItems.push_back(std::move(a));
        }
        ed.setEditType(EditType::Draw);
        ed.selectAnnotation(0);
        ed.restoreAnnotations(std::move(restoreItems));
        CHECK(ed.annotations().size() == 1);
        CHECK(!ed.selectedAnnotationIndex().has_value());

        // burnIn(base)：空 base 退化为 1×1 透明图（与集合口径一致）。
        const core::Image burned = ed.burnIn(core::Image{});
        CHECK(burned.width() == 1 && burned.height() == 1);
    }

    // ---- AnnotationEditor：复制选中标注（错位偏移 + 追加顶层 + 选中复制件 + 清重做栈）----
    {
        using gui::AnnotationEditor;
        auto makeRect = [](const double x0, const double y0, const double x1, const double y1) {
            annotation::ShapeRequest r;
            r.type = geometry::ShapeType::Rectangle;
            r.p1 = {x0, y0};
            r.p2 = {x1, y1};
            return annotation::buildShape(r);
        };

        AnnotationEditor ed;
        ed.addAnnotation(makeRect(0, 0, 10, 10));
        ed.addAnnotation(makeRect(20, 20, 30, 30));
        ed.selectAnnotation(0);
        ed.revoke();                        // 弹尾 (20,20) 入重做栈 → 剩 1 项（revoke 清选中）
        CHECK(ed.annotations().size() == 1);
        ed.selectAnnotation(0);             // 重新选中余下的原件

        // 复制：追加到末尾＝顶层，选中复制件；属新操作清重做栈。
        const auto copyIdx = ed.copySelectedAnnotation();
        CHECK(copyIdx.has_value() && *copyIdx == 1);
        CHECK(ed.annotations().size() == 2);
        CHECK(ed.selectedAnnotationIndex().has_value() && *ed.selectedAnnotationIndex() == 1);

        // 复制件与原件样式同、几何错位（原件 (0,0)-(10,10)，复制件 (16,16)-(26,26)）。
        CHECK(ed.annotations()[1].style.color == ed.annotations()[0].style.color);
        if (const auto hit = ed.hitTest({5, 5}); hit.has_value()) CHECK(*hit == 0);      // 原件仍可命中
        else CHECK(false);
        if (const auto hit = ed.hitTest({21, 21}); hit.has_value()) CHECK(*hit == 1);    // 复制件命中且为顶层
        else CHECK(false);

        // 重做栈已被复制清空：redo 无变化。
        ed.redo();
        CHECK(ed.annotations().size() == 2);

        // 无选中复制 → 无操作。
        ed.selectAnnotation(std::nullopt);
        CHECK(!ed.copySelectedAnnotation().has_value());
        CHECK(ed.annotations().size() == 2);
    }

    // ---- AnnotationEditor：重叠连击循环（pickAt / cycleAt / hitTestAll / closedForFill）回归 ----
    {
        using gui::AnnotationEditor;
        auto makeRect = [](const double x0, const double y0, const double x1, const double y1, const bool) {
            annotation::ShapeRequest r;
            r.type = geometry::ShapeType::Rectangle;
            r.p1 = {x0, y0};
            r.p2 = {x1, y1};
            auto s = annotation::buildShape(r);
            return s;
        };

        AnnotationEditor ed;
        std::vector<annotation::Annotation> items;
        for (int i = 0; i < 3; ++i) {   // 三矩形同区域叠放：zOrder 0/1/2
            annotation::Annotation a;
            a.shape = makeRect(100, 100, 200, 200, true);
            a.style.fill = true;
            items.push_back(std::move(a));
        }
        ed.restoreAnnotations(std::move(items));
        const core::Point2D p{150, 150};

        // hitTestAll：自顶向下（2=最顶）返回全部命中；hitTest 返回最顶。
        //   （hitTestAll 在 AnnotationSet 上，编辑器经同构集合验证顺序）
        const auto all = [&] {
            annotation::AnnotationSet probe;
            std::vector<annotation::Annotation> copy;
            for (const auto& a : ed.annotations()) copy.push_back(a);
            probe.restore(std::move(copy));
            return probe.hitTestAll(p);
        }();
        CHECK(all.size() == 3);
        CHECK(all[0] == 2 && all[1] == 1 && all[2] == 0);
        CHECK(ed.hitTest(p).has_value() && *ed.hitTest(p) == 2);

        // pickAt：未选中 → 选最顶；选中项在命中栈内 → 保持（拖动跟随选中项）。
        CHECK(*ed.pickAt(p) == 2);
        ed.selectAnnotation(1);
        CHECK(*ed.pickAt(p) == 1);
        ed.selectAnnotation(std::nullopt);

        // 一次点击 = pickAt + cycleAt：每次只推进一位；自顶向下循环并回绕。
        //   cycleAt 的同点容差由调用方传入（GUI 侧按缩放从屏幕基准折算），此处取固定 5.0 验行为。
        constexpr double pickTol = 5.0;
        auto clickOnce = [&](const core::Point2D& pt) {
            ed.selectAnnotation(ed.pickAt(pt));
            ed.selectAnnotation(ed.cycleAt(pt, pickTol));
            return ed.selectedAnnotationIndex();
        };
        CHECK(*clickOnce(p) == 2);   // 首击选最顶
        CHECK(*clickOnce(p) == 1);   // 同点续击下探一位
        CHECK(*clickOnce(p) == 0);
        CHECK(*clickOnce(p) == 2);   // 到底回顶

        // 换点 → 游标重置为最顶。
        const core::Point2D q{110, 110};
        CHECK(*clickOnce(q) == 2);

        // closedForFill：开放折线补闭合边后 contains 覆盖靠闭合边一侧区域（左瓣）。
        geometry::Path bow;
        bow.moveTo(0, 0);
        bow.lineTo(100, 0);
        bow.lineTo(0, 100);
        bow.lineTo(100, 100);        // 未闭合
        const geometry::Path closed = bow.closedForFill();
        CHECK(!bow.contains(50, 50));             // 开路径偶奇溢区不含交叉点附近
        (void)closed;
        CHECK(closed.contains(50, 75));           // 闭合后下瓣内部可判内
        CHECK(closed.contains(50, 25));           // 上瓣内部可判内
    }

    std::cout << "[annotation] OK\n";
}

// ---------------------------------------------------------------------------
// 平移拖拽手势（零 Qt 值类型，跨模块直编）测试：点击/拖动判定与位移累积
// ---------------------------------------------------------------------------
static void testDragGesture() {
    using namespace idc;
    gui::DragGesture g;

    // 纯点击（起手后直接 end）：wasClick==true、零位移；end 复位后可再次起手势。
    g.start(10.0, 20.0);
    {
        const auto [wasClick, dx, dy] = g.end();
        CHECK(wasClick);
        CHECK(dx == 0.0 && dy == 0.0);
        CHECK(!g.active());
    }

    // 拖动：moved 由「收到过非零位移」判定（与缩放/设备像素阈值无关），净位移正确累计。
    g.start(0.0, 0.0);
    g.moveTo(3.0, 0.0);
    g.moveTo(5.0, -2.0);
    {
        const auto [wasClick, dx, dy] = g.end();
        CHECK(!wasClick);
        CHECK(dx == 5.0 && dy == -2.0);
    }

    // 折返归零：发生位移但净位移为零 → 仍判拖动（wasClick==false，不推进连击循环）、零提交量。
    g.start(100.0, 100.0);
    g.moveTo(105.0, 102.0);
    g.moveTo(100.0, 100.0);
    {
        const auto [wasClick, dx, dy] = g.end();
        CHECK(!wasClick);
        CHECK(dx == 0.0 && dy == 0.0);
    }

    // 未起手势时 moveTo 忽略（安全护栏，与图元层 if (!active) return 语义一致）。
    g.moveTo(999.0, 999.0);
    {
        const auto [wasClick, dx, dy] = g.end();
        CHECK(wasClick);   // 空手势 end 视为原地点击（不触发任何提交路径）
        CHECK(dx == 0.0 && dy == 0.0);
    }

    std::cout << "[drag_gesture] OK\n";
}

// ---------------------------------------------------------------------------
// 同点连击循环选层（零 Qt 值类型，跨模块直编）测试：按下解析纯查询 / 点击推进 / 复位
//   （含按下已路由目标的 advanceFromTarget：外部选中的下层目标不弹回栈顶，及游标位重合的不可区分局限）
// ---------------------------------------------------------------------------
static void testPickCycle() {
    using namespace idc;
    using Cycle = gui::PickCycle<int>;
    constexpr double tol = 5.0;

    // resolveOnPress（纯查询）：当前选中仍在栈内 → 保持（拖动跟随选中项）；不在 → 栈顶；空栈 → nullopt。
    {
        const std::vector stack{3, 2, 1};
        CHECK(Cycle::resolveOnPress(stack, std::optional{2}) == std::optional{2});
        CHECK(Cycle::resolveOnPress(stack, std::optional{9}) == std::optional{3});
        CHECK(Cycle::resolveOnPress(stack, std::nullopt) == std::optional{3});
        CHECK(!Cycle::resolveOnPress({}, std::optional{1}).has_value());
    }

    // advanceOnClick：首次点击复位到栈顶（不推进）；同点同栈续击下探一位，到底回顶。
    {
        Cycle c;
        const std::vector stack{3, 2, 1};
        CHECK(c.advanceOnClick(10, 10, stack, tol) == std::optional{3});   // 首击：栈顶
        CHECK(c.advanceOnClick(10, 10, stack, tol) == std::optional{2});
        CHECK(c.advanceOnClick(10, 10, stack, tol) == std::optional{1});
        CHECK(c.advanceOnClick(10, 10, stack, tol) == std::optional{3});   // 到底回顶
    }

    // 换点 → 复位栈顶：容差内视作同点（距(0,0)=5 恰在边界），超出即换点。
    {
        Cycle c;
        const std::vector stack{3, 2, 1};
        CHECK(c.advanceOnClick(0, 0, stack, tol) == std::optional{3});
        CHECK(c.advanceOnClick(3, 4, stack, tol) == std::optional{2});   // 容差边界内：续击
        CHECK(c.advanceOnClick(30, 30, stack, tol) == std::optional{3}); // 换点：复位栈顶
    }

    // 命中栈变化（与位置无关）→ 复位栈顶。
    {
        Cycle c;
        const std::vector a{3, 2, 1};
        const std::vector b{9, 8};
        CHECK(c.advanceOnClick(10, 10, a, tol) == std::optional{3});
        CHECK(c.advanceOnClick(10, 10, a, tol) == std::optional{2});
        CHECK(c.advanceOnClick(10, 10, b, tol) == std::optional{9});
    }

    // 空栈 → 清循环态返回 nullopt；其后同点同栈从栈顶重新起算（不沿旧游标续推）。
    {
        Cycle c;
        const std::vector stack{3, 2, 1};
        CHECK(c.advanceOnClick(10, 10, stack, tol) == std::optional{3});
        CHECK(c.advanceOnClick(10, 10, stack, tol) == std::optional{2});
        CHECK(!c.advanceOnClick(10, 10, {}, tol).has_value());
        CHECK(c.advanceOnClick(10, 10, stack, tol) == std::optional{3});
    }

    // reset（拖动提交后清历史）：同点同栈也回到栈顶，不续推陈旧游标。
    {
        Cycle c;
        const std::vector stack{3, 2, 1};
        CHECK(c.advanceOnClick(10, 10, stack, tol) == std::optional{3});
        CHECK(c.advanceOnClick(10, 10, stack, tol) == std::optional{2});
        c.reset();
        CHECK(c.advanceOnClick(10, 10, stack, tol) == std::optional{3});
    }

    // advanceFromTarget：同点同栈且游标已指向目标 → 下探一位（连击链，到底回顶）。
    {
        Cycle c;
        const std::vector stack{3, 2, 1};
        CHECK(c.advanceFromTarget(3, 10, 10, stack, tol) == std::optional{3});   // 首击取栈顶：只定位
        CHECK(c.advanceFromTarget(3, 10, 10, stack, tol) == std::optional{2});   // 续击：下探
        CHECK(c.advanceFromTarget(2, 10, 10, stack, tol) == std::optional{1});
        CHECK(c.advanceFromTarget(1, 10, 10, stack, tol) == std::optional{3});   // 到底回顶
    }

    // advanceFromTarget：目标来自外部选中（不在连击链上，游标未指向它）→ 本次点击只对齐游标、
    // 不弹回栈顶（否则列表选中的下层框会被单击换选到最顶那个）；其后同点续击从该层往下探。
    {
        Cycle c;
        const std::vector stack{3, 2, 1};
        CHECK(c.advanceFromTarget(2, 10, 10, stack, tol) == std::optional{2});   // 列表选中 2：保持
        CHECK(c.advanceFromTarget(2, 10, 10, stack, tol) == std::optional{1});   // 续击下探
    }

    // advanceFromTarget 的已知局限：判据只看状态（同点同栈且游标已指向 target），不看 target 的来历——
    // 外部选中的目标恰落在游标位时与「连击链继续」不可区分，本击仍按下探处理。
    {
        Cycle c;
        const std::vector stack{3, 2, 1};
        CHECK(c.advanceFromTarget(3, 10, 10, stack, tol) == std::optional{3});   // 首击无历史：只定位栈顶
        // 目标仍是 3（等同于刚从列表外部选中）：状态与连击链续击完全同形 → 仍下探到 2。
        CHECK(c.advanceFromTarget(3, 10, 10, stack, tol) == std::optional{2});
    }

    // advanceFromTarget：目标不在栈内（栈已变）→ 对齐栈顶；空栈 → 清态返回 nullopt。
    {
        Cycle c;
        const std::vector stack{3, 2, 1};
        CHECK(c.advanceFromTarget(9, 10, 10, stack, tol) == std::optional{3});
        CHECK(!c.advanceFromTarget(3, 10, 10, {}, tol).has_value());
        CHECK(c.advanceFromTarget(2, 10, 10, stack, tol) == std::optional{2});   // 清态后重新定位
    }

    std::cout << "[pick_cycle] OK\n";
}

// ---------------------------------------------------------------------------
// GUI 配置资产助手（零 Qt，跨模块直编）测试：越界判定 / 查找链 / 解码注入 / 打包 / assetsDir 往返
// ---------------------------------------------------------------------------
static void testConfigAssets() {
    using namespace idc;
    namespace fs = std::filesystem;
    std::error_code ec;

    // isSafeLayerAsset：相对名安全判定（与 CLI 同规）。
    CHECK(gui::isSafeLayerAsset("a.png"));
    CHECK(gui::isSafeLayerAsset("sub/a.png"));
    CHECK(!gui::isSafeLayerAsset(""));
    CHECK(!gui::isSafeLayerAsset("../a.png"));
    CHECK(!gui::isSafeLayerAsset("sub/../../a.png"));

    // 临时目录骨架：dir/cfg.json；dir/sub/a.png（配置目录层）；dir/cfg.assets/sub/b.png（约定层）；
    //   dir/custom/c.png（assetsDir 声明层）；dir/override/d.png（显式来源层）。
    const fs::path dir = fs::temp_directory_path() / "idc_cfgassets_test";
    fs::remove_all(dir, ec);
    fs::create_directories(dir / "sub", ec);
    fs::create_directories(dir / "cfg.assets" / "sub", ec);
    fs::create_directories(dir / "custom", ec);
    fs::create_directories(dir / "override", ec);
    core::Image one(2, 2, core::ImageFormat::Rgba);
    one.fill(core::kWhite);
    CHECK(io::writeImageFile((dir / "sub" / "a.png").string(), one, engine::ExportFormat::Png, 90, core::kTransparent).has_value());
    CHECK(io::writeImageFile((dir / "cfg.assets" / "sub" / "b.png").string(), one, engine::ExportFormat::Png, 90, core::kTransparent).has_value());
    CHECK(io::writeImageFile((dir / "custom" / "c.png").string(), one, engine::ExportFormat::Png, 90, core::kTransparent).has_value());
    CHECK(io::writeImageFile((dir / "override" / "d.png").string(), one, engine::ExportFormat::Png, 90, core::kTransparent).has_value());
    const std::string cfgPath = (dir / "cfg.json").string();

    // 查找链：约定 .assets → 配置目录；声明目录（config.assetsDir）优于约定；显式来源最优先。
    CHECK(gui::resolveLayerAsset(cfgPath, "", "", "sub/b.png") == dir / "cfg.assets" / "sub" / "b.png");
    CHECK(gui::resolveLayerAsset(cfgPath, "", "", "sub/a.png") == dir / "sub" / "a.png");
    CHECK(gui::resolveLayerAsset(cfgPath, "custom", "", "c.png") == dir / "custom" / "c.png");
    CHECK(gui::resolveLayerAsset(cfgPath, "", (dir / "override").string(), "d.png") == dir / "override" / "d.png");
    CHECK(gui::resolveLayerAsset(cfgPath, "", "", "nope.png").empty());

    // decodeFileLayers：file 层按链解码注入（含子目录相对名）；缺文件计数、保持空像素；纯图案层（无图片元素）不动。
    {
        engine::EngineConfig cfg;
        std::vector<canvas::ImageLayer> layers(3);
        layers[0].fileName = "sub/a.png";
        layers[1].fileName = "missing.png";
        layers[2].backdrop = generate::PatternParams{};
        cfg.layers = std::move(layers);
        int missing = 0;
        std::string err;
        CHECK(gui::decodeFileLayers(cfg, cfgPath, "", missing, err));
        CHECK(missing == 1);
        CHECK(!(*cfg.layers)[0].decodedPixels.empty());
        CHECK((*cfg.layers)[1].decodedPixels.empty());
        CHECK((*cfg.layers)[2].backdrop.has_value());
    }

    // decodeFileLayers：越界名 → 拒绝（false + err 非空）。
    {
        engine::EngineConfig cfg;
        std::vector<canvas::ImageLayer> layers(1);
        layers[0].fileName = "../evil.png";
        cfg.layers = std::move(layers);
        int missing = 0;
        std::string err;
        CHECK(!gui::decodeFileLayers(cfg, cfgPath, "", missing, err));
        CHECK(!err.empty());
    }

    // bundleFileLayers：像素写盘进目标目录（保留相对子路径）；缺像素 / 越界名 → 拒绝。
    {
        engine::EngineConfig cfg;
        std::vector<canvas::ImageLayer> layers(2);
        layers[0].fileName = "sub/a.png";
        layers[0].decodedPixels = one;
        layers[1].fileName = "empty.png";   // 缺像素：不静默。
        cfg.layers = std::move(layers);
        std::string err;
        CHECK(!gui::bundleFileLayers(cfg, dir / "out", err));

        cfg.layers = std::vector<canvas::ImageLayer>(1);
        (*cfg.layers)[0].fileName = "sub/a.png";
        (*cfg.layers)[0].decodedPixels = one;
        CHECK(gui::bundleFileLayers(cfg, dir / "out", err));
        CHECK(fs::exists(dir / "out" / "sub" / "a.png", ec));
    }

    // assetsDir 字段往返：保存 → 重载保留（§7 键序不碍字段值）。
    {
        engine::EngineConfig cfg;
        cfg.source = engine::SourceInfo{4, 4};
        cfg.assetsDir = "custom";
        const std::string f = (dir / "round.json").string();
        CHECK(engine::saveEngineConfig(f, cfg).has_value());
        engine::EngineConfig back;
        CHECK(engine::loadEngineConfig(f, back).has_value());
        CHECK(back.assetsDir == "custom");
    }

    fs::remove_all(dir, ec);
    std::cout << "[config-assets] OK\n";
}

// ---------------------------------------------------------------------------
// preprocess 模块测试：流水线按顺序执行
// ---------------------------------------------------------------------------
static void testPreprocess() {
    using namespace idc;

    core::Image img(16, 16, core::ImageFormat::Rgba);
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
    CHECK(preprocess::configType(preprocess::GrayOp{}) == preprocess::PreprocessType::Gray);
    CHECK(std::string(preprocess::preprocessTypeName(preprocess::PreprocessType::Flip)) == "Flip");

    // ResizeOp：目标尺寸重采样；configType / typeName / makeDefaultConfig 均覆盖 RESIZE。
    preprocess::PreprocessPipeline pipe2;
    pipe2.add(preprocess::ResizeOp{24, 12});
    const core::Image r2 = pipe2.apply(img);
    CHECK(r2.width() == 24 && r2.height() == 12);
    CHECK(preprocess::configType(preprocess::ResizeOp{}) == preprocess::PreprocessType::Resize);
    CHECK(std::string(preprocess::preprocessTypeName(preprocess::PreprocessType::Resize)) == "Resize");
    CHECK(std::holds_alternative<preprocess::ResizeOp>(preprocess::makeDefaultConfig(preprocess::PreprocessType::Resize)));
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

    // ---- mapRectGeometry / mapDeltaToInput：画布坐标系↔工作图坐标系的矩形/增量几何映射 ----
    // 口径与逐像素几何一致（连续坐标）：供 GUI 把 layerPlacement 落位框映到底图坐标系、拖动增量逆映回画布。
    {
        // 空管线 / 仅色彩步 → 恒等直通。
        preprocess::PreprocessPipeline iden;
        const auto r0 = iden.mapRectGeometry(2, 3, 4, 5, 10, 6);
        CHECK(r0.x == 2 && r0.y == 3 && r0.width == 4 && r0.height == 5);
        iden.add(preprocess::GrayOp{});
        iden.add(preprocess::InvertOp{});
        const auto [x, y, width, height] = iden.mapRectGeometry(2, 3, 4, 5, 10, 6);
        CHECK(x == 2 && y == 3 && width == 4 && height == 5);
        double idx0 = 7.0, idy0 = -2.0;
        iden.mapDeltaToInput(7, -2, 10, 6, idx0, idy0);
        CHECK(idx0 == 7 && idy0 == -2);
    }
    {
        // rotate90CW（输入 10×6）：(px,py)->(curH-py, px)，矩形 (2,1,4,3) 角点后包围盒：
        //   x0=6-4=2, y0=2, x1=6-1=5, y1=6 → (2,2,3,4)。
        preprocess::PreprocessPipeline q; q.add(preprocess::RotateOp{90});
        const auto [x, y, width, height] = q.mapRectGeometry(2, 1, 4, 3, 10, 6);
        CHECK(x == 2 && y == 2 && width == 3 && height == 4);
        // 拖动增量逆映射：正向 (dx,dy)->(-dy,dx)，逆为 (dy,-dx)。
        double ox = 0, oy = 0;
        q.mapDeltaToInput(3, 5, 10, 6, ox, oy);
        CHECK(ox == 5 && oy == -3);
    }
    {
        // rotate180（10×6）：矩形 (2,1,4,3) → x0=10-6=4, y0=6-4=2 → (4,2,4,3)；增量自逆取反。
        preprocess::PreprocessPipeline q; q.add(preprocess::RotateOp{180});
        const auto [x, y, width, height] = q.mapRectGeometry(2, 1, 4, 3, 10, 6);
        CHECK(x == 4 && y == 2 && width == 4 && height == 3);
        double ox = 0, oy = 0;
        q.mapDeltaToInput(3, 5, 10, 6, ox, oy);
        CHECK(ox == -3 && oy == -5);
    }
    {
        // rotate270CW（10×6）：(px,py)->(py, curW-px)，包围盒 (1,4,3,4)；正向增量 (dy,-dx) → 逆 (-dy,dx)。
        preprocess::PreprocessPipeline q; q.add(preprocess::RotateOp{270});
        const auto [x, y, width, height] = q.mapRectGeometry(2, 1, 4, 3, 10, 6);
        CHECK(x == 1 && y == 4 && width == 3 && height == 4);
        double ox = 0, oy = 0;
        q.mapDeltaToInput(3, 5, 10, 6, ox, oy);
        CHECK(ox == -5 && oy == 3);
    }
    {
        // flipH（10×6）：矩形 (2,1,4,3) → x0=10-6=4 → (4,1,4,3)；dx 取反、dy 不变。
        preprocess::PreprocessPipeline q; q.add(preprocess::FlipOp{true});
        const auto r = q.mapRectGeometry(2, 1, 4, 3, 10, 6);
        CHECK(r.x == 4 && r.y == 1 && r.width == 4 && r.height == 3);
        double ox = 0, oy = 0;
        q.mapDeltaToInput(3, 5, 10, 6, ox, oy);
        CHECK(ox == -3 && oy == 5);
        // flipV：y0=6-4=2 → (2,2,4,3)；dy 取反。
        preprocess::PreprocessPipeline qv; qv.add(preprocess::FlipOp{false});
        const auto [x, y, width, height] = qv.mapRectGeometry(2, 1, 4, 3, 10, 6);
        CHECK(x == 2 && y == 2 && width == 4 && height == 3);
        qv.mapDeltaToInput(3, 5, 10, 6, ox, oy);
        CHECK(ox == 3 && oy == -5);
    }
    {
        // resize（10×6 → 20×12，因子 2）：矩形与增量同比例正向；逆映射取倒数。
        preprocess::PreprocessPipeline q; q.add(preprocess::ResizeOp{20, 12});
        const auto r = q.mapRectGeometry(2, 1, 4, 3, 10, 6);
        CHECK(r.x == 4 && r.y == 2 && r.width == 8 && r.height == 6);
        double ox = 0, oy = 0;
        q.mapDeltaToInput(6, 8, 10, 6, ox, oy);
        CHECK(ox == 3 && oy == 4);
        // 非正尺寸 → 空操作（直通）。
        preprocess::PreprocessPipeline qb; qb.add(preprocess::ResizeOp{0, 0});
        const auto [x, y, width, height] = qb.mapRectGeometry(2, 1, 4, 3, 10, 6);
        CHECK(x == 2 && y == 1 && width == 4 && height == 3);
    }
    {
        // 复合链（resize 2× → rot90）逐步与整体一致；矩形映后的中心与逐像素正映射同口径。
        // resize → (4,2,8,6) @ 20×12；rot90（curH=12）：nx0=12-8=4, ny0=4, nx1=12-2=10, ny1=12 → (4,4,6,8)。
        preprocess::PreprocessPipeline q2;
        q2.add(preprocess::ResizeOp{20, 12});
        q2.add(preprocess::RotateOp{90});
        const auto [x, y, width, height] = q2.mapRectGeometry(2, 1, 4, 3, 10, 6);
        CHECK(x == 4 && y == 4 && width == 6 && height == 8);
        // 增量：正向 resize×2 再 rot90 (dx,dy)->(-2dy,2dx)；逆：先 rot90⁻¹ (dy,-dx) 再 resize⁻¹ ÷2。
        double ox = 0, oy = 0;
        q2.mapDeltaToInput(4, 8, 10, 6, ox, oy);
        CHECK(ox == 4 && oy == -2);   // (8/2, -4/2) = (4,-2)
    }
    {
        // 正/逆一致性：[Rot90, FlipH] 的增量正映为 (dx,dy)->(dy,dx)（自逆），
        //   故 mapDeltaToInput 连施两次应还原原增量。
        preprocess::PreprocessPipeline q;
        q.add(preprocess::RotateOp{90});
        q.add(preprocess::FlipOp{true});
        double cx = 0, cy = 0;
        q.mapDeltaToInput(5, -2, 10, 6, cx, cy);        // 第一次逆映 → (-2,5)
        double bx = 0, by = 0;
        q.mapDeltaToInput(cx, cy, 10, 6, bx, by);       // 第二次逆映（自逆）→ 还原 (5,-2)
        CHECK(bx == 5 && by == -2);                    // 往返还原原增量
    }

    // ---- 执行融合回归队列：applyExec 与逐步 apply 逐像素等价 ----
    // 非方形渐变图：区分 rotate90/270 的宽高互换、flip 的 H/V 轴向，且内容逐像素唯一（镜像/旋转可辨）。
    auto buildFoldImg = [] {
        core::Image im(6, 4, core::ImageFormat::Rgba);
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
    // 正确性口径：applyExec（execPlan + channelMap/remapD4 单趟融合核）与逐步 apply 必逐像素等价（含尺寸/格式）。
    // 下方回归队列 + 种子随机性质测试共同保证等价性。
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

    // 注：不透明度归图层合成期 ImageTransform.opacity 承载（非预处理步），
    //     相关断言见 testCanvas。

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
        core::Image im(5, 3, core::ImageFormat::Rgba);
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
    core::Image gbase(6, 4, core::ImageFormat::Gray);
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
    // 种子随机性质测试：200 条随机队列（六类 op 全参与、掩码含全0/非3长、角度含负值），
    // 每条断言 applyExec 与逐步 apply 逐像素等价（含尺寸/格式；resize 块界交错、gray 相邻序均被压到）。
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
    bs.setBase(core::Image(1, 1, core::ImageFormat::Rgba), pref1);
    CHECK(!bs.baseDirtyFor(pref1));                         // 同前缀 → 复用
    CHECK(bs.baseDirtyFor(pref2));                          // 异前缀 → 脏
    bs.clearBase();
    CHECK(bs.baseDirtyFor(pref1));                          // 释放后 → 脏

    // ---- clear：会话失效（换图/重置复用），清 steps_/resize_/base_ ----
    PreprocessSession c;
    c.setResize(ResizeTarget{5, 5});
    preprocess::PreprocessPipeline cSteps; cSteps.add(preprocess::GrayOp{});
    c.setSteps(cSteps);
    c.setBase(core::Image(1, 1, core::ImageFormat::Rgba), preprocess::PreprocessPipeline{});
    CHECK(c.hasPreprocess() && c.hasBase());
    c.clear();
    CHECK(!c.hasPreprocess() && !c.hasBase() && !c.hasResize());

    std::cout << "[preprocess_session] OK\n";
}

// ---------------------------------------------------------------------------
// history 能力测试（core::HistoryManager<T>）：泛型撤销 / 重做栈
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
    CHECK(p != q);

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
// engine 模块测试：Grid-Selection-Export 引擎的数据结构骨架（不含算法逻辑）
// ---------------------------------------------------------------------------
static void testEngine() {
    using namespace idc::engine;

    // RectRegion：左闭右开的宽高、面积与包含判定
    constexpr RectRegion r(10, 20, 30, 50);
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
    sel.setPolarity(Polarity::Remove);
    CHECK(sel.polarity() == Polarity::Remove);

    // Sequence：自定义序列可直接设置
    Sequence seq;
    seq.setCustom({3, 1, 2, 0});
    CHECK(seq.size() == 4 && seq.order().front() == 3);

    // EngineConfig：三层模式共用的配置聚合可正常构造与赋值
    EngineConfig cfg;
    cfg.source = SourceInfo{1920, 1080};
    cfg.cut.tier = Tier::Erase;
    cfg.cut.generator = CutGenerator::Rect;
    cfg.cut.rect = RectRegion(300, 200, 900, 700);
    cfg.cut.polarity = Polarity::Remove;
    cfg.exportParams.mode = ExportMode::Merged;
    cfg.exportParams.layout = MergeLayout::Collapse;
    CHECK(cfg.source.width == 1920 && cfg.cut.tier == Tier::Erase);
    CHECK(cfg.cut.polarity == Polarity::Remove);

    std::cout << "[engine] OK\n";
}

// ---------------------------------------------------------------------------
// generate 模块测试：程序化图案生成（新建图像）——尺寸/透明/非法/各类型关键像素
// ---------------------------------------------------------------------------
static void testGenerate() {
    using namespace idc;
    using namespace idc::generate;

    // 非法尺寸：<=0 或超上限 → 空图
    CHECK(makePattern(0, 10, PatternParams{}).empty());
    CHECK(makePattern(-1, 10, PatternParams{}).empty());
    CHECK(makePattern(generate::kMaxDimension + 1, 10, PatternParams{}).empty());

    // 总像素封顶（≤kMaxTotalPixels）：单边均合法但宽×高超限 → 空图（分配前短路，不 OOM）。
    CHECK(makePattern(generate::kMaxDimension, generate::kMaxDimension, PatternParams{}).empty()); // 65535² ≫ 64M
    CHECK(makePattern(8001, 8000, PatternParams{}).empty());   // 64,008,000 > 64,000,000
    CHECK(!makePattern(4000, 4000, PatternParams{}).empty());  // 16M ≪ 64M，正常产出

    // Solid：整张填底色，alpha 直写
    PatternParams solid;
    solid.kind = PatternKind::Solid;
    solid.background = core::Color(10, 20, 30, 128);
    const core::Image s = makePattern(4, 3, solid);
    CHECK(!s.empty() && s.width() == 4 && s.height() == 3 && s.format() == core::ImageFormat::Rgba);
    CHECK(s.getPixel(0, 0) == core::Color(10, 20, 30, 128));
    CHECK(s.getPixel(3, 2) == core::Color(10, 20, 30, 128));

    // 透明底（alpha=0）：像素 alpha 恒为 0
    PatternParams trans = solid;
    trans.background = core::kTransparent;
    const core::Image t = makePattern(2, 2, trans);
    CHECK(t.getPixel(1, 1).a == 0);

    // 线性渐变（水平）：起点=bg、终点=fg，中点插值介于两者
    PatternParams lg;
    lg.kind = PatternKind::LinearGradient;
    lg.orientation = Orientation::Horizontal;
    lg.background = core::Color(0, 0, 0, 255);
    lg.foreground = core::Color(200, 0, 0, 255);
    const core::Image l = makePattern(5, 1, lg);
    CHECK(l.getPixel(0, 0).r == 0);
    CHECK(l.getPixel(4, 0).r == 200);
    CHECK(l.getPixel(2, 0).r == 100);   // 中点线性插值

    // 径向渐变：中心=bg、四角=fg
    PatternParams rg;
    rg.kind = PatternKind::RadialGradient;
    rg.background = core::Color(0, 0, 0, 255);
    rg.foreground = core::Color(0, 0, 255, 255);
    const core::Image r = makePattern(3, 3, rg);
    CHECK(r.getPixel(1, 1).b == 0);          // 中心＝bg
    CHECK(r.getPixel(0, 0).b == 255);        // 角落＝fg

    // 棋盘格：(0,0) 为背景、(period,0) 为前景（奇偶交替）
    PatternParams cb;
    cb.kind = PatternKind::Checkerboard;
    cb.background = core::kWhite;
    cb.foreground = core::kBlack;
    cb.period = 2;
    const core::Image c = makePattern(4, 4, cb);
    CHECK(c.getPixel(0, 0) == core::kWhite);
    CHECK(c.getPixel(2, 0) == core::kBlack);
    CHECK(c.getPixel(0, 2) == core::kBlack);

    // 网格线：格点 (0,0) 为前景线、区内 (5,5) 为背景
    PatternParams gl;
    gl.kind = PatternKind::GridLines;
    gl.background = core::kWhite;
    gl.foreground = core::kRed;
    gl.period = 10;
    gl.lineWidth = 1;
    const core::Image g = makePattern(20, 20, gl);
    CHECK(g.getPixel(0, 0) == core::kRed);
    CHECK(g.getPixel(5, 5) == core::kWhite);

    // 参数钳位：period<=0 钳到 2，仍可正常生成非空图
    PatternParams clamp;
    clamp.kind = PatternKind::Checkerboard;
    clamp.period = 0;
    CHECK(!makePattern(4, 4, clamp).empty());

    // 斜纹即条纹的 DiagonalDown 取向（↘ 沿 x−y）：主对角 x==y 恒 v=0 → 偶带=背景；(2,0) v=2 → 奇带=前景
    PatternParams ds;
    ds.kind = PatternKind::Stripes;
    ds.orientation = Orientation::DiagonalDown;
    ds.background = core::kWhite;
    ds.foreground = core::kBlack;
    ds.period = 2;
    const core::Image d = makePattern(4, 4, ds);
    CHECK(d.getPixel(0, 0) == core::kWhite && d.getPixel(1, 1) == core::kWhite && d.getPixel(3, 3) == core::kWhite);
    CHECK(d.getPixel(2, 0) == core::kBlack);

    // 同心方框即同心环的 Vertical 取向（orientation=Vertical → 切比雪夫方环）：中心在环上=前景；(2,0) 距中心 2、周期 4 → 环间=背景
    PatternParams rr;
    rr.kind = PatternKind::Concentric;
    rr.orientation = Orientation::Vertical;
    rr.background = core::kWhite;
    rr.foreground = core::kBlack;
    rr.period = 4;
    rr.lineWidth = 1;
    const core::Image rrImg = makePattern(5, 5, rr);
    CHECK(rrImg.getPixel(2, 2) == core::kBlack);   // 中心方环
    CHECK(rrImg.getPixel(2, 0) == core::kWhite);    // 环间空白

    // 色相渐变：coord=0 → H=0 → 纯红、不透明
    PatternParams hg;
    hg.kind = PatternKind::HueGradient;
    hg.orientation = Orientation::Horizontal;
    hg.period = 100;
    const core::Image h = makePattern(4, 1, hg);
    CHECK(h.getPixel(0, 0).r == 255 && h.getPixel(0, 0).g == 0 && h.getPixel(0, 0).b == 0 && h.getPixel(0, 0).a == 255);

    // 噪声：同 seed+同尺寸恒同图（逐像素一致）；异 seed → 至少一处不同。全彩不透明。
    PatternParams nz;
    nz.kind = PatternKind::Noise;
    nz.noiseMode = NoiseMode::FullColor;
    nz.seed = 1234;
    const core::Image n1 = makePattern(16, 16, nz);
    const core::Image n2 = makePattern(16, 16, nz);           // 同参数
    CHECK(!n1.empty() && n1.getPixel(0, 0).a == 255);
    bool same = true;
    for (int y = 0; y < 16 && same; ++y)
        for (int x = 0; x < 16; ++x) { if (n1.getPixel(x, y) != n2.getPixel(x, y)) { same = false; break; } }
    CHECK(same);                                              // 确定性：恒同图
    nz.seed = 5678;                                          // 换种子
    const core::Image n3 = makePattern(16, 16, nz);
    bool anyDiff = false;
    for (int y = 0; y < 16 && !anyDiff; ++y)
        for (int x = 0; x < 16; ++x) { if (n1.getPixel(x, y) != n3.getPixel(x, y)) { anyDiff = true; break; } }
    CHECK(anyDiff);                                          // 异种子应产异图

    // 灰度噪声：R==G==B
    PatternParams ng;
    ng.kind = PatternKind::Noise;
    ng.noiseMode = NoiseMode::Grayscale;
    ng.seed = 42;
    const core::Image gray = makePattern(8, 8, ng);
    const core::Color gp = gray.getPixel(3, 3);
    CHECK(gp.r == gp.g && gp.g == gp.b && gp.a == 255);

    // 图片不是程序化图案类型：落位统一走 composeCanvas
    //   装配序（见 testCanvas 的图片层缩放/锚点落位/越界裁剪用例）。

    std::cout << "[generate] OK\n";
}

// ---------------------------------------------------------------------------
// canvas 模块测试：composeCanvas（z 序 / source-over / 越界裁剪 / 锚点+枢轴+自由落位 /
//   配方与文件层 / 逐层预处理）与层不透明度 transform.opacity（合成期 alpha 乘子 / 灰度层亦生效）。
// ---------------------------------------------------------------------------
static void testCanvas() {
    using namespace idc;
    using namespace idc::canvas;

    auto solid = [](const int w, const int h, const core::Color& c) {
        core::Image im(w, h, core::ImageFormat::Rgba);
        im.fill(c);
        return im;
    };
    constexpr core::Color red{255, 0, 0, 255};
    constexpr core::Color blue{0, 0, 255, 255};
    constexpr core::Color green{0, 255, 0, 255};

    // ---- z 序：顶层不透明覆盖底层（B 2×2 居中盖住 A 4×4 的中央）----
    {
        std::vector<ImageLayer> st;
        ImageLayer a; a.decodedPixels = solid(4, 4, red); a.zOrder = 0; a.id = 1;
        ImageLayer b; b.decodedPixels = solid(2, 2, blue); b.zOrder = 1; b.id = 2;
        st = {b, a};   // 故意乱序存入，验证合成按 zOrder 而非内存序
        const core::Image out = composeCanvas(st, 4, 4);
        CHECK(out.width() == 4 && out.height() == 4);
        CHECK(out.getPixel(0, 0) == red);          // 角落露出底层
        CHECK(out.getPixel(1, 1) == blue);         // 2×2 居中偏移 (1,1) 起，被顶层覆盖
        CHECK(out.getPixel(2, 2) == blue);
    }

    // ---- 图片图层整体显隐由调用方处理：传空 layers → 仅底图（透明）；传全层 → 顶层覆盖中央 ----
    {
        ImageLayer a; a.decodedPixels = solid(4, 4, red); a.zOrder = 0; a.id = 1;
        ImageLayer b; b.decodedPixels = solid(2, 2, blue); b.zOrder = 1; b.id = 2;
        const std::vector layers{a, b};
        const core::Image hidden = composeCanvas({}, 4, 4);            // GUI 隐藏态传空 → 仅透明底
        CHECK(hidden.getPixel(1, 1).a == 0);
        const core::Image shown = composeCanvas(layers, 4, 4);         // 可见态传全层 → 顶层 blue 覆盖中央
        CHECK(shown.getPixel(1, 1) == blue);
        CHECK(layerPlacement(b, 4, 4).has_value());    // layerPlacement 不读显隐（整体门控归 GUI）
    }

    // ---- source-over：半透明顶层（a=128）与不透明红底混合 → r≈127、结果不透明 ----
    {
        constexpr core::Color semi{0, 0, 0, 128};
        const core::Color composited = core::compositeOver(red, semi);
        CHECK(composited.r == 127 && composited.g == 0 && composited.b == 0 && composited.a == 255);
        std::vector<ImageLayer> st;
        ImageLayer a; a.decodedPixels = solid(4, 4, red); a.zOrder = 0;
        ImageLayer b; b.decodedPixels = solid(4, 4, semi); b.zOrder = 1;
        st = {a, b};
        const core::Image out = composeCanvas(st, 4, 4);
        CHECK(out.getPixel(0, 0) == core::Color(127, 0, 0, 255));
    }

    // ---- 越界裁剪：图层整体落在画布外（自由偏移 +10,+10）→ 不写入、不崩溃 ----
    {
        std::vector<ImageLayer> st;
        ImageLayer a; a.decodedPixels = solid(4, 4, red); a.zOrder = 0;
        ImageLayer c; c.decodedPixels = solid(2, 2, green); c.zOrder = 1;
        c.transform.inCanvasAnchor = generate::ImageAnchor::TopLeft;   // 锚左上，枢轴左上，再 +10,+10 → 全出界
        c.transform.layerPivot = generate::ImageAnchor::TopLeft;
        c.transform.x = 10; c.transform.y = 10;
        st = {a, c};
        const core::Image out = composeCanvas(st, 4, 4);
        CHECK(out.getPixel(3, 3) == red);          // 出界层未覆盖任何像素
    }

    // ---- 锚点 + 枢轴落位：TopRight 锚 + TopLeft 枢轴 → 图层左上角对齐画布右上角 ----
    {
        std::vector<ImageLayer> st;
        // 画布恒透明底（composeCanvas 无级背景）
        ImageLayer b; b.decodedPixels = solid(2, 2, blue); b.zOrder = 0;
        b.transform.inCanvasAnchor = generate::ImageAnchor::TopRight;   // 画布点 = (W, 0) = (4,0)
        b.transform.layerPivot = generate::ImageAnchor::TopLeft;        // 图层点 = (0,0) → offset=(4,0)
        st = {b};
        const core::Image out = composeCanvas(st, 4, 4);
        // offset=(4,0)：图层完全在 x≥4 之外（画布宽 4，列 0..3）→ 全裁掉，画布保持透明。
        CHECK(out.getPixel(3, 0).a == 0);
        // 改枢轴为 TopRight（图层点=(2,0)）→ offset=(2,0)，覆盖 (2,0)-(3,1)。
        st[0].transform.layerPivot = generate::ImageAnchor::TopRight;   // st 存的是副本，须改入库那份
        const core::Image out2 = composeCanvas(st, 4, 4);
        CHECK(out2.getPixel(2, 0) == blue && out2.getPixel(3, 1) == blue);
        CHECK(out2.getPixel(1, 0).a == 0);
    }

    // ---- 纯图案层：backdrop 经 makePattern 现算（Solid 绿 2×2），居中入 4×4 ----
    {
        std::vector<ImageLayer> st;
        ImageLayer rl;
        generate::PatternParams pp; pp.kind = generate::PatternKind::Solid; pp.background = green;
        rl.backdrop = pp; rl.transform.width = 2; rl.transform.height = 2; rl.zOrder = 0;
        st = {rl};
        const core::Image out = composeCanvas(st, 4, 4);
        CHECK(out.getPixel(1, 1) == green);   // 居中偏移 (1,1)
        CHECK(out.getPixel(0, 0).a == 0);     // 画布外区域透明
    }

    // ---- 无内层落位的图片元素：source-over 叠于图案之上（图案元素不因图片而丢） ----
    {
        ImageLayer a;
        generate::PatternParams pp; pp.kind = generate::PatternKind::Solid; pp.background = green;
        a.backdrop = pp;   // 无落位 → 区域＝源尺寸（2×2），图案先生成
        a.decodedPixels = core::Image(2, 2, core::ImageFormat::Rgba);
        a.decodedPixels.setPixel(0, 0, core::Color(0, 0, 0, 0));   // 透明 → 透出图案
        a.decodedPixels.setPixel(1, 0, red);                       // 不透明 → 盖住图案
        a.decodedPixels.setPixel(0, 1, core::Color(0, 0, 0, 0));
        a.decodedPixels.setPixel(1, 1, red);
        const core::Image mat = materializeLayer(a);
        CHECK(mat.getPixel(0, 0) == green);
        CHECK(mat.getPixel(1, 0) == red);
        CHECK(mat.getPixel(1, 1) == red);
        // 元素不透明度只作用于图片：red a=255 × opacity128 → (255·128+127)/255=128，叠绿底；图案不受影响。
        a.imageOpacity = 128;
        const core::Image mat2 = materializeLayer(a);
        CHECK(mat2.getPixel(1, 0) == core::compositeOver(green, core::Color(255, 0, 0, 128)));
        CHECK(mat2.getPixel(0, 0) == green);
    }

    // ---- 图层框 ≤0（退化＝无图层框）：源图按源尺寸直接叠于图案（不整层丢弃） ----
    {
        ImageLayer a;
        generate::PatternParams pp; pp.kind = generate::PatternKind::Solid; pp.background = green;
        a.backdrop = pp;
        a.hasImagePlacement = true;   // 有 fit 但框 ≤0
        a.boxWidth = 0; a.boxHeight = 0;
        a.decodedPixels = core::Image(2, 2, core::ImageFormat::Rgba);
        a.decodedPixels.setPixel(0, 0, core::Color(0, 0, 0, 0));
        a.decodedPixels.setPixel(1, 1, red);
        const core::Image mat = materializeLayer(a);
        CHECK(!mat.empty());   // 不再整层丢弃
        CHECK(mat.width() == 2 && mat.height() == 2);
        CHECK(mat.getPixel(0, 0) == green);
        CHECK(mat.getPixel(1, 1) == red);
    }

    // ---- 图片元素外观几何步：落位前施于源，区域（图层框）尺寸不被改写 ----
    {
        ImageLayer a;
        a.hasImagePlacement = true;
        a.boxWidth = 4; a.boxHeight = 4;
        a.imageAnchor = generate::ImageAnchor::TopLeft;
        a.imagePivot = generate::ImageAnchor::TopLeft;
        a.decodedPixels = core::Image(4, 2, core::ImageFormat::Rgba);   // 4×2：左半红右半蓝
        for (int y = 0; y < 2; ++y) for (int x = 0; x < 4; ++x)
            a.decodedPixels.setPixel(x, y, x < 2 ? red : blue);
        a.imagePreprocess.add(preprocess::RotateOp{90});   // 旋转 → 2×4，落位按新宽高
        const core::Image mat = materializeLayer(a);
        CHECK(mat.width() == 4 && mat.height() == 4);   // 区域仍是图层框
        bool hasRed = false; bool hasBlue = false; bool hasTransparent = false;
        for (int y = 0; y < 4; ++y) for (int x = 0; x < 4; ++x) {
            const core::Color c = mat.getPixel(x, y);
            if (c == red) hasRed = true;
            else if (c == blue) hasBlue = true;
            if (c.a == 0) hasTransparent = true;
        }
        CHECK(hasRed && hasBlue && hasTransparent);   // 内容在位、框内余下透明
    }

    // ---- Native 有框路径：区域按源尺寸收敛（框≠源尺寸时以源为准，Core 不校验调用方） ----
    {
        ImageLayer a;
        a.hasImagePlacement = true;
        a.imageFit = ImageFit::Native;
        a.boxWidth = 4; a.boxHeight = 4;   // 框 4×4 ≠ 源 2×2：Native 收敛到源尺寸
        a.decodedPixels = solid(2, 2, red);
        const core::Image mat = materializeLayer(a);
        CHECK(mat.width() == 2 && mat.height() == 2);
        CHECK(mat.getPixel(0, 0) == red && mat.getPixel(1, 1) == red);
    }

    // ---- 无框 + 图片元素外观几何步：区域＝预处理后源尺寸（不被原尺寸裁剪/留白；Core 侧防线收口） ----
    {
        ImageLayer a;
        generate::PatternParams pp; pp.kind = generate::PatternKind::Solid; pp.background = green;
        a.backdrop = pp;
        a.decodedPixels = core::Image(4, 2, core::ImageFormat::Rgba);   // 4×2：左半红右半蓝
        for (int y = 0; y < 2; ++y) for (int x = 0; x < 4; ++x)
            a.decodedPixels.setPixel(x, y, x < 2 ? red : blue);
        a.imagePreprocess.add(preprocess::RotateOp{90});   // 旋转 → 2×4：无框区域随之后移
        const core::Image mat = materializeLayer(a);
        CHECK(mat.width() == 2 && mat.height() == 4);   // 区域＝旋转后 2×4（非原 4×2）
        for (int y = 0; y < 4; ++y) for (int x = 0; x < 2; ++x) {
            const core::Color c = mat.getPixel(x, y);
            CHECK(c == red || c == blue);   // 整幅被源覆盖：无裁剪缺失、无图案留白
        }
    }

    // ---- 节点外观预处理物化：图片层不透明红 + Invert → materialize 反色、alpha 不变（不透明度属合成期，非预处理步）----
    {
        ImageLayer a; a.decodedPixels = solid(2, 2, red);
        a.preprocess.add(preprocess::InvertOp{"111"});
        const core::Image mat = materializeLayer(a);
        CHECK(mat.getPixel(0, 0) == core::Color(0, 255, 255, 255));
    }

    // ---- layerPlacement 落位矩形（拾取几何，与 composeCanvas 同口径）----
    {
        // 默认 Center/Center/(0,0)：File 4×4 入 8×8 画布 → 居中 offset=(2,2)。
        ImageLayer a; a.decodedPixels = solid(4, 4, red); a.id = 1;
        const auto p = layerPlacement(a, 8, 8);
        CHECK(p.has_value() && p->x == 2 && p->y == 2 && p->width == 4 && p->height == 4);
        // TopLeft 锚 + TopLeft 枢轴 → offset=(0,0)；叠加自由偏移 +10,+10 → (10,10)（越界不裁剪，矩形如实返回）。
        a.transform.inCanvasAnchor = generate::ImageAnchor::TopLeft;
        a.transform.layerPivot = generate::ImageAnchor::TopLeft;
        const auto p0 = layerPlacement(a, 8, 8);
        CHECK(p0->x == 0 && p0->y == 0);
        a.transform.x = 10; a.transform.y = 10;
        const auto p1 = layerPlacement(a, 8, 8);
        CHECK(p1->x == 10 && p1->y == 10 && p1->width == 4 && p1->height == 4);
        // 目标宽高缩放：4×4 → 6×6，Center 入 8×8 → offset=(1,1)、尺寸 6×6。
        ImageLayer s; s.decodedPixels = solid(4, 4, blue);
        s.transform.width = 6; s.transform.height = 6;
        const auto ps = layerPlacement(s, 8, 8);
        CHECK(ps->x == 1 && ps->y == 1 && ps->width == 6 && ps->height == 6);
        // 无可呈现位图（空占位）→ nullopt（合成与拾取跳过）。
        ImageLayer e;
        CHECK(!layerPlacement(e, 8, 8).has_value());
    }

    // ---- 层不透明度（合成期 alpha 乘子 a'=(a·opacity+127)/255）：透明底上直接读出乘子结果，RGB 不动 ----
    {
        auto composeOne = [](const core::Color& srcPx, const int opacity) {
            // 透明底
            ImageLayer a;
            a.decodedPixels = core::Image(1, 1, core::ImageFormat::Rgba);
            a.decodedPixels.setPixel(0, 0, srcPx);
            a.transform.opacity = opacity;
            const std::vector st = {a};
            return composeCanvas(st, 1, 1).getPixel(0, 0);
        };
        // 源 alpha=200, opacity=128 → (200·128+127)/255 = 100（floor），叠透明底 → alpha 100、RGB 不变
        CHECK(composeOne(core::Color(9, 9, 9, 200), 128) == core::Color(9, 9, 9, 100));
        // opacity=255 → 恒等（alpha 200）
        CHECK(composeOne(core::Color(9, 9, 9, 200), 255).a == 200);
        // opacity=0 → 全透明
        CHECK(composeOne(core::Color(9, 9, 9, 200), 0).a == 0);
    }

    // ---- 灰度层不透明度（Gap 2：GRAY 无 alpha 通道，层 opacity 仍生效，getPixel 视作 a=255）----
    {
        std::vector<ImageLayer> st;   // 透明底
        ImageLayer a;
        core::Image g(2, 2, core::ImageFormat::Gray); g.fill(core::Color(50, 50, 50, 255));
        a.decodedPixels = g;
        a.transform.opacity = 128;   // (255·128+127)/255 = 128
        st = {a};
        const core::Image out = composeCanvas(st, 2, 2);
        CHECK(out.getPixel(0, 0) == core::Color(50, 50, 50, 128));
    }

    // ---- 预物化表（materialized，缺口 1 缓存契约）----
    {
        // 单层 File：解码像素红 + Invert 预处理（materializeLayer 会反色成青）。
        // 命中预物化表时 composeCanvas 直取表内位图、跳过 materializeLayer，故喂入的位图原样落位。
        ImageLayer a; a.id = 7;
        a.decodedPixels = solid(2, 2, red);
        a.preprocess.add(preprocess::InvertOp{"111"});
        a.transform.inCanvasAnchor = generate::ImageAnchor::TopLeft;
        a.transform.layerPivot = generate::ImageAnchor::TopLeft;
        std::vector<ImageLayer> st; st = {a};

        // ① 无表：现算 materializeLayer → 红反色成青（与 L1341 段同口径）。
        const core::Image noCache = composeCanvas(st, 2, 2);
        CHECK(noCache.getPixel(0, 0) == core::Color(0, 255, 255, 255));

        // ② 命中缓存表：以「缩放前」绿色位图为物化产物 → compose 直取、不重跑 Invert → 落位仍绿（证缓存生效）。
        const std::unordered_map<int, core::Image> cache{{7, solid(2, 2, green)}};
        const core::Image cached = composeCanvas(st, 2, 2, core::Image{}, &cache);
        CHECK(cached.getPixel(0, 0) == green);

        // ③ 表内命中但为空图 → 回退现算（等同无表）：护栏防把「空占位」误当已物化结果。
        const std::unordered_map<int, core::Image> cacheEmpty{{7, core::Image{}}};
        const core::Image coreImageFallback = composeCanvas(st, 2, 2, core::Image{}, &cacheEmpty);
        CHECK(coreImageFallback.getPixel(0, 0) == core::Color(0, 255, 255, 255));

        // ④ 表内 id 不匹配（层 7 不在表）→ 回退现算，与无表一致。
        const std::unordered_map<int, core::Image> cacheMiss{{9, solid(2, 2, green)}};
        const core::Image miss = composeCanvas(st, 2, 2, core::Image{}, &cacheMiss);
        CHECK(miss.getPixel(0, 0) == core::Color(0, 255, 255, 255));
    }

    // ---- composeBase（底图同构 ImageLayer 展平，SPEC §7 generate 段）----
    {
        // Recipe 底（纯图案、无内层落位）：按 source 尺寸现算图案、铺满画布。
        ImageLayer base;
        generate::PatternParams pp;
        pp.kind = generate::PatternKind::Solid;
        pp.background = green;
        base.backdrop = pp;   // transform 恒等（width/height=0），不走 materializeLayer 的尺寸回归
        const core::Image pat = composeBase(base, 4, 4);
        CHECK(pat.width() == 4 && pat.height() == 4);
        CHECK(pat.getPixel(0, 0) == green && pat.getPixel(3, 3) == green);

        // File 底 + 内层落位：composeBase 内部转调 composeCanvas({base},w,h)，二者逐像素等价（委托契约）。
        ImageLayer fb;
        fb.decodedPixels = solid(2, 2, red);
        fb.hasImagePlacement = true;
        fb.boxWidth = 4; fb.boxHeight = 4;
        fb.imageFit = ImageFit::Crop;
        fb.imageAnchor = generate::ImageAnchor::TopLeft;
        fb.imagePivot = generate::ImageAnchor::TopLeft;
        const core::Image viaBase = composeBase(fb, 4, 4);
        const core::Image viaCanvas = composeCanvas({fb}, 4, 4);
        CHECK(viaBase.width() == 4 && viaBase.height() == 4);
        CHECK(viaBase.getPixel(0, 0) == red);           // 源图左上对齐图层框左上（TopLeft 锚/枢轴）
        CHECK(viaBase.getPixel(2, 2).a == 0);           // 图层框越源部分透明
        for (int y = 0; y < 4; ++y)
            for (int x = 0; x < 4; ++x)
                CHECK(viaBase.getPixel(x, y) == viaCanvas.getPixel(x, y));
    }

    // ---- 整层生效（色彩及底色）：源空但带底色框 + Invert → 框底色一起被反色（预处理作用于含底色的整层、底色亦参与）----
    {
        ImageLayer a; a.id = 20;   // decodedPixels 空（仅靠底色框呈现）
        a.hasImagePlacement = true; a.boxWidth = 2; a.boxHeight = 2;
        generate::PatternParams bd; bd.kind = generate::PatternKind::Solid; bd.background = green;
        a.backdrop = bd;
        a.preprocess.add(preprocess::InvertOp{"111"});
        a.transform.inCanvasAnchor = generate::ImageAnchor::TopLeft;
        a.transform.layerPivot = generate::ImageAnchor::TopLeft;
        const std::vector st = {a};
        const core::Image out = composeCanvas(st, 2, 2);
        CHECK(out.getPixel(0, 0) == core::Color(255, 0, 255, 255));   // 绿底反色成品红（证明预处理作用于含底色的整层）
    }

    // ---- 整层生效（几何驱动足迹）：源 2×4 + 目标框 2×4 + Rotate90 → 落位 4×2（宽高互换、不被拉回旧框）----
    {
        ImageLayer a; a.id = 21;
        a.decodedPixels = solid(2, 4, red);
        a.transform.width = 2; a.transform.height = 4;    // 区域缩放不覆盖几何步：旋转后宽高互换，不被拉回 2×4
        a.preprocess.add(preprocess::RotateOp{90});
        const auto p = layerPlacement(a, 8, 8);
        CHECK(p.has_value() && p->width == 4 && p->height == 2);   // 几何步改变最终足迹（4×2）
    }

    // ---- 整层生效（resize 步设新尺寸、覆盖 transform 目标）：源 4×4 + transform 2×2 + Resize6×3 → 落位 6×3 ----
    {
        ImageLayer a; a.id = 22;
        a.decodedPixels = solid(4, 4, blue);
        a.transform.width = 2; a.transform.height = 2;
        a.preprocess.add(preprocess::ResizeOp{6, 3});
        const auto p = layerPlacement(a, 20, 20);
        CHECK(p.has_value() && p->width == 6 && p->height == 3);
    }

    std::cout << "[canvas] OK\n";
}

// ---------------------------------------------------------------------------
// main：依次执行所有测试段
// ---------------------------------------------------------------------------
int main() {
    testCore();
    testGeometry();
    testPixelOps();
    testGenerate();
    testCanvas();
    testAnnotation();
    testDragGesture();   // 平移拖拽手势（零 Qt，跨模块直编）
    testPickCycle();     // 同点连击循环选层（零 Qt，跨模块直编）
    testConfigAssets();  // GUI 配置资产助手（零 Qt，跨模块直编）：查找链 / 解码注入 / 打包 / 字段往返
    testPreprocess();
    testPreprocessSession();  // 预处理会话（零 Qt，跨模块直编）不变式/归一化/baseDirty/签名断言
    testHistory();
    testPipeline();
    testEngine();          // 引擎数据结构骨架
    testEnginePipeline();  // 引擎流水线功能（SPEC §3）
    testEngineExport();    // 引擎导出落盘（SPEC §4）
    testEngineBoundary();  // 引擎边界异常（SPEC §5 / E-1~E-8）
    testEngineGridAxis();  // L3 网格单轴三态（分割数 / 比例）：取整律、混合轴、精确报因、JSON 双形态
    testEngineConfigJson(); // 配置 JSON 存取（SPEC §7，含 annotations 段）
    testEngineConfigStrict(); // 配置严格 schema 校验（present-but-invalid 拒绝；缺失/越界/合法别名仍过）
    testPercentCoords();    // 百分比几何坐标："N%" 换算 / 保留比例回写 / 无基准与小数拒绝
    testAnnoParseText();    // 标注 JSON 文本段级解析（SPEC §7，供 CLI 复用）
    testLayerParseText();   // 图层 JSON 文本段级解析（SPEC §7，供 CLI --layer/--layers 复用）
    testAnnoAnchorRoundTrip(); // 约束图元锚点往返（buildShape ↔ shapeToJson/FromJson 顶点级一致）
    testI18nCatalog();   // i18n 目录对账：读源码树比三层「常量层 ↔ 译文表」，孤儿键 / 重复键 / 跨层撞键即红
    testI18nCore();      // i18n 机制内核（本段会改进程级语言态，故置于最后，收尾已复位到源语言）
    std::cout << "all tests passed.\n";
    return 0;
}
