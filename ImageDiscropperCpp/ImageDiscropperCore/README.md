# ImageDiscropperCore

`ImageDiscropperCore` 是「**图像区域提取、反向剔除与网格分割工具**」的 **Core 层**
（纯逻辑静态库，命名空间 `idc`）。它是功能规格 [`../SPEC.md`](../../SPEC.md) 所述三层架构
（Core / CLI / GUI）中的 **Core**，不依赖任何 GUI，可被上层 CLI 与 GUI 直接引用。

> Grid-Selection-Emit 统一引擎的完整实现落于此库；三层模式（L1/L2/L3）共用同一条代码路径（NFR-0）。

---

## 1. 库的组成（模块划分）

代码按**功能 / 任务**分目录、分文件；`include/` 为对外接口，`src/` 为一一对应的实现。
按对新项目的**主次**分为两层：

- **① 核心引擎层（新项目主体）**：`engine`。承载切割线 / 诱导网格 / 选择集 / 极性 / 排布导出的全部价值。
- **② 复用支撑层（前置能力）**：`core` / `io` / `geometry` / `annotation` / `preprocess` / `generate` / `canvas`。
  服务 SPEC §3.1 FR-1「基础图像处理」前置层，是引擎的输入准备与辅助。

| 模块           | 命名空间              | 层         | 职责（细节见各子目录 README）                                                                                                                                        | 对应规格                          |
|--------------|-------------------|-----------|-----------------------------------------------------------------------------------------------------------------------------------------------------------|-------------------------------|
| `engine`     | `idc::engine`     | ★ 核心      | Grid-Selection-Emit 统一引擎：切割线 / 网格 / 切分 / 选择极性 / 排序 / 合成 / 导出 / 配置 JSON / 顶层编排 `runEngine`。                                                                | SPEC §1 全部核心概念                |
| `core`       | `idc::core`       | 支撑（地基）    | 基础数据类型 `Color` / `Point2D` / `Image`（含 `resized`/`scaled`/`colorAt` 等通用像素原语）与 `compositeOver`；header-only 泛型撤销栈 `HistoryManager<T>`、有序步骤队列 `Pipeline<T>`。 | SPEC §2.2 / FR-1.5 / NFR-4    |
| `io`         | `idc::io`         | 支撑（基础 IO） | 图像编解码：`readImageFile` / `writeImageFile` / 内存编码。底层 stb（PNG/JPEG/BMP）+ libwebp（WebP），第三方类型不经公共头扩散。                                                         | SPEC §4.5 / §5 E-8            |
| `geometry`   | `idc::geometry`   | 支撑        | 标注图形几何：`ShapeType` / `Shape` / `Path` / `AffineTransform`。                                                                                                | FR-1.3 标注几何                   |
| `annotation` | `idc::annotation` | 支撑        | 标注集合：`AnnotationSet`（纯数据 + zOrder + 命中 + 烧录 `burnIn`）/ `ShapeFactory` / `Rasterizer` / `ViewTransform` / 标注 ↔ JSON。                                       | FR-1.3 标注图层（可烧录）              |
| `preprocess` | `idc::preprocess` | 支撑        | 切割前的声明式有序步骤队列 `PreprocessPipeline`（公开继承 `core::Pipeline<PreprocessConfig>`）：灰度 / 反色 / 色道分离 / 旋转 / 翻转 / resize；`geometryOutput` 几何推演 + `applyExec` 执行层融合。  | SPEC §7 `preprocess` 段 / FR-1 |
| `canvas`     | `idc::canvas`     | 支撑        | 多层画布数据模型与合成（纯像素、零 GUI）：`ImageLayer` / `ImageTransform` / `materializeLayer` / `composeCanvas` / `composeBase` / `applyAlpha`。                             | SPEC §7 `layers` 段（多层画布）      |
| `generate`   | `idc::generate`   | 支撑        | 程序化图案生成（「新建画布」画布内容工厂）：`makePattern` 依 `PatternParams` 产出 RGBA 图（渐变 / 棋盘 / 条纹 / 网格线 / 点阵 / 同心环 / 放射线 / 色相 / 噪声）。                                           | —（新建 / 生成源能力）                 |

> `examples/`（演示程序）与 `tests/`（极简自测）分别验证库的典型用法与关键行为。
> 表中「层」表达**主次**（`engine` 为主体）；§4 目录顺序仅为**物理布局**，二者不必一致。

---

## 2. 与功能规格核心概念的对应

SPEC §1 流水线概念在本库中的落点如下（✅ 表示数据结构与算法均已落地）：

| 规格概念                   | 本库类型 / 接口                                                                                                                     | 头文件                                |
|------------------------|-------------------------------------------------------------------------------------------------------------------------------|------------------------------------|
| `Image`                | `idc::core::Image`                                                                                                            | `core/image.h`                     |
| `Region` / `RegionSet` | `idc::engine::RectRegion` / `RegionKind` / `RegionSet` / `Fragment`                                                           | `engine/region.h` / `region_set.h` |
| 切割线（cut line）          | `idc::engine::CutLine` / `CutLineSet`                                                                                         | `engine/cut_line.h`                |
| `Grid`                 | `idc::engine::Grid` / `Cell` / `GridParams`（`build()` / `induceGrid()`）                                                       | `engine/grid.h`                    |
| `Selection`            | `idc::engine::Selection` / `Polarity`（`resolve()` / `applyPolarity()`）                                                        | `engine/selection.h`               |
| `Sequence`             | `idc::engine::Sequence` / `SortStrategy`（`build()`）                                                                           | `engine/sequence.h`                |
| `Composition`          | `idc::engine::Composition` / `MergeLayout` / `MergeOrder`（`isCollapsible()` / `compose()`）                                    | `engine/composition.h`             |
| 流水线编排                  | `generateCutLines` / `induceGrid` / `split` / `applyPolarity` / `compose` / `exportImage` / `runEngine` / `runEngineGeometry` | `engine/engine.h`                  |
| 图像编解码 I/O              | `readImageFile` / `writeImageFile` / 内存编码（stb + libwebp，路径按 UTF-8 解析）                                                         | `io/image_io.h`                    |
| 多层画布图层                 | `idc::canvas::ImageLayer` / `materializeLayer` / `composeCanvas` / `composeBase`                                              | `canvas/layer.h`                   |
| 配置 JSON                | `saveEngineConfig` / `loadEngineConfig`（SPEC §7 整档往返，可选 `annotations` 段随配置读写）                                                 | `engine/engine_config_json.h`      |
| 标注图层（burn-in）          | `idc::annotation::AnnotationSet::burnIn(base)` / `restore`                                                                    | `annotation/annotation_set.h`      |

三种模式即引擎的不同参数预设（SPEC §1）：

- **标准提取**：`split → select → keep → export`（`Polarity::Keep`）
- **反向剔除**：`split → select → remove → export`（`Polarity::Remove`）
- **网格重组**：`grid → select → sequence → compose → export`

> **合并重排的两个"顺序"正交**：`Sequence`（选择排序）决定保留块的先后列表；`MergeOrder`（行/列优先 + 蛇形 + 倒序）决定该列表铺进输出画布的路径。合并重排**仅 L3 网格模式支持**，其余显式请求重排直接报错（不自动降级）。

---

## 3. 典型调用

```cpp
EngineConfig cfg;                                        // 三层模式的统一配置
CutLineSet lines = generateCutLines(cfg.cut, cfg.source);   // ①
Grid grid        = induceGrid(lines, cfg.source);           // ②
RegionSet all    = split(image, grid);                      // split
RegionSet kept   = applyPolarity(all, selection);           // ③④
Composition comp = compose(kept, sequence, cfg.emitParams); // ⑤
exportImage(comp, image, outputPath);                       // export
// 或一次跑通（不落盘，返回保留集 + 合成描述）：
EngineResult r = runEngine(image, cfg);
```

> 多层画布：`composeCanvas` 依底图 → 叠加层顺序把 `cfg.layers` 展平为一张工作图，再交 `runEngine`（引擎签名与逻辑不变，永远只收一张展平图 + 空 preprocess）。

---

## 4. 目录结构

```
ImageDiscropperCore/
├── CMakeLists.txt            构建脚本（静态库 + demo + unit_tests）
├── README.md                 本文件
├── include/                  公共头文件（对外接口）
│   ├── core/                 基础数据类型 Color / Point / Image（含通用像素原语与 compositeOver）+ HistoryManager / Pipeline 模板
│   ├── io/                   图像编解码 readImageFile / writeImageFile
│   ├── i18n/                 三层共用 i18n 内核 core_i18n.h（零 Qt）+ 字符串常量层 core_texts.h
│   ├── geometry/             标注图形几何：ShapeType / Shape / Path / AffineTransform
│   ├── annotation/           标注集合：AnnotationSet / ShapeFactory / Rasterizer / ViewTransform
│   ├── preprocess/           预处理流水线：PreprocessConfig / PreprocessPipeline（像素核 pixel_kernels 为 src 私有）
│   ├── canvas/               多层画布数据模型与合成：ImageLayer / composeCanvas（零 GUI、无 IO）
│   ├── generate/             程序化图案生成：makePattern / PatternKind / PatternParams
│   └── engine/               ★ Grid-Selection-Emit 统一引擎接口
├── src/                      对应 include 的实现（含私有内部头 json_common.h、engine_config_json_internal.h、pixel_kernels.h）
├── examples/                 演示程序 demo_main.cpp（真正跑通引擎并导出到 demo_output/）
└── tests/                    自测 test_main.cpp + test_engine_*.cpp（引擎验收）+ test_i18n_*.cpp（i18n 内核与目录对账）
```

每个子目录下都有独立的 `README.md` 说明该目录的职责边界与分块依据。

---

## 5. 构建

工程使用 CMake（>= 3.28）。除标准库外依赖三个 vcpkg 第三方库，且**全部以 PRIVATE 方式接入**
（仅本库对应 `.cpp` 内部使用，不经公共头暴露给下游）：

- **stb**（header-only）：PNG/JPEG/BMP 编解码 —— `find_package(Stb REQUIRED)`，仅 `src/io/image_io.cpp` 使用。
- **libwebp**：WebP 编解码（补齐 stb 不支持的格式）—— `find_package(WebP CONFIG REQUIRED)` 链接 `WebP::webp`，仅 `src/io/image_io.cpp` 使用。
- **nlohmann/json**（单头）：配置与标注 JSON 存取 —— `find_package(nlohmann_json CONFIG REQUIRED)` 链接 `nlohmann_json::nlohmann_json`，仅 `engine_config_json.cpp` 与 `annotation/annotation_json.cpp` 使用。

先安装依赖（`vcpkg install stb libwebp nlohmann-json`），再配置构建：

```bash
cmake -S . -B build
cmake --build build --config Release
```

构建产物：

- `libimage_discropper_core.a` / `image_discropper_core.lib` — Core 静态库
- `demo` — 演示可执行程序（`examples/demo_main.cpp`，跑通引擎并导出到 `demo_output/`）
- `unit_tests` — 自测（`tests/test_main.cpp` + `tests/test_engine_*.cpp` + `tests/test_i18n_*.cpp`）

---

## 6. 本库特有约定

- **坐标约定**（SPEC §2.1）：整数像素、原点左上、`x` 向右 `y` 向下、区间左闭右开 `[a, b)`。
- **像素存储**：`Image` 支持 `RGB` / `RGBA` / `GRAY` 三种模式，行主序、通道交错。
- **引擎实现**：三层模式（L1/L2/L3）共用同一条代码路径，仅为参数预设（NFR-0）；导出编码支持 PNG/JPEG/BMP（stb）与 WebP（libwebp）。
- **路径编码**：所有文件打开（读写图像、配置、导出）以 **UTF-8** 解析路径（`core::pathFromUtf8` / `pathToUtf8`，取代 C++23 已移除的 `u8path`），中文 / 非 ASCII 路径可读写；上游须传 UTF-8。
- **多层画布是画布层**：`runEngine` 永远只收一张展平的画布图 + 空 preprocess；图层栈合成与逐层预处理在 Core 内完成、不入引擎。图层资源以相对名引用、位图不入配置（缺文件降级为占位）。
- **i18n 内核住本层、译文表不住**：`i18n/core_i18n.h`（零 Qt）持有唯一的进程级语言状态（已装语言表 + 当前语言），GUI / CLI / Core 三层共用同一张扁平表（键 = `分组::常量名`）；Core 只放自己的报因与图案名表（`src/i18n/lang_en.cpp`），GUI / CLI 的文案仍住各自层的 `lang_en.cpp`。源语言 zh-CN 不建表（回退常量层原文）。机制详见根 [`../README.md`](../README.md)「三层共用的 i18n」与 [include/i18n/README.md](include/i18n/README.md)。
- 全仓库统一约定（C++23、中文注释、命名风格、错误处理、分层纪律）见根 [`CONTRIBUTING.md`](../../CONTRIBUTING.md)。

---

## 7. 许可

本模块与仓库整体一致，采用 **GPL-3.0**（见仓库根 [`LICENSE`](../../LICENSE)）。
