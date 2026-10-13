# include/core

**目录作用**：定义整个库最底层、无外部依赖的基础数据类型与通用能力。所有上层模块
（`geometry` / `annotation` / `preprocess` / `engine`）
都直接或间接依赖这里的类型。对应 SPEC §2.2 的 `Image` 概念与 FR-1.5 / NFR-4 的撤销重做能力。

**分块依据**：将「数据」与「算法」解耦，把最基础的像素点、颜色、图像三种数据结构
独立成三个头文件，避免上层出现循环依赖；与业务无关的通用容器能力（泛型撤销 / 重做栈、
泛型有序步骤队列，原 `history` 模块并入）同样落在本层，供标注层 / GUI 文档历史 / preprocess 复用。

| 文件                   | 职责                                                                                                                                                                                                                               |
|----------------------|----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------|
| `color.h`            | RGBA 颜色结构体 `Color` 及颜色运算（反色 / 感知反色 / 互补色 / HSV 转换）                                                                                                                                                                               |
| `point.h`            | 二维浮点坐标 `Point2D`、整数点 `Point2I` 及其算术运算                                                                                                                                                                                            |
| `image.h`            | 图像像素容器 `Image`，支持 RGB / RGBA / GRAY 三种模式，行主序存储                                                                                                                                                                                   |
| `history_manager.h`  | 泛型撤销 / 重做栈 `HistoryManager<T>`：`push` / `popToRedo` / `popFromRedo` 等（纯模板头实现，原 `history` 模块并入）                                                                                                                                   |
| `pipeline_manager.h` | 泛型有序步骤队列 `Pipeline<T>`：`add` / `insert` / `remove` / `replaceAt` / `moveUp` / `moveDown` / `clear` + `size` / `empty` / `items` / `operator==`（纯模板头实现，与元素类型无关的「按序增删移改 + 越界钳制/忽略」容器骨架；现由 `preprocess::PreprocessPipeline` 公开继承复用） |
| `core_nums.h`        | **常量层 · 数字**：Core 各模块「会被人工调」的阈值 / 范围上下限 / 出厂默认值，按来源模块分组（`nums::engine` / `preprocess` / `annotation` / `canvas` / `generate` / `geometry`），header-only                                                                           |
| `i18n/`（子目录）         | **常量层 · 字符串** `core_texts.h`（`jsonkey`/`defaults` 不可译 + `msg` 可译）与**三层共用 i18n 内核** `core_i18n.h`（`IDC_KTR`/`IDC_KTRF` + `formatPattern` + 语言决议，零 Qt）已迁入 [`include/i18n/`](../i18n/README.md)，逐文件职责见该目录说明                         |

> 语义：`push` 追加新条目并清空重做栈（新操作切断「未来」）；`popToRedo` 撤销、
> `popFromRedo` 重做。主栈经 `undoStack()`（只读 / 可写两个重载）对外暴露：可写重载供使用方对「当前有效条目」
> 原地编辑（如标注 EDIT 模式改样式，不新增历史条目）。现仅由 GUI `DocHistory` 以 `T=EngineConfig`
> 实例化承载文档参数态历史（标注会话的撤销/重做已拆至 GUI `AnnotationEditor`，改在 `AnnotationSet` 上自行编排、不再复用本栈）。

> `Pipeline<T>` 语义：与 `HistoryManager<T>` 同为纯头模板，逻辑只落在 `std::vector` 下标操作与边界策略上，绝不含 `T` 专属代码。
> 边界钳制/忽略：`insert` 越界（`>= size`）钳制为追加队尾、`remove`/`replaceAt` 越界忽略、`moveUp` 首位忽略、`moveDown` 末位忽略；
> `moveUp/moveDown` 只做相邻单格机械换序，不感知跨元素不变式（geo→color 分区那类不变式属上层会话职责）。现由 `preprocess::PreprocessPipeline` 以其存储 + CRUD 骨架公开继承复用（LSP 成立：Core 侧无跨方法守卫不变式可绕过）。

> 坐标与像素约定遵循 SPEC §2.1：整数像素、原点左上、`x` 向右 `y` 向下。

## 常量层（`core_nums.h` · 本目录 / `core_texts.h` · [`i18n/`](../i18n/README.md)）

**为何存在**：魔数与硬编码字符串散在各模块时，人工调参要满仓库找；JSON 键名在写 / 读两侧各写一份还会漂移。
两文件按「数字 / 字符串」分文件、按来源模块分 `namespace`，集中但**不追求穷尽**。

**提取判据**（与 CLI / GUI 常量层一致）：

- 提取：重复出现 ≥2 次的同一字面量；人工需要调的阈值 / 范围上下限 / 出厂默认值；有业务语义的边界值；一切 JSON 键名与
  兜底默认串（成员初值与读取回退必须同值，故两处合为一条）。
- 不提取：数学与换算常数（255 / 360 / 180 / 2π / 0.5 / 1000 / 1024 / 100）；「数据即定义」的表（卷积核系数、
  色彩矩阵、分量位权与 `0xFF` 掩码）；枚举 ↔ 串的**成对映射表**内的 token 值（映射表即该 token 的唯一定义点）；
  已在模块头就地命名并注释清楚的护栏常量（如 `pattern.h::kMaxDimension`）；§7 里「无值即不生效」的 0/1 型占位回退；
  命名模板的 token 碎片（`"{name}"` 等留在 `applyNaming`）；循环边界 0/1/-1、理论不可达兜底；`tests/` 与 `examples/` 的期望值与示例值。

**可译性分区**（i18n 已落地）：`jsonkey` / `defaults` 是要落盘的数据，翻译即破协议，**永不可译**；
`msg` 面向人展示，使用点经 `IDC_KTR(分组, 常量名)` / `IDC_KTRF(...)`（宏把常量名在预处理器里拼成键
`分组::常量名`，同名中文常量作回退值）。Core 无 Qt 依赖，故查表与译文存储都落在本层（`core_i18n.h`），
GUI / CLI 只各自包一层返回 `QString` / `std::string` 的门面。**不引 Qt Linguist / `tr()` / `lupdate` / `.ts` / `.qm`**：
lupdate 抽不到常量层，`.qm` 又是 Qt6 内部二进制格式，零 Qt 的 Core / CLI 读不了。

**表与语言码的家**：Core 自己的英文表住 `src/i18n/lang_en.cpp`，由 `installCoreCatalogs()` 导出；GUI 与 CLI
各自的 `installCatalogs()` **先调它**再装本层表（显示的是 Core 的报因，不该各存一份）。源语言 zh-CN **不建表**
（空表 → 全部回退常量层原文），因此接译文不改变中文输出，也不会把中文存两遍。

**引用方式**：使用点以命名空间别名简写（`namespace keys = core::texts::jsonkey;` 等）。别名一律**只落在 .cpp 或
内部头**内，不进公共头 —— 以免污染库使用者的命名空间；公共头写全限定名。

**类型选择**：一律 `inline constexpr`（数字用值类型，字符串用 `char[]` UTF-8 字节串），
不引入 `std::string` 全局对象，以免静态初始化顺序与启动开销。

**改完怎么自检**（三层同一套路，改常量前后各跑一次）：

1. **零孤儿**：两条 `*_texts.h` / `*_nums.h` 内每条 `kXxx` 都要能在仓库内被引用；未被引用即说明对应文案已删，条目连注释一起删。
2. **零回落**：生产代码里不应再出现裸中文与裸键名字面量（`src/` 内 grep「引号内含中文」应 0 命中：拼接报因与
   图案显示名已在 i18n I3 收进 `texts::msg`，仅剩成对映射表内的 token 属不提取判据）。
3. **同值不重复**：同一文件内同值条目只允许「同字不同角色」，且两侧行尾注释必须互相点名；同值同角色必须合一并列全落点。
4. **构建 + 单测**：Core 不依赖 Qt，`cmake --build` 后跑 `ctest` 即可（引擎边界 / 导出 / 流水线三组用例）。
5. **译文表对账**：`tests/test_i18n_catalog.cpp` 零 Qt 直读三层常量层与三张译文表，断言无孤儿键 / 无重复键 /
   无跨层撞键 / 契约组不越界，并打印各组覆盖率（改名或删条目后必跑，否则错键只会静默回退而不报错）。
