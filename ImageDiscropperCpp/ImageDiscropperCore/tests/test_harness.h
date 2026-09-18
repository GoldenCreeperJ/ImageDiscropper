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

// ---------------------------------------------------------------------------
// 引擎验收测试段（定义于独立 .cpp，由 test_main.cpp 的 main() 调用）。
//   testEnginePipeline —— SPEC §3 功能：L1/L2 保留尺寸与四角位置、极性开关、L3 网格/选择/排序/重排。
//   testEngineExport   —— SPEC §4 导出：分离到文件夹、坍缩合并、降级重排、PNG/BMP/JPEG/WebP 四格式写盘。
//   testEngineBoundary —— SPEC §5 边界：E-1~E-8 全覆盖 + 空块跳过 + 像素无重复无丢失。
// ---------------------------------------------------------------------------
void testEnginePipeline();
void testEngineExport();
void testEngineBoundary();
