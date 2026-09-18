# ImageDiscropperCore

`ImageDiscropperCore` 是「**图像区域提取、反向剔除与网格分割工具**」的 **Core 层**
（纯逻辑静态库，命名空间 `idc`）。它是功能规格 [`../SPEC.md`](../SPEC.md) 所述三层架构
（Core / CLI / GUI）中的 **Core**，不依赖任何 GUI，可被上层 CLI 与 GUI 直接引用。

> 本库由早期的图像绘制与处理逻辑库重构而来：**保留并重定位**了可复用的基础图像处理与
> 标注能力（SPEC §3.1 FR-1 前置层），并**落地了 Grid-Selection-Emit 统一引擎**的完整实现；
> 三层模式共用同一条代码路径（NFR-0）。

---

## 1. 库的组成（模块划分）

代码按**功能 / 任务**分目录、分文件；`include/` 为对外接口，`src/` 为一一对应的实现。
按对新项目的**主次**分为两层——先理解这一点，才不会把「复用来的支撑能力」误当作项目主体：

- **① 核心引擎层（新项目主体）**：`engine`。承载本工具区别于常规裁剪的全部价值
  （切割线 / 诱导网格 / 选择集 / 极性 / 排布导出），对应 MVP 与 v2，
  是后续开发的主战场；现已落地为完整可运行的统一引擎（含 stb/libwebp 图像编码 / nlohmann JSON 配置）。
- **② 复用支撑层（前置能力）**：`core` / `pixel_ops` / `preprocess` / `geometry` /
  `annotation` / `history`。由旧库重构保留，服务于 SPEC §3.1 **FR-1「基础图像处理」前置层**，
  多属 v3（标注图层最重、最后做）。它们是引擎的输入准备与辅助，**不是**主体。

| 模块           | 命名空间              | 层      | 职责                                                                      | 对应规格                          |
|--------------|-------------------|--------|-------------------------------------------------------------------------|-------------------------------|
| `engine`     | `idc::engine`     | ★ 核心   | Grid-Selection-Emit 统一引擎（切割/网格/选择/合成/导出/JSON 全实现）                       | SPEC §1 全部核心概念                |
| `core`       | `idc::core`       | 支撑（地基） | 基础数据类型：`Color` / `Point2D` / `Image`                                    | SPEC §2.2 `Image`             |
| `pixel_ops`  | `idc::pixel_ops`  | 支撑     | 像素处理：颜色/通道、旋转/翻转/缩放                                                     | FR-1.1 / FR-1.2 / FR-1.4      |
| `preprocess` | `idc::preprocess` | 支撑     | 切割前的基础图像处理流水线：`PreprocessPipeline`                                      | SPEC §7 `preprocess` 段 / FR-1 |
| `geometry`   | `idc::geometry`   | 支撑     | 标注图形几何：`ShapeType` / `Shape` / `Path` / `AffineTransform`               | FR-1.3 标注几何                   |
| `annotation` | `idc::annotation` | 支撑     | 标注层：`AnnotationLayer` / `ShapeFactory` / `Rasterizer` / `ViewTransform` | FR-1.3 标注图层（可烧录）              |
| `history`    | `idc::history`    | 支撑     | 泛型撤销 / 重做栈：`HistoryManager<T>`（被 `annotation::AnnotationLayer` 复用）      | FR-1.5 / NFR-4                |

> `examples/`（演示程序）与 `tests/`（极简自测）分别验证库的典型用法与关键行为。
> 注：表中「层」表达**主次**（`engine` 为主体）；§4 的目录顺序仅为**物理布局**，二者不必一致。

---

## 2. 与功能规格核心概念的对应

SPEC §1 流水线概念在本库中的落点如下：

| 规格概念                       | 本库类型 / 接口                                                                                               | 头文件                             | 状态                                     |
|----------------------------|---------------------------------------------------------------------------------------------------------|---------------------------------|----------------------------------------|
| `Image`                    | `idc::core::Image`                                                                                      | `core/image.h`                  | ✅ 已实现                                  |
| `Region`                   | `idc::engine::RectRegion` / `RegionKind`                                                                | `engine/region.h`               | ✅ 数据结构                                 |
| `RegionSet`                | `idc::engine::RegionSet` / `Fragment`                                                                   | `engine/region_set.h`           | ✅ 数据结构                                 |
| 切割线（cut line）              | `idc::engine::CutLine` / `CutLineSet`                                                                   | `engine/cut_line.h`             | ✅ 数据结构                                 |
| `Grid`                     | `idc::engine::Grid` / `Cell` / `GridParams`                                                             | `engine/grid.h`                 | ✅ 已实现（`build()` / `induceGrid()`）      |
| `Selection`                | `idc::engine::Selection` / `Polarity`                                                                   | `engine/selection.h`            | ✅ 已实现（`resolve()` / `applyPolarity()`） |
| `Sequence`                 | `idc::engine::Sequence` / `SortStrategy`                                                                | `engine/sequence.h`             | ✅ 已实现（`build()`）                       |
| `Composition`              | `idc::engine::Composition` / `MergeLayout` / `MergeOrder`                                               | `engine/composition.h`          | ✅ 已实现（`isCollapsible()` / `compose()`） |
| 流水线 `split/compose/export` | `generateCutLines` / `induceGrid` / `split` / `applyPolarity` / `compose` / `exportImage` / `runEngine` | `engine/engine.h`               | ✅ 已实现（顶层编排 `runEngine`）                |
| 图像编码 I/O                   | `writeImageFile` / `encodeImageToMemory`                                                                | `engine/image_io.h`             | ✅ 已实现（stb：PNG/JPEG/BMP；libwebp：WebP）   |
| 配置 JSON                    | `saveEngineConfig` / `loadEngineConfig`                                                                 | `engine/engine_config_json.h`   | ✅ 已实现（nlohmann/json）                   |
| 标注图层（burn-in）              | `idc::annotation::AnnotationLayer::burnIn()`                                                            | `annotation/annotation_layer.h` | ✅ 已实现                                  |

> 图例：✅ 可直接使用（数据结构与算法均已落地）。

三种模式即引擎的不同参数预设（SPEC §1）：

- **标准提取**：`split → select → keep → export`（`Polarity::KEEP`）
- **反向剔除**：`split → select → remove → export`（`Polarity::REMOVE`）
- **网格重组**：`grid → select → sequence → compose → export`

> **合并重排的两个“顺序”正交**：`Sequence`（选择排序，含 CUSTOM）决定保留块的先后列表；
> `MergeOrder`（行/列优先 + 蛇形? + 倒序?）决定该列表铺进 `cols × rows` 输出画布的路径。
> **合并重排仅 L3 网格模式支持**：`runEngine` 对非 L3 的 REARRANGE、以及 L1/L2 不可坍缩的情形直接报错（不自动降级）。

---

## 3. 典型调用

```cpp
EngineConfig cfg;                       // 三层模式的统一配置
CutLineSet lines = generateCutLines(cfg.cut, cfg.source);   // ①
Grid grid        = induceGrid(lines, cfg.source);           // ②
RegionSet all    = split(image, grid);                      // split
RegionSet kept   = applyPolarity(all, selection);           // ③④
Composition comp = compose(kept, sequence, cfg.emitParams); // ⑤
exportImage(comp, image, outputPath);                       // export
// 或一次跑通（不落盘，返回保留集 + 合成描述）：
EngineResult r = runEngine(image, cfg);
```

---

## 4. 目录结构

```
ImageDiscropperCore/
├── CMakeLists.txt            构建脚本（静态库 + demo + unit_tests）
├── README.md                 本文件
├── include/                  公共头文件（对外接口）
│   ├── core/                 基础数据类型：Color / Point / Image
│   ├── geometry/             标注图形几何：ShapeType / Shape / Path / AffineTransform
│   ├── pixel_ops/            像素处理算法（颜色/通道、几何变换）
│   ├── annotation/           标注层：AnnotationLayer / ShapeFactory / Rasterizer / ViewTransform
│   ├── preprocess/           预处理流水线：PreprocessConfig / PreprocessPipeline
│   ├── history/              撤销 / 重做栈
│   └── engine/               ★ Grid-Selection-Emit 统一引擎接口
├── src/                      对应 include 的实现文件（engine 全链路真实实现）
├── examples/                 演示程序 demo_main.cpp（真正跑通引擎并导出到 demo_output/）
└── tests/                    自测 test_main.cpp + test_engine_*.cpp（引擎验收）
```

每个子目录下都有独立的 `README.md` 说明该目录的职责边界与分块依据。

---

## 5. 构建

工程使用 CMake（>= 3.28）。除标准库外依赖三个 vcpkg 第三方库，且**全部以 PRIVATE 方式接入**
（仅本库对应 `.cpp` 内部使用，不经公共头暴露给下游）：

- **stb**（header-only）：PNG/JPEG/BMP 编码 —— `find_package(Stb REQUIRED)`，仅 `image_io.cpp` 使用。
- **libwebp**：WebP 编码（补齐 stb 不支持的格式）—— `find_package(WebP CONFIG REQUIRED)` 链接 `WebP::webp`，仅 `image_io.cpp` 使用。
- **nlohmann/json**（单头）：配置 JSON 存取 —— `find_package(nlohmann_json CONFIG REQUIRED)` 链接 `nlohmann_json::nlohmann_json`，仅 `engine_config_json.cpp` 使用。

先安装依赖（`vcpkg install stb libwebp nlohmann-json`），再配置构建：

```bash
cmake -S . -B build
cmake --build build --config Release
```

构建产物：

- `libimage_discropper_core.a` / `image_discropper_core.lib` — Core 静态库
- `demo` — 演示可执行程序（`examples/demo_main.cpp`，真正跑通引擎并导出到 `demo_output/`）
- `unit_tests` — 自测（`tests/test_main.cpp` + `tests/test_engine_*.cpp`）

---

## 6. 本库特有约定

- **坐标约定**（SPEC §2.1）：整数像素、原点左上、`x` 向右 `y` 向下、区间左闭右开 `[a, b)`。
- **像素存储**：`Image` 支持 `RGB` / `RGBA` / `GRAY` 三种模式，行主序、通道交错。
- **引擎实现**：三层模式（L1/L2/L3）共用同一条代码路径，仅为参数预设（NFR-0）；
  导出编码支持 PNG/JPEG/BMP（stb）与 WebP（libwebp）四种格式。
- 全仓库统一约定（C++17、中文注释、命名风格、错误处理、分层纪律）见根 [`CONTRIBUTING.md`](../../CONTRIBUTING.md)。

---

## 7. 许可

本模块与仓库整体一致，采用 **GPL-3.0**（见仓库根 [`LICENSE`](../../LICENSE)）。
