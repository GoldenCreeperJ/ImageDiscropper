// ============================================================================
// 文件：tests/test_harness.h
// 作用：极简测试骨架——统一的 CHECK 断言宏 + 跨文件测试段声明。
//       供 test_main.cpp 与各 test_engine_*.cpp 共享，避免每个测试文件重复定义宏、
//       也避免把所有测试塞进单一 God File（严禁 God File）。
// 分块依据：CHECK 宏是“断言机制”，测试段声明是“跨编译单元的连接点”，二者集中于此；
//       具体测试逻辑分散在各 test_engine_*.cpp，由 test_main.cpp 的 main() 统一调用。
// ============================================================================
#pragma once

#include <cstdlib>
#include <iostream>

// 简易断言宏：失败时打印文件 / 行号 / 表达式并以非零码退出。
// 说明：宏内含逗号表达式时用 CHECK((a==b)) 双括号包裹，避免逗号被当作宏参数分隔符。
#define CHECK(expr)                                                            \
    do {                                                                       \
        if (!(expr)) {                                                         \
            std::cerr << "CHECK failed: " #expr " @ " << __FILE__ << ":"       \
                      << __LINE__ << "\n";                                     \
            std::exit(1);                                                      \
        }                                                                      \
    } while (0)

// 带动态说明文本的断言：用于「读文件 / 解析目录」这类光看表达式无法定位的失败。
// 与 CHECK 的区别只在于多一个会在失败时插流的 msg（求值仅在失败路径上发生）。
#define CHECK_MSG(expr, msg)                                                   \
    do {                                                                       \
        if (!(expr)) {                                                         \
            std::cerr << "CHECK_MSG failed: " #expr " -- " << (msg) << " @ "    \
                      << __FILE__ << ":" << __LINE__ << "\n";                  \
            std::exit(1);                                                      \
        }                                                                      \
    } while (0)

// ---------------------------------------------------------------------------
// 引擎验收测试段（定义于独立 .cpp，由 test_main.cpp 的 main() 调用）。
//   testEnginePipeline —— SPEC §3 功能：L1/L2 保留尺寸与四角位置、极性开关、L3 网格/选择/排序/重排。
//   testEngineExport   —— SPEC §4 导出：分离到文件夹、坍缩合并、降级重排、PNG/BMP/JPEG/WebP 四格式写盘。
//   testEngineBoundary —— SPEC §5 边界：E-1~E-8 全覆盖 + 空块跳过 + 像素无重复无丢失。
//   testEngineGridAxis —— FR-L3.1 网格单轴三态：分割数 / 比例的取整律、周期轴混合、非法定义报因、JSON 双形态往返。
//   testEngineConfigJson —— SPEC §7 配置存取：字段往返 + annotations 段带样式往返 + 向后兼容。
//   testAnnoParseText    —— SPEC §7 标注文本段级解析：parseAnnotationsFromJsonText 单对象/根数组/累积/回滚/逐条强制校验/空串。
//   testAnnoAnchorRoundTrip —— SPEC §7 约束图元锚点往返：buildShape(锚点)→shapeToJson(反解)→shapeFromJson 顶点级一致。
//   testI18nCore    —— i18n 机制内核：表登记 / 语言码归一 / 缺译回退 / 源语言不建表（放最后一段，会改全局语言态）。
//   testI18nCatalog —— i18n 目录对账：孤儿键 / 重复键 / 跳层撞键 / 契约组越界 fail + 各组覆盖率打印（不改进程级语言态）。
// ---------------------------------------------------------------------------
void testEnginePipeline();
void testEngineExport();
void testEngineBoundary();
void testEngineGridAxis();      // L3 网格单轴三态：分割数 / 比例取整律 + 混合轴 + 精确报因 + JSON 双形态
void testEngineConfigJson();
void testEngineConfigStrict();   // SPEC §7 配置严格 schema 校验：present-but-invalid 拒绝 + 缺失/越界/合法别名仍过
void testPercentCoords();        // SPEC §7 百分比几何坐标："N%" 依基准换算 + 保留比例回写 + 无基准/小数拒绝
void testAnnoParseText();
void testLayerParseText();   // 图层 JSON 文本段级解析（SPEC §7，供 CLI --layer/--layers 复用）
void testAnnoAnchorRoundTrip();
void testI18nCore();
void testI18nCatalog();
