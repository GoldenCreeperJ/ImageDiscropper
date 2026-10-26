# src/engine

**目录作用**：`include/engine` 下「算法型」接口的实现代码集合——切割线生成与归一化 / 网格铺设与诱导
（以基准点为相位锚周期生成贯穿线，行列数自动推导）/ 切分 / 选择极性 / 排序 / 合成（分离·坍缩·重排）/
导出（编码落盘，多图写入文件夹）/ 顶层编排，覆盖 SPEC §1 切割流水线。
图像编解码（stb_image 解码 / stb+libwebp 编码）位于 [`src/io/`](../io/README.md)；
配置 JSON 存取是**独立于流水线**的关注点，居子目录 [`config_json/`](config_json/README.md)。

**分块依据**：与 `include/engine` 头文件一一对应，并按流水线阶段拆分——每个 `.cpp` 只承载
一个阶段的算法，且只 include 自身阶段头（`split.cpp`→`split.h`、`selection.cpp`→`selection.h`、
`composition.cpp`→`composition.h`、`export.cpp`→`export.h`），而不全量依赖 `engine.h` facade（解耦）；
`engine.cpp` 仅保留顶层编排 `runEngine`。`generateCutLines` / `induceGrid` 因桥接聚合配置
（`CutConfig` / `SourceInfo`）声明留在 `engine.h`，故 `cut_line.cpp` / `grid.cpp` 仍 include 之。避免上帝文件（严禁 God File）。

| 文件                              | 职责                                                                                                                                                                                                                                                                                                                                                                                | 阶段     |
|---------------------------------|-----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------|--------|
| `cut_line.cpp`                  | `normalizeCutLines`（去重/升序/裁到边界，E-2）+ `generateCutLines`（Rect/横线/竖线/多矩形/网格 5 生成器，E-1/E-3）                                                                                                                                                                                                                                                                                          | ①      |
| `grid.cpp`                      | `Grid::build`（逐轴按有效定义展开：周期轴自虚拟起点双向铺满全图、跨界残缺单元按 Discard/KeepPartial/Pad 处理；分割数 / 比例轴恰好铺满 `[0,L]`、末格吸收取整误差；行列数自动推导）+ `effectiveAxis` / `checkAxis` + `buildFromLines` + `induceGrid`                                                                                                                                                                                                | ②      |
| `split.cpp`                     | `split`：遍历网格单元切分，跳过空块（E-4），产出带单元序号的 `RegionSet`                                                                                                                                                                                                                                                                                                                                   | split  |
| `selection.cpp`                 | `Selection::resolve`（Keep→选中 / Remove→补集）+ `applyPolarity`（过滤得保留集 R）                                                                                                                                                                                                                                                                                                              | ③④     |
| `sequence.cpp`                  | `Sequence::build`：row-major / column-major + reverse + snake（Custom 由 `setCustom` 提供）                                                                                                                                                                                                                                                                                             | 排序     |
| `composition.cpp`               | `isCollapsible`（SPEC §4.4 定理）+ `compose`（Separate / Collapse / Rearrange 三布局；Rearrange 依 `MergeOrder` 行/列优先 + 蛇形 + 倒序生成输出槽序，与选择排序正交；槽尺寸取保留块最大宽/高＝统一格，块长宽全等由 runEngine 准入门限保证）                                                                                                                                                                                                     | ⑤      |
| `export.cpp`                    | `exportImage`：分离→crop + 编码 + 命名模板写入文件夹（自动创建）；合并→画布 blit + 写单图（E-7/E-8）；编码委托 `io::writeImageFile`；目录/文件名经 `pathFromUtf8` 按 UTF-8 开合、`pathToUtf8` 回传（中文 / 非 ASCII 导出路径）                                                                                                                                                                                                             | export |
| `engine.cpp`                    | 顶层编排 `runEngine`：串起 ①→②→split→③④→排序→⑤，含 E-1/E-6/E-7 校验；**合并重排仅 L3 可用**（非 L3 的 Rearrange 直接报错），L1/L2 不可坍缩时亦报错（不自动降级）；**重排准入门限＝保留块长宽全等**（SPEC §4.3）——不全等时逐级回退：可坍缩 → 坍缩，否则 → 分离导出（`EngineResult::uniformBlocks` 回灌前端）                                                                                                                                                                | 编排     |

> **依赖顺序**：cut_line → grid → split → selection → sequence → composition → export；
> export 的编码落盘委托 `io` 模块；`config_json/` 独立于流水线，仅负责配置持久化（见其 README）。
> libwebp / nlohmann-json 均由 CMake 以 PRIVATE 接入：libwebp 仅在 `io/image_io.cpp` 编译期可见；
> nlohmann 仅在配置/标注的私有实现（`config_json/` 三件 + annotation 两件，见 CMakeLists 注释）编译期可见。
