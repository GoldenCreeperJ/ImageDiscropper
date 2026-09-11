# src/engine

**目录作用**：`include/engine` 下「算法型」接口的实现代码集合。★ 当前全部为**桩实现**
（返回空结果 + `TODO`），仅保证接口可编译、可链接，不含任何切割 / 剔除 / 网格 / 排序 / 合成 /
导出的真实算法。

**分块依据**：与 `include/engine` 头文件一一对应，每个 `.cpp` 只实现同名头文件里声明的算法函数；
纯数据结构（`Region` / `RegionSet` 等）已在头文件中 inline 实现，故本目录无对应 `.cpp`。

| 文件               | 职责                                                                                       | 状态  |
|------------------|------------------------------------------------------------------------------------------|-----|
| `cut_line.cpp`   | 切割线归一化 `normalizeCutLines`                                                               | ⛔ 桩 |
| `grid.cpp`       | 网格铺设 `Grid::build`                                                                       | ⛔ 桩 |
| `selection.cpp`  | 选择集解析 `Selection::resolve`                                                               | ⛔ 桩 |
| `sequence.cpp`   | 排序序列构建 `Sequence::build`                                                                 | ⛔ 桩 |
| `composition.cpp`| 坍缩可行性判定 `isCollapsible`                                                                  | ⛔ 桩 |
| `engine.cpp`     | 流水线 `generateCutLines` / `induceGrid` / `split` / `applyPolarity` / `compose` / `exportImage` | ⛔ 桩 |

> **实现边界**：真正的引擎算法属于新项目 MVP / v2 / v3 阶段，**不在本次「适配既有项目」的范围内**。
