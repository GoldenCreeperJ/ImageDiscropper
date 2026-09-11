# ImageDiscropperCore

`ImageDiscropperCore` 是「**图像区域提取、反向剔除与网格分割工具**」的 **Core 层**
（纯逻辑静态库，命名空间 `idc`）。它对应终稿需求文档 `Require.md` §10.1 所述三层架构
（Core / CLI / GUI）中的 **Core**，不依赖任何 GUI，可被上层 CLI 与 GUI 直接引用。

> 本库由早期的图像绘制与处理逻辑库重构而来：**保留并重定位**了其中可复用的
> 基础图像处理与标注能力（对应终稿 FR-1「基础图像处理」前置层），并**新增了
> Grid-Selection-Emit 引擎的接口骨架**（数据结构 + 声明），为新项目后续开发铺路。
> 引擎的切割 / 剔除 / 网格 / 导出**算法尚未实现**（严格属于新项目 MVP 及后续阶段）。

---

## 1. 应用介绍

常规裁剪只回答「保留哪一块」；本工具回答「**沿哪些线切开、留下哪些块、怎么重新拼起来**」。
所有功能归约为同一条流水线（终稿 §2）：

```text
原图 → ① 切割线集合 → ② 诱导网格 → ③ 选择集 → ④ 极性 → ⑤ 排布导出
```

三种模式在数学上是包含关系（L1 ⊂ L2 ⊂ L3），在产品上并列呈现，但**共用同一套引擎代码**
（NFR-0：模式仅为参数预设）：

| 层级 | 模式 | 定位 |
|---|---|---|
| **L1** | 标准提取模式 | 兼容基线（极性恒为 `keep`） |
| **L2** | 反向剔除模式 | ⭐ 差异化内核（按线删除、零伪造、无损） |
| **L3** | 网格分割模式 | ⭐ 完备表达层（参数化网格 + 显式排序 + 重排） |

---

## 2. 库的组成（模块划分）

代码按**功能 / 任务**分目录、分文件；`include/` 为对外接口，`src/` 为一一对应的实现。

| 模块 | 命名空间 | 职责 | 对应终稿 |
|---|---|---|---|
| `core` | `idc::core` | 基础数据类型：`Color` / `Point2D` / `Image` | §10.2 `Image` |
| `geometry` | `idc::geometry` | 标注图形几何：`ShapeType` / `Shape` / `Path` | FR-1.3 标注几何 |
| `processing` | `idc::processing` | 像素处理：颜色/通道、旋转/翻转/缩放 | FR-1.1 / FR-1.2 / FR-1.4 |
| `annotation` | `idc::annotation` | 标注层：`AnnotationLayer` / `Rasterizer` / `ViewTransform` | FR-1.3 标注图层（可烧录） |
| `preprocess` | `idc::preprocess` | 切割前的基础图像处理流水线：`PreprocessPipeline` | §9 `preprocess` 段 |
| `history` | `idc::history` | 泛型撤销 / 重做栈：`HistoryManager<T>` | FR-1.5 / NFR-4 |
| `engine` | `idc::engine` | ★ Grid-Selection-Emit 引擎接口骨架（数据结构 + 声明） | §2 / §10.2 全部核心概念 |

`examples/`（演示程序）与 `tests/`（极简自测）分别验证库的典型用法与关键行为。

---

## 3. 与终稿核心概念的对应

终稿 §10.2 建议的 Core 概念，在本库中的落点如下：

| 终稿概念 | 本库类型 / 接口 | 头文件 | 状态 |
|---|---|---|---|
| `Image` | `idc::core::Image` | `core/image.h` | ✅ 已实现 |
| `Region` | `idc::engine::RectRegion` / `RegionKind` | `engine/region.h` | ✅ 数据结构 |
| `RegionSet` | `idc::engine::RegionSet` / `Fragment` | `engine/region_set.h` | ✅ 数据结构 |
| 切割线（cut line） | `idc::engine::CutLine` / `CutLineSet` | `engine/cut_line.h` | ✅ 数据结构 |
| `Grid` | `idc::engine::Grid` / `Cell` / `GridParams` | `engine/grid.h` | 🟡 结构就绪，`build()` 待实现 |
| `Selection` | `idc::engine::Selection` / `Polarity` | `engine/selection.h` | 🟡 结构就绪，`resolve()` 待实现 |
| `Sequence` | `idc::engine::Sequence` / `SortStrategy` | `engine/sequence.h` | 🟡 结构就绪，`build()` 待实现 |
| `Composition` | `idc::engine::Composition` / `MergeLayout` | `engine/composition.h` | 🟡 结构就绪，`isCollapsible()` 待实现 |
| 流水线 `split/remove/compose/export` | `generateCutLines` / `induceGrid` / `split` / `applyPolarity` / `compose` / `exportImage` | `engine/engine.h` | ⛔ 仅声明，桩实现（MVP 起） |
| 标注图层（burn-in） | `idc::annotation::AnnotationLayer::burnIn()` | `annotation/annotation_layer.h` | ✅ 已实现 |

> 图例：✅ 可直接使用；🟡 数据结构可用、算法待实现；⛔ 接口骨架（桩实现返回空结果 + `TODO`）。

三种模式即引擎的不同参数预设（终稿 §10.2）：

- **标准提取**：`split → select → keep → export`（`Polarity::KEEP`）
- **反向剔除**：`split → select → remove → export`（`Polarity::REMOVE`）
- **网格重组**：`grid → select → sequence → compose → export`

---

## 4. 目录结构

```
ImageDiscropperCore/
├── CMakeLists.txt            构建脚本（静态库 + demo + unit_tests）
├── README.md                 本文件
├── include/                  公共头文件（对外接口）
│   ├── core/                 基础数据类型：Color / Point / Image
│   ├── geometry/             标注图形几何：ShapeType / Shape / Path
│   ├── processing/           像素处理算法（颜色/通道、几何变换）
│   ├── annotation/           标注层：AnnotationLayer / Rasterizer / ViewTransform
│   ├── preprocess/           预处理流水线：PreprocessConfig / PreprocessPipeline
│   ├── history/              撤销 / 重做栈
│   └── engine/               ★ Grid-Selection-Emit 引擎接口骨架
├── src/                      对应 include 的实现文件（含 engine 桩实现）
├── examples/                 演示程序 demo_main.cpp
└── tests/                    极简自测 test_main.cpp
```

每个子目录下都有独立的 `README.md` 说明该目录的职责边界与分块依据。

---

## 5. 构建

工程使用 CMake（>= 3.28），仅依赖标准库，无第三方库：

```bash
cmake -S . -B build
cmake --build build --config Release
```

构建产物：

- `libimage_discropper_core.a` / `image_discropper_core.lib` — Core 静态库
- `demo` — 演示可执行程序（`examples/demo_main.cpp`）
- `unit_tests` — 极简自测（`tests/test_main.cpp`）

---

## 6. 代码约定

- **语言标准**：C++17（结构化绑定、`std::optional`、`std::variant`）。
- **注释语言**：全部使用中文注释；每个文件头说明文件作用与分块依据，每个函数首说明用途。
- **命名风格**：类型 `PascalCase`；函数 / 变量 `camelCase`；常量 `kConstantName`；命名空间 `lowercase`。
- **坐标约定**（终稿 §3.1）：整数像素、原点左上、`x` 向右 `y` 向下、区间左闭右开 `[a, b)`。
- **错误处理**：内部算法通过返回值 / `std::optional` 传递失败；对外接口不抛异常。
- **像素存储**：`Image` 支持 `RGB` / `RGBA` / `GRAY` 三种模式，行主序、通道交错。
- **引擎骨架**：`engine` 模块的算法函数目前为桩实现（返回空结果 + `TODO`），
  真正实现属于新项目 MVP / v2 / v3 阶段，不在本库当前范围内。
