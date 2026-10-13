# src/core

**目录作用**：`include/core` 下头文件的实现代码集合。

**分块依据**：与头文件一一对应，每个 `.cpp` 只实现同名头文件里声明的接口，
避免出现"一个 cpp 实现所有功能"的耦合。

| 文件                    | 职责                                                                                                          |
|-----------------------|-------------------------------------------------------------------------------------------------------------|
| `color.cpp`           | 颜色运算与 RGB ↔ HSV 转换实现                                                                                        |
| `point.cpp`           | 距离计算等辅助函数                                                                                                   |
| `image.cpp`           | 图像像素缓冲、读写、格式转换实现                                                                                            |
| `history_manager.cpp` | 空翻译单元占位（`HistoryManager<T>` 纯模板实现全在头文件，供将来扩展非模板辅助函数）                                                        |
| `i18n/`（子目录）          | 三层共用的 i18n 内核实现 `core_i18n.cpp` 与 Core 英文译文表 `lang_en.cpp` 已迁入 [`src/i18n/`](../i18n/README.md)，逐文件职责见该目录说明 |

> `pipeline_manager.h` 的 `Pipeline<T>` 为纯头模板、无 `.cpp`（无占位 TU）：其语义全在头文件，消费者侧（`preprocess`）按需实例化即可。

> i18n 的**内核与译文表分开安置**（已迁至 [`src/i18n/`](../i18n/README.md)）：`core_i18n.cpp` 只做查表与语言决议（不含任何文案），条目表在
> `lang_en.cpp` 并在那里定义 `installCoreCatalogs()`，由 GUI / CLI 各自的 `installCatalogs()` 调用装配
> （demo / examples 不装表就是纯中文，合预期）。可用语言清单由 `installLanguage` 的登记动作累积而来，
> 不另维一张表。加一门语言 = 新增一份 `lang_xx.cpp` + 在 `installCoreCatalogs()` 登记一行 +
> `core_i18n.h` 补一个语言码常量（需接受别名时再在 `resolveLanguage` 补一条映射）。
