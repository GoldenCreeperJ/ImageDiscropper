# include/engine

**目录作用**：★ **Grid-Selection-Emit 统一引擎的对外接口**，是本工具区别于常规裁剪的核心。
把 SPEC §1 的流水线落实为一组概念类型与函数声明；算法实现在 [`src/engine/`](../../src/engine/README.md)，
均已落地。SPEC 概念 → 类型 / 接口的完整映射与典型调用见 [本库 README「与功能规格核心概念的对应」](../../README.md)。

**分块依据**：按流水线阶段与概念逐一分文件，避免单文件承载全部功能（严禁 God File）。

| 文件                     | 概念 / 阶段                   | 主要内容（声明）                                                                                                                                                                                           |
|------------------------|---------------------------|----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------|
| `region.h`             | Region（SPEC §2.2/§2.3）    | `RectRegion`（左闭右开）、`RegionKind`、水平/垂直带构造                                                                                                                                                           |
| `cut_line.h`           | ① 切割线（SPEC §1）            | `CutOrientation`、`CutLine`、`CutLineSet`、`normalizeCutLines`                                                                                                                                        |
| `region_set.h`         | RegionSet（SPEC §2.2）      | `Fragment`（区域片段 + 单元序号）、`RegionSet`（面积/包围盒统计）                                                                                                                                                      |
| `grid.h`               | ② 诱导网格（SPEC §3.4.3）       | `GridParams`（仅基准点 + 单元尺寸，行列数由图像边界自动推导）、`RemainderPolicy`、`Cell`、`Grid`（`build` 周期铺满全图 / `buildFromLines`）                                                                                          |
| `split.h`              | split 切分                  | `split`：按网格切分为 `RegionSet`，跳过空块（E-4）                                                                                                                                                               |
| `selection.h`          | ③④ 选择集 + 极性               | `Polarity`（keep/remove）、`Selection`（点选/全选/反选、`resolve`）、`applyPolarity`（过滤得保留集 R）                                                                                                                  |
| `sequence.h`           | 顺序（SPEC §3.4.1）           | `SortStrategy`、`SequenceParams`、`Sequence`（`build`：row/col-major + reverse + snake）                                                                                                                |
| `composition.h`        | ⑤ 排布导出（SPEC §4）           | `EmitMode`、`MergeLayout`、`MergeOrder`（重排填充顺序，与选择排序正交）、`ExportFormat`、`CompositionParams`、`Composition`、`isCollapsible`、`compose`                                                                   |
| `export.h`             | export 导出（SPEC §4.1/§4.5） | `exportImage`：分离多图写入文件夹（自动创建，E-8）/ 合并单图                                                                                                                                                            |
| `engine.h`             | 顶层配置 + 流水线 facade         | `Tier`、`CutGenerator`、`CutConfig`、`EngineConfig`、`EngineResult`；`generateCutLines` / `induceGrid` / `runEngine`，并聚合 include 各阶段头（`split` / `applyPolarity` / `compose` / `exportImage` 声明已下沉至对应子头） |
| `image_io.h`           | 图像编解码 I/O（SPEC §4.5）      | `readImageFile`（解码为 RGBA，stb_image）/ `writeImageFile` / `encodeImageToMemory`（编码：stb PNG/JPEG/BMP；libwebp WebP）                                                                                    |
| `engine_config_json.h` | 配置存取（FR-L3.8/NFR-4）       | `saveEngineConfig` / `loadEngineConfig`（内部用 nlohmann/json，依赖不外泄）                                                                                                                                   |
