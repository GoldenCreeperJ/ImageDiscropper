# tests

**目录作用**：极简自测入口，用于验证核心模块在常见场景下的正确性。
不依赖任何第三方测试框架（如 GoogleTest），只用断言宏 + 标准输出。

**分块依据**：所有测试用例集中在一个 `test_main.cpp` 里，按模块分段；
将来若需扩展，可拆分为 `test_core.cpp` / `test_processing.cpp` 等独立文件，
由 CMake 统一收集。

| 文件              | 职责                                                                             |
|-----------------|--------------------------------------------------------------------------------|
| `test_main.cpp` | core / geometry / processing / annotation / preprocess / history / engine 七大模块的冒烟测试 |

> engine 段仅测试数据结构（区域、切割线集合、选择集位操作、序列、配置组装），**不调用**尚为桩实现的算法函数。

## 运行

```bash
cmake -S .. -B ../build
cmake --build ../build --target unit_tests
../build/unit_tests
```
