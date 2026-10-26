# src/engine/config_json

**目录作用**：SPEC §7 配置 JSON 存取的全部实现——`EngineConfig ↔ JSON` 的写/读编解码、读侧严格
schema 守卫与百分比坐标归一化/回写。与父目录 `src/engine/` 的切割流水线（cut_line → … → export）
**正交**：流水线消费的只有 `EngineConfig` 结构体，配置持久化不参与任何切割/合成计算
（`engine_config_json` 独立于流水线，仅负责配置持久化）。

**分块依据**：编解码（字段 ↔ 键语义映射）、严格校验（present-but-invalid 拦截）、百分比换算
（就地归一化 + 记账回写）三套机制互不依赖，各成一对文件；三者之间只经以下入口衔接——
编解码侧调用 `validateSchema` / `validateLayerSeg`（读前守卫）与 `resolvePercentCoords` /
`applyPercentHints` / `resolveLayerPercentInPlace`（读前归一化 / 保存回写），其余助手全部内聚于
各自实现文件匿名命名空间。共用助手（枚举 ↔ 串、颜色、宽容读取、`keys::` / `defs::` / `nums::`
别名）集中 `engine_config_json_internal.h`；跨模块交换（annotations 段）经
[`src/json_common.h`](../../json_common.h) 声明、由 annotation 模块实现。全部文件只被本目录内
`.cpp` 以相对路径包含（`"../../json_common.h"`），nlohmann/json 依赖 PRIVATE 不外泄。

| 文件                              | 职责                                                                                                                                                                                                                                                                                                                                                                            |
|---------------------------------|-------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------|
| `engine_config_json.cpp`        | `EngineConfig ↔ JSON` 编解码（SPEC §7 schema，含可选 `annotations`/`layers`/`generate` 段与顶层 `opacity`；`layers` 直接以数组读写、底图 `base` 以 `generate` 段读写、位图不入配置；`preprocess` 队列中的 `opacity` 步非合法 `op`，载入时按未知步跳过）；兼提供 `parseImageLayersFromJsonText`（CLI `--layer` / `--layers` 的段级入口：逐条校验 + 按传入画布尺寸换算 `transform` 百分比）；文件存取内部用 nlohmann/json，`ofstream/ifstream` 经 `pathFromUtf8` 按 UTF-8 开合 |
| `schema_validate.{h,cpp}`       | §7 schema 严格校验（读侧守卫，`validateSchema` 整档 / `validateLayerSeg` 单层段）：拦「键写出且值非 null」但类型 / 枚举 / 颜色非法的 present-but-invalid（缺失 / null 仍走宽容默认）；逐段镜像 `engineConfigFromJson` 的读取分派，报因携 JSON 路径定位；仅 `engine_config_json.cpp` 调用的私有实现                                                                                                                                                     |
| `percent_coords.{h,cpp}`        | 百分比坐标读侧预归一化（`resolvePercentCoords`：白名单几何键 "50%" 就地换算像素、逐处记 `PercentHint`）+ 保存侧回写（`applyPercentHints`：值未编辑且基准未变才换回 "N%"）+ 段级入口 `resolveLayerPercentInPlace`（CLI `--layer` 传入的 transform 百分比，不记账即落像素）；仅 `engine_config_json.cpp` 调用的私有实现                                                                                                                                        |
| `engine_config_json_internal.h` | Core **私有内部头**（仅本目录三实现 `engine_config_json.cpp` / `schema_validate.cpp` / `percent_coords.cpp` 包含，同 `json_common.h` 先例，绝不外泄公共头）：§7 的「枚举 ↔ 串 / 颜色 / 宽容读取」助手区（`tierName` / `generatorName` / `remainderName` 等 + cut/generate/preprocess 各段小助手）与 `keys::` / `defs::` / `nums::` 三个别名（真相源在 `core_texts.h` / `core_nums.h`）；三个配置实现经 `using namespace detail` 共用，不得在 .cpp 里重复定义    |
