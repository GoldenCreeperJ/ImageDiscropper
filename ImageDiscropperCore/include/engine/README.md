# include/engine

**目录作用**：★ **Grid-Selection-Emit 统一引擎的对外接口**，是本工具区别于常规裁剪的核心。
它把终稿 §2 的流水线「原图 → ① 切割线 → ② 诱导网格 → ③ 选择集 → ④ 极性 → ⑤ 排布导出」
落实为一组概念类型与函数声明，覆盖 §10.2 建议的 Core 概念
（`Region` / `RegionSet` / `Grid` / `Selection` / `Sequence` / `Composition`）。

> **实现状态**：本目录提供引擎全部**数据结构**与**函数声明**，其算法定义分散在
> `src/engine/` 各阶段文件并**均已落地为真实实现**——切割线生成 / 网格铺设（以基准点为相位锚周期生成贯穿线，行列数自动推导）/ 切分 /
> 选择极性 / 排序 / 合成（分离·坍缩·重排）/ 图像解码（stb_image → RGBA）/ 导出（stb+libwebp 编码，分离多图写入文件夹）/ 配置 JSON 存取。
> 三层模式（L1/L2/L3）共用此同一代码路径，模式仅为参数预设（NFR-0）。

**分块依据**：按流水线阶段与概念逐一分文件，避免单文件承载全部功能（严禁 God File）。

| 文件 | 概念 / 阶段 | 主要内容 | 状态 |
|---|---|---|---|
| `region.h` | Region（§3.2/§3.3） | `RectRegion`（左闭右开）、`RegionKind`、水平/垂直带构造 | ✅ 数据结构 |
| `cut_line.h` | ① 切割线（§2） | `CutOrientation`、`CutLine`、`CutLineSet`、`normalizeCutLines` | ✅ 结构 + 归一化 |
| `region_set.h` | RegionSet（§3.2） | `Fragment`（区域片段 + 单元序号）、`RegionSet`（面积/包围盒统计） | ✅ 数据结构 |
| `grid.h` | ② 诱导网格（§4.4.4） | `GridParams`（仅基准点 + 单元尺寸，行列数由图像边界自动推导）、`RemainderPolicy`、`Cell`、`Grid`（`build` 周期铺满全图 / `buildFromLines`） | ✅ 结构 + 铺设 |
| `split.h` | split 切分（§10.2） | `split`：按网格切分为 `RegionSet`，跳过空块（E-4） | ✅ 已实现 |
| `selection.h` | ③④ 选择集 + 极性 | `Polarity`（keep/remove）、`Selection`（点选/全选/反选、`resolve`）、`applyPolarity`（过滤得保留集 R） | ✅ 结构 + 解析 |
| `sequence.h` | 顺序（§4.4.1） | `SortStrategy`、`SequenceParams`、`Sequence`（`build`：row/col-major + reverse + snake） | ✅ 结构 + 排序 |
| `composition.h` | ⑤ 排布导出（§5） | `EmitMode`、`MergeLayout`、`MergeOrder`（重排填充顺序：行/列优先 + 蛇形 + 倒序，与选择排序正交）、`ExportFormat`、`CompositionParams`、`Composition`、`isCollapsible`、`compose` | ✅ 结构 + 判定 + 合成 |
| `export.h` | export 导出（§5.1/§5.5） | `exportImage`：分离多图写入文件夹（自动创建，E-8）/ 合并单图 | ✅ 已实现 |
| `engine.h` | 顶层配置 + 流水线 facade | `Tier`、`CutGenerator`、`CutConfig`、`EngineConfig`、`EngineResult`；直接声明桥接配置的 `generateCutLines` / `induceGrid` 与 `runEngine`，并聚合 include 各阶段头（`split` / `applyPolarity` / `compose` / `exportImage` 声明已下沉至对应子头） | ✅ 接口 + 编排 |
| `image_io.h` | 图像编解码 I/O（§5.5） | `readImageFile`（解码为 RGBA，stb_image）/ `writeImageFile` / `encodeImageToMemory`（编码：stb PNG/JPEG/BMP；libwebp WebP） | ✅ 已实现 |
| `engine_config_json.h` | 配置存取（FR-L3.8/NFR-4） | `saveEngineConfig` / `loadEngineConfig`（内部用 nlohmann/json，依赖不外泄） | ✅ 已实现 |

> **两个正交的“顺序”概念**（易混淆，务必区分）：
> - **选择排序** `SequenceParams` / `Sequence`（`sequence.h`）：决定保留块的**先后列表**（含 CUSTOM 自定义拖拽序）。
> - **重排填充顺序** `MergeOrder`（`composition.h`）：决定该列表以**行优先/列优先 + 蛇形? + 倒序?** 的路径铺进
>   `cols × rows` 输出画布的哪些槽位（strategy 只取 ROW_MAJOR / COLUMN_MAJOR，CUSTOM 在 `compose` 中按 ROW_MAJOR 处理）。
> - **合并重排（`MergeLayout::REARRANGE`）仅 L3 网格模式支持**：`runEngine` 对非 L3 的 REARRANGE 直接报错（不降级）；
>   L1/L2 选坍缩但保留集不可坍缩（§5.4）时亦直接报错，提示改用分离导出。

**调用流程**（终稿 §10.2，`runEngine` 内部即按此编排；亦可逐阶段调用）：

```text
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

> **格式支持**：解码统一加载为 RGBA（stb_image，支持 PNG/JPEG/BMP/WebP/GIF/TGA 等）；
> 导出编码支持 PNG/JPEG/BMP（stb）与 WebP（libwebp）四种格式，
> 由 `ExportFormat` 选择；第三方库封装在 `image_io.cpp`，公共头不暴露其类型。
