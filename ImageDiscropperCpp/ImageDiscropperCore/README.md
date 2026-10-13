# ImageDiscropperCore

`ImageDiscropperCore` 是「**图像区域提取、反向剔除与网格分割工具**」的 **Core 层**
（纯逻辑静态库，命名空间 `idc`）。它是功能规格 [`../SPEC.md`](../../SPEC.md) 所述三层架构
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
- **② 复用支撑层（前置能力）**：`core` / `preprocess` / `geometry` / `annotation` / `canvas` / `generate`。
  由旧库重构保留，服务于 SPEC §3.1 **FR-1「基础图像处理」前置层**，
  多属 v3（标注图层最重、最后做）。它们是引擎的输入准备与辅助，**不是**主体。

| 模块           | 命名空间              | 层      | 职责                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                | 对应规格                               |
|--------------|-------------------|--------|-----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------|------------------------------------|
| `engine`     | `idc::engine`     | ★ 核心   | Grid-Selection-Emit 统一引擎（切割/网格/选择/合成/导出/JSON 全实现）                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                 | SPEC §1 全部核心概念                     |
| `core`       | `idc::core`       | 支撑（地基） | 基础数据类型：`Color` / `Point2D` / `Image`（含迁入的通用像素原语 `resized`/`scaled`/`colorAt` 与 `toGray`、`compositeOver` source-over 合成核）+ 泛型撤销 / 重做栈 `HistoryManager<T>`（原 `history` 模块并入，现仅供 GUI 文档参数撤销 `HistoryManager<EngineConfig>` 复用；标注会话已改在 `AnnotationSet` 上自行编排）+ 泛型有序步骤队列 `Pipeline<T>`（与元素类型无关的「按序增删移改 + 越界钳制/忽略」容器骨架，现由 `preprocess::PreprocessPipeline` 公开继承复用）                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                      | SPEC §2.2 `Image` / FR-1.5 / NFR-4 |
| `preprocess` | `idc::preprocess` | 支撑     | 切割前的**声明式有序步骤队列**：`PreprocessPipeline`（**公开继承 `core::Pipeline<PreprocessConfig>`** 承载存储 + CRUD；灰度/反色/色道分离/旋转/翻转/**尺寸 resize**——不透明度**已不再是像素预处理步**（旧 `OpacityOp` 已从变体彻底删除，上移为图层合成期字段 `canvas::ImageTransform.opacity`，使灰度层不透明度不再因 GRAY 无 alpha 而静默失效））+ 纯元数据 `geometryOutput` 几何推演 + 与逐像素几何同口径的矩形/增量映射 `mapRectGeometry`/`mapDeltaToInput`（供 GUI 图层落位框跨画布/工作图坐标系）+ 逐像素等价的执行层融合入口 `applyExec`（色彩折单次 `channelMap` f(v)=(v&K)^J、几何折单次 D4 `remapD4`；私有像素核 `pixel_kernels`）                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                             | SPEC §7 `preprocess` 段 / FR-1      |
| `geometry`   | `idc::geometry`   | 支撑     | 标注图形几何：`ShapeType` / `Shape` / `Path` / `AffineTransform`                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                         | FR-1.3 标注几何                        |
| `annotation` | `idc::annotation` | 支撑     | 标注集合：`AnnotationSet` / `ShapeFactory` / `Rasterizer` / `ViewTransform`（编辑会话态已拆至 GUI `AnnotationEditor`）                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                           | FR-1.3 标注图层（可烧录）                   |
| `canvas`     | `idc::canvas`     | 支撑     | **多层画布数据模型与合成**（纯逻辑、零 GUI、无 IO）：`LayerSourceKind`（Empty/Recipe/File）+ `ImageTransform`（画布锚点 + 图片枢轴 + 自由 `x,y` + 目标宽高 + **不透明度 `opacity`**（合成期 alpha 乘子 `a'=(a·opacity+127)/255`，缺省 255＝不透明，灰度层亦生效））+ `ImageLayer`（来源 + 落位 + `zOrder` + 逐层 `PreprocessPipeline`）+ `materializeLayer`（整层位图：组装含底色框→缩放→最后跑逐层 `preprocess`）+ `composeCanvas`（收 `std::vector<ImageLayer>` 数组、依**底图 base→叠加层**顺序展平：画布无级背景、恒铺全透明底；可选末位参 `base`（非空则 source-over 铺 (0,0)，它是画布本身）；叠加层按 `zOrder` 升序逐层物化（已含缩放与逐层预处理）→source-over 落位到 `w×h` 缓冲、越界自然裁剪）。另提供公共 `applyAlpha(core::Image&, std::uint8_t)`（对 **RGBA** 图逐像素乘 alpha `a'=(a·opacity+127)/255`、非 RGBA 无操作）供上层在**合成边界**对整张底图施加整体淡出（`composeBase` **不烘焙**底图 `transform.opacity`——placement 路传 `opacity=255` 规范化副本，避免与边界双淡）；`composeBase` 纯图案路对 `base.preprocess` **施一次色彩预处理**（`applyExec`，仅底图色彩步合法）；画布级整体不透明度 `EngineConfig::canvasOpacity` 与底图透明度同理**不入位图、由上层在产出边界各经 `applyAlpha` 施加一次**。多图层是「画布层」而非「引擎层」：`composeCanvas` 产物即喂 `runEngine` 的展平工作图，引擎零改动；source-over 与 `generate` 共用 `core::compositeOver` | SPEC §7 `layers` 段（多层画布）           |
| `generate`   | `idc::generate`   | 支撑     | 程序化图案生成（「新建画布」画布内容工厂）：`makePattern` 依 `PatternParams`（底色含 alpha / 前景色 / 周期 / 朝向 / 线宽 / 点半径 / `seed` / `noiseMode`）产出 RGBA 图（纯色/线性渐变/径向渐变/棋盘/条纹/网格线/点阵/同心环/放射线/色相渐变/噪声）；斜纹已并入条纹（`Orientation` 4 向含↘↗）、同心方框已并入同心环（`Orientation::Vertical`＝方环否则圆环）。噪声按 `seed`+尺寸经确定性整数哈希逐像素生成（同种子恒同图，供 C++/Rust 对拍），`noiseMode`（FullColor/Grayscale/TwoColor）仅对噪声生效。无输入图（「贴图图案」`PatternKind::Image` 与 `ImageScale` 已彻底移除，图片改由 GUI 侧「附加图片」交互入口按落点归入**底图图片**（入底图、走 `generate` 段）或**图层图片**（作 `file` 层入图层栈），经 `canvas::composeCanvas` 两段合成承载）。**总像素护栏**：单边 > `kMaxDimension(65535)` 或 宽×高 > `kMaxTotalPixels(64,000,000≈256MB RGBA)` 均在分配像素前短路回空图（不至 `bad_alloc`），调用方据 `empty()` 判失败。供 GUI（`EngineBridge::generatePattern`）与 CLI `new` / 各命令 `--size` 生成源 / `config --load` 据 `generate` 段生成共用                                                                                                                                                                                                                                                           | —（新建/生成源能力）                        |

> `examples/`（演示程序）与 `tests/`（极简自测）分别验证库的典型用法与关键行为。
> 注：表中「层」表达**主次**（`engine` 为主体）；§4 的目录顺序仅为**物理布局**，二者不必一致。

---

## 2. 与功能规格核心概念的对应

SPEC §1 流水线概念在本库中的落点如下：

| 规格概念                       | 本库类型 / 接口                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                            | 头文件                           | 状态                                                                                                                                                                                                 |
|----------------------------|--------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------|-------------------------------|----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------|
| `Image`                    | `idc::core::Image`                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                   | `core/image.h`                | ✅ 已实现                                                                                                                                                                                              |
| `Region`                   | `idc::engine::RectRegion` / `RegionKind`                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                             | `engine/region.h`             | ✅ 数据结构                                                                                                                                                                                             |
| `RegionSet`                | `idc::engine::RegionSet` / `Fragment`                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                | `engine/region_set.h`         | ✅ 数据结构                                                                                                                                                                                             |
| 切割线（cut line）              | `idc::engine::CutLine` / `CutLineSet`                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                | `engine/cut_line.h`           | ✅ 数据结构                                                                                                                                                                                             |
| `Grid`                     | `idc::engine::Grid` / `Cell` / `GridParams`                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                          | `engine/grid.h`               | ✅ 已实现（`build()` / `induceGrid()`）                                                                                                                                                                  |
| `Selection`                | `idc::engine::Selection` / `Polarity`                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                | `engine/selection.h`          | ✅ 已实现（`resolve()` / `applyPolarity()`）                                                                                                                                                             |
| `Sequence`                 | `idc::engine::Sequence` / `SortStrategy`                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                             | `engine/sequence.h`           | ✅ 已实现（`build()`）                                                                                                                                                                                   |
| `Composition`              | `idc::engine::Composition` / `MergeLayout` / `MergeOrder`                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                            | `engine/composition.h`        | ✅ 已实现（`isCollapsible()` / `compose()`）                                                                                                                                                             |
| 流水线 `split/compose/export` | `generateCutLines` / `induceGrid` / `split` / `splitSize` / `applyPolarity` / `compose` / `exportImage` / `runEngine`（图像入口）/ `runEngineGeometry`（仅尺寸预览入口）                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                            | `engine/engine.h`             | ✅ 已实现（顶层编排 `runEngine`；预览可走无像素 `runEngineGeometry`）                                                                                                                                                |
| 图像编码 I/O                   | `writeImageFile` / `encodeImageToMemory`                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                             | `engine/image_io.h`           | ✅ 已实现（stb：PNG/JPEG/BMP；libwebp：WebP）；路径按 UTF-8 解析（`pathFromUtf8`），支持中文 / 非 ASCII                                                                                                                   |
| 多层画布图层                     | `idc::canvas::ImageLayer` / `materializeLayer` / `composeCanvas` / `composeBase`                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                     | `canvas/layer.h`              | ✅ 已实现（z 序合成 / source-over / 越界裁剪 / 锚点+枢轴+自由落位 / 逐层预处理；`composeBase` 展平同构底图、对 `base.preprocess` 施一次色彩预处理但**不烘焙底图整体 `opacity` 与画布级 `canvasOpacity`**；公共 `applyAlpha` 供上层在合成/产出边界对底图透明度、画布透明度各施加一次淡出） |
| 配置 JSON                    | `saveEngineConfig` / `loadEngineConfig`（可选标注层参：SPEC §7 `annotations` 段随配置往返；`annotations` 段非法时按元素序逐条拼好报因经第 4 出参 `std::string* annoErr` 回传（专为 CLI 报因而设，可空；GUI 不消费、传 `nullptr`）；`EngineConfig` 含可选底图 `base`（`std::optional<canvas::ImageLayer>`，与图层同构、**JSON 键仍名 `generate`**：纯图案底与旧 `generate` 逐字段等价，新建画布附图片底额外携 `fit`/`box*`/`image*`/`backdrop` 落位声明（**只存落位、不存 `file` 路径**，位图运行时由 `--input` 供），与**底图整体不透明度 `opacity`**（`base.transform.opacity`、缺省 255、不入位图、合成边界施加）+ **底图色彩预处理 `preprocess`**（`base.preprocess`、仅色彩步合法、`baseToJson` 写出过滤几何/resize、缺省空则省略键））+ 顶层可选 `opacity` 键（**画布级整体不透明度 `EngineConfig::canvasOpacity`**、缺省 255、作用于合成+画布预处理后的整幅工作图、`applyAlpha` 边界单次施加、影响导出）与可选 `layers` 段（`std::optional<std::vector<canvas::ImageLayer>>`，文件层仅存相对名、位图不入配置、缺文件降级占位）；序列化/反序列化与 `new --save-config` / `config --load` 生成源 / 图层注入共用） | `engine/engine_config_json.h` | ✅ 已实现（nlohmann/json）                                                                                                                                                                               |
| 标注图层（burn-in）              | `idc::annotation::AnnotationSet::burnIn(base)` / `restore`                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                           | `annotation/annotation_set.h` | ✅ 已实现                                                                                                                                                                                              |

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

> 多层画布：上层先把 `cfg.layers` 里的叠加文件层像素解码注入，底图 base（同构 `canvas::ImageLayer`）由 `--input`/`--size`/`generate` 段（或 GUI 已开原图）得来——纯图案底 `composeBase` 按 source 尺寸现算、附图片落位声明底由 `--input` 供位图经 `placeInputIntoBase`（置 `File` 源后转 `composeBase`→`composeCanvas({base},w,h)`）按落位合成，再 `canvas::composeCanvas(layers, w, h, base)`
> 依底图→叠加层（画布无级背景、恒铺全透明底）得展平工作图，交 `runEngine`（引擎签名与逻辑不变；`base` 缺省为空图，保持旧行为）。

---

## 4. 目录结构

```
ImageDiscropperCore/
├── CMakeLists.txt            构建脚本（静态库 + demo + unit_tests）
├── README.md                 本文件
├── include/                  公共头文件（对外接口）
│   ├── core/                 基础数据类型：Color / Point / Image（含通用像素原语 resized/scaled/colorAt/compositeOver）+ 泛型撤销/重做栈 HistoryManager（原 history 并入）+ 泛型有序队列 Pipeline
│   ├── i18n/                 三层共用 i18n 内核 core_i18n.h（零 Qt）+ 字符串常量层 core_texts.h
│   ├── geometry/             标注图形几何：ShapeType / Shape / Path / AffineTransform
│   ├── annotation/           标注集合：AnnotationSet / ShapeFactory / Rasterizer / ViewTransform
│   ├── preprocess/           预处理流水线：PreprocessConfig / PreprocessPipeline（像素核 pixel_kernels 为 src 私有）
│   ├── canvas/               多层画布数据模型与合成：ImageLayer / materializeLayer / composeCanvas（零 GUI、无 IO）
│   ├── generate/             程序化图案生成：makePattern / PatternKind / PatternParams（「新建画布」画布工厂，无输入图）
│   └── engine/               ★ Grid-Selection-Emit 统一引擎接口
├── src/                      对应 include 的实现文件（engine 全链路真实实现）
├── examples/                 演示程序 demo_main.cpp（真正跑通引擎并导出到 demo_output/）
└── tests/                    自测 test_main.cpp + test_engine_*.cpp（引擎验收）+ test_i18n_*.cpp（i18n 内核与目录对账）
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
- `unit_tests` — 自测（`tests/test_main.cpp` + `tests/test_engine_*.cpp` + `tests/test_i18n_*.cpp`）

---

## 6. 本库特有约定

- **坐标约定**（SPEC §2.1）：整数像素、原点左上、`x` 向右 `y` 向下、区间左闭右开 `[a, b)`。
- **像素存储**：`Image` 支持 `RGB` / `RGBA` / `GRAY` 三种模式，行主序、通道交错。
- **引擎实现**：三层模式（L1/L2/L3）共用同一条代码路径，仅为参数预设（NFR-0）；
  导出编码支持 PNG/JPEG/BMP（stb）与 WebP（libwebp）四种格式。
- **路径编码**：所有文件打开（`readImageFile`/`writeImageFile`、配置读写、导出）以 **UTF-8** 解析路径（`core::pathFromUtf8` 开、`pathToUtf8` 回，取代 C++23 已移除的 `u8path`），解码走内存、编码走 `ofstream`，绕开 stb/`fopen` 的窄字符（Windows ANSI 码页）限制——中文 / 非 ASCII 路径可读写；上游（GUI `QString::toStdString` / CLI 入口 `__wargv`→UTF-8）须传 UTF-8。
- **多层画布是画布层**：`runEngine` 永远只收**一张展平的画布图 + 空 preprocess**；图层栈的合成
  （`composeCanvas`）与逐层预处理在 Core 内完成、不入引擎。图层资源以相对名引用、位图不入配置
  （缺文件降级为占位），与 CLI `--bundle-assets` 同构。
- **i18n 内核住本层、译文表不住**：`i18n/core_i18n.h`（零 Qt）持有唯一的**进程级语言状态**（已装语言表 +
  当前语言），GUI / CLI / Core 三层共用同一张扁平表（键 = `分组::常量名`）；Core 只放自己的报因与图案名表
  （`src/i18n/lang_en.cpp`），**GUI / CLI 的文案不进静态库**，仍住各自层的 `lang_en.cpp`。
  源语言 zh-CN **不建表**（全部回退常量层原文），故中文路径逐字节零行为变化。机制详见根
  [`../README.md`](../README.md)「三层共用的 i18n」与 [include/i18n/README.md](include/i18n/README.md)。
- 全仓库统一约定（C++23、中文注释、命名风格、错误处理、分层纪律）见根 [`CONTRIBUTING.md`](../../CONTRIBUTING.md)。

---

## 7. 许可

本模块与仓库整体一致，采用 **GPL-3.0**（见仓库根 [`LICENSE`](../../LICENSE)）。
