# src/engine

**目录作用**：`include/engine` 下「算法型」接口的实现代码集合——切割线生成与归一化 / 网格铺设与诱导
（以基准点为相位锚周期生成贯穿线，行列数自动推导）/ 切分 / 选择极性 / 排序 / 合成（分离·坍缩·重排）/
导出（编码落盘，多图写入文件夹）/ 配置 JSON 存取 / 顶层编排，覆盖 SPEC §1 全流水线。
图像编解码（stb_image 解码 / stb+libwebp 编码）位于 [`src/io/`](../io/README.md)。

**分块依据**：与 `include/engine` 头文件一一对应，并按流水线阶段拆分——每个 `.cpp` 只承载
一个阶段的算法，且只 include 自身阶段头（`split.cpp`→`split.h`、`selection.cpp`→`selection.h`、
`composition.cpp`→`composition.h`、`export.cpp`→`export.h`），而不全量依赖 `engine.h` facade（解耦）；
`engine.cpp` 仅保留顶层编排 `runEngine`。`generateCutLines` / `induceGrid` 因桥接聚合配置
（`CutConfig` / `SourceInfo`）声明留在 `engine.h`，故 `cut_line.cpp` / `grid.cpp` 仍 include 之。避免上帝文件（严禁 God File）。

| 文件                              | 职责                                                                                                                                                                                                                                                                                                                                                | 阶段     |
|---------------------------------|---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------|--------|
| `cut_line.cpp`                  | `normalizeCutLines`（去重/升序/裁到边界，E-2）+ `generateCutLines`（Rect/横线/竖线/多矩形/网格 5 生成器，E-1/E-3）                                                                                                                                                                                                                                                          | ①      |
| `grid.cpp`                      | `Grid::build`（基准点为相位锚周期铺满全图，行列数自动推导；余量策略 Discard/KeepPartial/Pad，E-5）+ `buildFromLines` + `induceGrid`                                                                                                                                                                                                                                            | ②      |
| `split.cpp`                     | `split`：遍历网格单元切分，跳过空块（E-4），产出带单元序号的 `RegionSet`                                                                                                                                                                                                                                                                                                   | split  |
| `selection.cpp`                 | `Selection::resolve`（Keep→选中 / Remove→补集）+ `applyPolarity`（过滤得保留集 R）                                                                                                                                                                                                                                                                              | ③④     |
| `sequence.cpp`                  | `Sequence::build`：row-major / column-major + reverse + snake（Custom 由 `setCustom` 提供）                                                                                                                                                                                                                                                             | 排序     |
| `composition.cpp`               | `isCollapsible`（SPEC §4.4 定理）+ `compose`（Separate / Collapse / Rearrange 三布局；Rearrange 依 `MergeOrder` 行/列优先 + 蛇形 + 倒序生成输出槽序，与选择排序正交）                                                                                                                                                                                                              | ⑤      |
| `export.cpp`                    | `exportImage`：分离→crop + 编码 + 命名模板写入文件夹（自动创建）；合并→画布 blit + 写单图（E-7/E-8）；编码委托 `io::writeImageFile`；目录/文件名经 `pathFromUtf8` 按 UTF-8 开合、`pathToUtf8` 回传（中文 / 非 ASCII 导出路径）                                                                                                                                                                             | export |
| `engine_config_json.cpp`        | `EngineConfig ↔ JSON`（SPEC §7 schema，含可选 `annotations`/`layers`/`generate` 段与顶层 `opacity`；`layers` 直接以数组读写、底图 `base` 以 `generate` 段读写、位图不入配置；兼容旧图层 `preprocess` 的 `opacity` 步——载入时折叠为 `transform.opacity`）；文件存取内部用 nlohmann/json，`ofstream/ifstream` 经 `pathFromUtf8` 按 UTF-8 开合                                                                  | 配置     |
| `engine_config_json_internal.h` | Core **私有内部头**（仅 `engine_config_json.cpp` 包含，同 `json_common.h` 先例，绝不外泄公共头）：§7 的「枚举 ↔ 串 / 颜色 / 宽容读取」助手区（`tierName` / `generatorName` / `remainderName` 等 + cut/sequence/generate/preprocess 各段小助手）与 `keys::` / `defs::` / `nums::` 三个别名（真相源在 `core_texts.h` / `core_nums.h`）；`engine_config_json.cpp` 经 `using namespace detail` 共用，不得在 .cpp 里重复定义 | 配置     |
| `engine.cpp`                    | 顶层编排 `runEngine`：串起 ①→②→split→③④→排序→⑤，含 E-1/E-6/E-7 校验；**合并重排仅 L3 可用**（非 L3 的 Rearrange 直接报错），L1/L2 不可坍缩时亦报错（不自动降级）                                                                                                                                                                                                                               | 编排     |

> **依赖顺序**：cut_line → grid → split → selection → sequence → composition → export；
> export 的编码落盘委托 `io` 模块；engine_config_json 独立于流水线，仅负责配置持久化。
> libwebp / nlohmann-json 均由 CMake 以 PRIVATE 接入，仅在各自 `.cpp`（io/image_io / engine_config_json）编译期可见。
