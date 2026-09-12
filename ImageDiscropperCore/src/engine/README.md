# src/engine

**目录作用**：`include/engine` 下「算法型」接口的实现代码集合。★ 现已**全部落地为真实实现**
——切割线生成与归一化 / 网格铺设与诱导（以基准点为相位锚周期生成贯穿线，行列数自动推导）/ 切分 / 选择极性 / 排序 / 合成（分离·坍缩·重排）/
图像解码（stb_image → RGBA）/ 导出（stb+libwebp 编码，多图写入文件夹）/ 配置 JSON 存取 / 顶层编排，覆盖终稿 §2 全流水线。

**分块依据**：与 `include/engine` 头文件一一对应，并按流水线阶段拆分——每个 `.cpp` 只承载
一个阶段的算法，且只 include 自身阶段头（`split.cpp`→`split.h`、`selection.cpp`→`selection.h`、
`composition.cpp`→`composition.h`、`export.cpp`→`export.h`），不再全量依赖 `engine.h` facade（解耦）；
`engine.cpp` 仅保留顶层编排 `runEngine`。`generateCutLines` / `induceGrid` 因桥接聚合配置
（`CutConfig` / `SourceInfo`）声明留在 `engine.h`，故 `cut_line.cpp` / `grid.cpp` 仍 include 之。避免上帝文件（guideline：严禁 God File）。

| 文件 | 职责 | 阶段 |
|---|---|---|
| `cut_line.cpp` | `normalizeCutLines`（去重/升序/裁到边界，E-2）+ `generateCutLines`（RECT/横线/竖线/多矩形/网格 5 生成器，E-1/E-3） | ① |
| `grid.cpp` | `Grid::build`（基准点为相位锚周期铺满全图，行列数自动推导；余量策略 DISCARD/KEEP_PARTIAL/PAD，E-5）+ `buildFromLines` + `induceGrid` | ② |
| `split.cpp` | `split`：遍历网格单元切分，跳过空块（E-4），产出带单元序号的 `RegionSet` | split |
| `selection.cpp` | `Selection::resolve`（keep→选中 / remove→补集）+ `applyPolarity`（过滤得保留集 R） | ③④ |
| `sequence.cpp` | `Sequence::build`：row-major / column-major + reverse + snake（CUSTOM 由 `setCustom` 提供） | 排序 |
| `composition.cpp` | `isCollapsible`（§5.4 定理）+ `compose`（SEPARATE / COLLAPSE / REARRANGE 三布局） | ⑤ |
| `export.cpp` | `exportImage`：分离→crop + 编码 + 命名模板写入文件夹（自动创建）；合并→画布 blit + 写单图（E-7/E-8） | export |
| `image_io.cpp` | stb_image 解码（→ RGBA）+ stb 图像编码（PNG/JPEG/BMP）+ libwebp（WebP）；**全工程唯一定义 `STB_IMAGE_IMPLEMENTATION` / `STB_IMAGE_WRITE_IMPLEMENTATION`** | I/O |
| `engine_config_json.cpp` | `EngineConfig ↔ JSON`（§9 schema）+ 文件存取（内部用 nlohmann/json） | 配置 |
| `engine.cpp` | 顶层编排 `runEngine`：串起 ①→②→split→③④→排序→⑤，含 E-1/E-6/E-7 校验 | 编排 |

> **依赖顺序**：cut_line → grid → split → selection → sequence → composition → export；
> image_io 为 export 的底层支撑；engine_config_json 独立于流水线，仅负责配置持久化。
> stb / libwebp / nlohmann-json 均由 CMake 以 PRIVATE 接入，仅在各自 `.cpp`（image_io / engine_config_json）编译期可见。
