# src — Core 实现层的聚合目录

**目录作用**：Core 各模块 `.cpp` 实现的**归属层**——一个模块一个子目录，本层不放业务实现代码，
只放**跨模块共用的私有内部头**。每个子目录的职责边界、分块依据与文件清单写在自己的 `README.md` 里：
[annotation/](annotation/README.md)、[canvas/](canvas/README.md)、[core/](core/README.md)、
[engine/](engine/README.md)、[generate/](generate/README.md)、[geometry/](geometry/README.md)、
[i18n/](i18n/README.md)、[io/](io/README.md)、[preprocess/](preprocess/README.md)。

**分块依据**：实现按模块下沉子目录，与本层唯一散置文件之间遵循**依赖单向**——私有内部头只可被 Core
内部 `.cpp` 以相对路径包含，**绝不经 `include/` 公共头外泄给下游**（同 [../CMakeLists.txt](../CMakeLists.txt)
的 PRIVATE 封装注释与仓库根 CONTRIBUTING「分层纪律」）。判据：若某助手被**两个及以上模块**共同依赖、
且其口径漂移会影响向后兼容，就上收到本层单一真相源；只服务单模块的一律留在模块目录内
（先例：`engine/config_json/engine_config_json_internal.h`、`annotation/annotation_json_internal.h` 只在各自模块内用，
故留在各自目录）。

| 文件              | 内容                                                                                                                                                  |
|-----------------|-----------------------------------------------------------------------------------------------------------------------------------------------------|
| `json_common.h` | engine 与 annotation 共用的 nlohmann/json 宽容读取助手（`at`/`jInt`/`jStr`/`jBool`/`jDouble`/`colorToHex`/`colorFromHex`）与「SPEC §7 annotations 段 ↔ JSON 片段」交换。 |
