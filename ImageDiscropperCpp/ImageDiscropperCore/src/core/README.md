# src/core

**目录作用**：`include/core` 下头文件的实现代码集合。

**分块依据**：与头文件一一对应，每个 `.cpp` 只实现同名头文件里声明的接口，
避免出现"一个 cpp 实现所有功能"的耦合。

| 文件           | 职责                                                                                                  |
|--------------|-----------------------------------------------------------------------------------------------------|
| `color.cpp`  | HSV → RGB 转换 `hsvToRgb` 实现                                                                          |
| `point.cpp`  | 两点欧氏距离 `distance` 实现                                                                                |
| `image.cpp`  | `Image` 像素缓冲、读写、格式转换、缩放 / 裁剪 / 贴图实现                                                                 |
| `i18n/`（子目录） | i18n 内核实现 `core_i18n.cpp` 与 Core 英文译文表 `lang_en.cpp` 住 [`src/i18n/`](../i18n/README.md)，逐文件职责见该目录说明 |

> `pipeline.h` 的 `Pipeline<T>` 与 `history_manager.h` 的 `HistoryManager<T>` 均为纯头模板、无 `.cpp`：
> 其语义全在头文件，消费者侧（`preprocess`、GUI `DocHistory`）按需实例化即可。

> i18n 的**内核与译文表分开安置**（在 [`src/i18n/`](../i18n/README.md)）：`core_i18n.cpp` 只做查表与语言决议（不含任何文案），条目表在
> `lang_en.cpp` 并在那里定义 `installCoreCatalogs()`，由 GUI / CLI 各自的 `installCatalogs()` 调用装配
> （demo / examples 不装表就是纯中文，合预期）。可用语言清单由 `installLanguage` 的登记动作累积而来，
> 不另维一张表。加一门语言 = 新增一份 `lang_xx.cpp` + 在 `installCoreCatalogs()` 登记一行 +
> `core_i18n.h` 补一个语言码清单（需接受别名时再在 `resolveLanguage` 补一条映射）。
