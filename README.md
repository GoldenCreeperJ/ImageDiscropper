# ImageDiscropper

**图像区域提取、反向剔除与网格分割工具**。

> 一句话定位：常规裁剪只回答「保留哪一块」；本工具回答「**沿哪些线切开、留下哪些块、怎么重新拼起来**」。

本工程依据《图像区域提取、反向剔除与网格分割工具 · 需求规格说明（终稿 v1.0）》
（[`Require.md`](Require.md)）实现，施工图见 [`guideline.md`](guideline.md)。

---

## 1. 应用介绍

所有功能归约为同一条 **Grid-Selection-Emit** 流水线（终稿 §2）：

```text
原图 → ① 切割线集合 → ② 诱导网格 → ③ 选择集 → ④ 极性(Keep/Remove) → ⑤ 排布导出(Collapse/Rearrange)
```

**公理**：切割线是贯穿全图的直线，不是线段。三种模式在数学上是包含关系（L1 ⊂ L2 ⊂ L3），
在产品上并列呈现，但**共用同一套引擎代码**（NFR-0：模式仅为参数预设）：

| 层级 | 模式 | 定位 | CLI 命令 |
|---|---|---|---|
| **L1** | 标准提取模式 | 兼容基线（极性恒 `keep`） | `extract` |
| **L2** | 反向剔除模式 | ⭐ 差异化内核（按线删除、零伪造、无损） | `erase` |
| **L3** | 网格分割模式 | ⭐ 完备表达层（参数化网格 + 显式排序 + 重排） | `grid` |

---

## 2. 工程组成

采用终稿 §10.1 的三层结构（Core / CLI / GUI），Core 作为静态库被上层引用：

```text
GUI ──────┐
          ├── Core
CLI ──────┘
```

| 模块目录 | 层 | 产物 | 职责 | 状态 |
|---|---|---|---|---|
| [`ImageDiscropperCore/`](ImageDiscropperCore) | Core | 静态库 `image_discropper_core` | 纯逻辑：Grid-Selection-Emit 统一引擎 + 基础图像处理，无 GUI 依赖 | ✅ MVP / v2 已落地 |
| [`ImageDiscropperCli/`](ImageDiscropperCli) | CLI | 可执行 `idc` + 静态库 `idc_cli_lib` | 命令行薄壳：解析参数 → 调用 Core → 格式化输出 → 返回退出码 | ✅ 已落地 |
| `gui/`（规划中） | GUI | 桌面前端 | 交互式画布、拖拽切割线、实时预览 | ⏳ 未落地 |

顶层 [`CMakeLists.txt`](CMakeLists.txt) 只做「模块编排 + 全局约定」：统一 C++17、
按依赖顺序先引入 Core 再引入 CLI、开放 `ctest`。第三方库（stb / libwebp / nlohmann_json）
由 Core 以 PRIVATE 封装，CLI 经 Core 公共头间接使用，无需感知。

---

## 3. 与终稿核心概念的映射

终稿 §10.2 建议的 Core 概念在本工程中的落点（详见 [`ImageDiscropperCore/README.md`](ImageDiscropperCore/README.md)）：

| 终稿概念 | 类型 / 接口 | 头文件 |
|---|---|---|
| `Image` | `idc::core::Image` | `core/image.h` |
| `Region` | `idc::engine::RectRegion` / `RegionKind` | `engine/region.h` |
| `RegionSet` | `idc::engine::RegionSet` / `Fragment` | `engine/region_set.h` |
| 切割线（cut line） | `idc::engine::CutLine` / `CutLineSet` | `engine/cut_line.h` |
| `Grid` | `idc::engine::Grid` / `Cell` / `GridParams` | `engine/grid.h` |
| `Selection` | `idc::engine::Selection` / `Polarity` | `engine/selection.h` |
| `Sequence` | `idc::engine::Sequence` / `SortStrategy` | `engine/sequence.h` |
| `Composition` | `idc::engine::Composition` / `MergeLayout` | `engine/composition.h` |
| 流水线编排 | `runEngine`（`split → select → compose → export`） | `engine/engine.h` |
| 图像 I/O | `readImageFile` / `writeImageFile` | `engine/image_io.h` |
| 配置 JSON | `loadEngineConfig` / `saveEngineConfig` | `engine/engine_config_json.h` |

CLI 命令与终稿功能的对应（详见 [`ImageDiscropperCli/README.md`](ImageDiscropperCli/README.md)）：

| CLI 命令 | 层级 | 极性 | 输出 | 对应终稿 |
|---|---|---|---|---|
| `extract` | L1 标准提取 | 恒 keep | 合并单图（坍缩） | §4.2 |
| `erase` | L2 反向剔除 | 恒 remove | 分离 / 坍缩 / 重排 | §4.3（FR-L2.1~L2.6） |
| `grid` | L3 网格分割 | keep 或 remove | 分离 / 重排合并 | §4.4（FR-L3.1~L3.8） |
| `config` | 配置驱动 | 由 JSON 决定 | 由 JSON 的 emit 决定 | §9 / FR-L3.8 |

---

## 4. 构建与运行

工程使用 CMake（>= 3.28）+ vcpkg（`stb` / `libwebp` / `nlohmann-json`，均由 Core PRIVATE 接入）。
**以仓库根为 CMake 源目录**加载工程：

```bash
cmake -S . -B build            # 需配置 vcpkg 工具链
cmake --build build            # 产出 build/bin/idc(.exe) 与 build/bin/demo(.exe)
ctest --test-dir build         # 同时运行 Core unit_tests 与 CLI cli_tests（IT-1~IT-18）
```

命令行速览（完整选项见 `idc <command> --help`）：

```bash
idc extract --input photo.jpg --rect 100,100,300,250 --output out.png
idc erase   --input photo.jpg --rect 100,100,300,250 --merge collapse --output out.png
idc erase   --input photo.jpg --rect 100,100,300,250 --output-dir ./out/ --format png
idc grid    --input photo.jpg --grid 100,100,200,150 --keep 0,0 --keep 0,2 --keep 2,0 --keep 2,2 \
            --compose --canvas 2x2 --output result.png
idc config  --load my-config.json --input photo.jpg --output result.png
```

退出码（guideline §5.1）：`0` 成功 · `1` 参数错误 · `2` 运行时错误 · `3` 输入文件错误 ·
`4` 输出失败 · `5` 内部错误。

---

## 5. 文档索引

| 文档 | 说明 |
|---|---|
| [`Require.md`](Require.md) | 需求规格说明（终稿 v1.0），唯一需求基线 |
| [`guideline.md`](guideline.md) | 给 AI Agent 的 CLI 施工图（终稿的可执行化版本） |
| [`ImageDiscropperCore/README.md`](ImageDiscropperCore/README.md) | Core 层：模块划分、概念映射、构建 |
| [`ImageDiscropperCli/README.md`](ImageDiscropperCli/README.md) | CLI 层：命令、选项、退出码、Core API 清单 |

每个源码子目录下均有独立的 `README.md` 说明其职责边界与分块依据（guideline Strict Rule 1）。

---

## 6. 实现状态（对照终稿 §8 分期）

| 阶段 | 范围 | 状态 |
|---|---|---|
| **MVP** | 统一引擎 + L2 反向剔除（单矩形/横线/竖线）+ L1 标准提取 + 分离与坍缩导出 | ✅ 已落地 |
| **v2** | L3 网格分割（排序 + 重排合并）+ 配置预设、多矩形并集剔除 | ✅ 已落地 |
| **v3** | 基础图像处理（FR-1）、标注图层 | ⏳ Core 支撑层已重构保留，GUI 未落地 |

> 本地处理、无损优先（NFR-1 / NFR-2）：图像不上传服务器；切割为纯像素搬运，仅 JPEG 输出有损。
