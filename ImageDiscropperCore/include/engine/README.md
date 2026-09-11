# include/engine

**目录作用**：★ **Grid-Selection-Emit 统一引擎的接口骨架**，是本工具区别于常规裁剪的核心。
它把终稿 §2 的流水线「原图 → ① 切割线 → ② 诱导网格 → ③ 选择集 → ④ 极性 → ⑤ 排布导出」
落实为一组概念类型与函数声明，覆盖 §10.2 建议的 Core 概念
（`Region` / `RegionSet` / `Grid` / `Selection` / `Sequence` / `Composition`）。

> **重要边界**：本目录当前只提供**数据结构**与**函数声明**。数据结构（区域、切割线、
> 选择集位操作、序列容器、合成参数等）已可用；而切割 / 剔除 / 网格铺设 / 排序 / 合成 /
> 导出等**算法为桩实现**（返回空结果 + `TODO`），真正实现属于新项目 MVP / v2 / v3 阶段，
> **不在本次「适配既有项目」的范围内**。三层模式共用此骨架，模式仅为参数预设（NFR-0）。

**分块依据**：按流水线阶段与概念逐一分文件，避免单文件承载全部功能。

| 文件 | 概念 / 阶段 | 主要内容 | 状态 |
|---|---|---|---|
| `region.h` | Region（§3.2/§3.3） | `RectRegion`（左闭右开）、`RegionKind`、水平/垂直带构造 | ✅ 数据结构 |
| `cut_line.h` | ① 切割线（§2） | `CutOrientation`、`CutLine`、`CutLineSet`、`normalizeCutLines` | ✅ 结构 / ⛔ 归一化 |
| `region_set.h` | RegionSet（§3.2） | `Fragment`（区域片段）、`RegionSet`（面积/包围盒统计） | ✅ 数据结构 |
| `grid.h` | ② 诱导网格（§4.4） | `GridParams`、`RemainderPolicy`、`Cell`、`Grid` | ✅ 结构 / ⛔ `build` |
| `selection.h` | ③④ 选择集 + 极性 | `Polarity`（keep/remove）、`Selection`（点选/全选/反选） | ✅ 结构 / ⛔ `resolve` |
| `sequence.h` | 顺序（§4.4.1） | `SortStrategy`、`SequenceParams`、`Sequence` | ✅ 结构 / ⛔ `build` |
| `composition.h` | ⑤ 排布导出（§5） | `EmitMode`、`MergeLayout`、`ExportFormat`、`CompositionParams`、`Composition`、`isCollapsible` | ✅ 结构 / ⛔ 判定 |
| `engine.h` | 顶层配置 + 流水线 | `Tier`、`CutGenerator`、`CutConfig`、`EngineConfig`，及 `generateCutLines` / `induceGrid` / `split` / `applyPolarity` / `compose` / `exportImage` | ⛔ 接口骨架 |

**预期调用流程**（终稿 §10.2，待 MVP 实现后生效）：

```text
EngineConfig cfg;                       // 三层模式的统一配置
CutLineSet lines = generateCutLines(cfg.cut, cfg.source);   // ①
Grid grid        = induceGrid(lines, cfg.source);           // ②
RegionSet all    = split(image, grid);                      // split
RegionSet kept   = applyPolarity(all, selection);           // ③④
Composition comp = compose(kept, sequence, cfg.emit);       // ⑤
exportImage(comp, image, outputPath);                       // export
```
