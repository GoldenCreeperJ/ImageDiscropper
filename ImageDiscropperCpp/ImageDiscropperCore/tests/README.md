# tests

**目录作用**：极简自测入口，用于验证核心模块与 Grid-Selection-Emit 引擎的正确性。
不依赖任何第三方测试框架（如 GoogleTest），只用断言宏（`CHECK`）+ 标准输出。

**分块依据**：`test_main.cpp` 的 `main()` 统一调用各测试段；断言宏与跨文件测试段声明抽到
`test_harness.h` 共享；引擎验收测试按主题拆为 pipeline / export / boundary / config-json 四个独立 `.cpp`，
由 CMake 统一收集，避免把所有测试塞进单一 God File（严禁 God File）。

| 文件                            | 职责                                                                                                                                                                                                                                                                                                                             |
|-------------------------------|--------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------|
| `test_harness.h`              | 共享的 `CHECK` 断言宏 + 六个引擎测试段（`testEnginePipeline` / `Export` / `Boundary` / `ConfigJson` / `AnnoParseText` / `AnnoAnchorRoundTrip`）声明                                                                                                                                                                                             |
| `test_main.cpp`               | `main()` 入口：core / geometry / pixel_ops / annotation / preprocess（含像素核 `applyOne` 逐步与 `applyExec` 执行层融合等价回归）/ preprocess_session（GUI 零 Qt 预处理会话，跨模块直编回归不变式/归一化/baseDirty/签名）/ history（泛型撤销栈 `HistoryManager<T>`，原 history 模块、现属 core）/ pipeline（泛型有序步骤队列 `core::Pipeline<T>` 的按序增删移改与越界钳制/忽略边界策略）/ engine 数据结构冒烟测试 + 调用六个引擎验收段 |
| `test_engine_pipeline.cpp`    | 功能：L1 十字/横线/竖线保留尺寸；L2 四角位置与坍缩尺寸；极性开关；L3 网格/单元选择/排序（row·col-major·reverse·snake·custom）/重排                                                                                                                                                                                                                                      |
| `test_engine_export.cpp`      | 导出：分离导出写入文件夹（自动创建）与命名模板、坍缩合并尺寸、坍缩不可行→降级重排、PNG/BMP/JPEG/WebP 四格式写盘成功                                                                                                                                                                                                                                                            |
| `test_engine_boundary.cpp`    | SPEC §5 边界：E-1~E-8 全覆盖 + 空块跳过 + 像素归属无重复无丢失（左闭右开校验）                                                                                                                                                                                                                                                                             |
| `test_engine_config_json.cpp` | SPEC §7 配置 JSON 往返：四类标注（多边形/椭圆/路径/文字）含样式与变换快照逐字段比对、`annotations` 段缺失向后兼容、缺段+传层＝清空（配置为真相源）；`testAnnoParseText`（`parseAnnotationsFromJsonText` 文本段级解析：单对象/根数组/累积/回滚/逐条强制校验负例/空串，供 CLI 复用）；`testAnnoAnchorRoundTrip`（约束多边形 8 例顶点级 + 圆/椭圆 3 例包围盒级锚点往返一致）                                                                           |

> 导出类测试写入临时目录并校验文件存在 / 尺寸 / 文件夹内图片数，测试结束后清理。

## 运行

```bash
cmake -S .. -B ../build
cmake --build ../build --target unit_tests
../build/unit_tests          # 或经 CTest：ctest --test-dir ../build
```
