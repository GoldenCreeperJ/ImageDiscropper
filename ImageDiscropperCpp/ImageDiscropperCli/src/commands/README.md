# src/commands

**目录作用**：CLI 各子命令的实现——**一命令一文件**。每个命令都是薄壳
：解析本命令参数 → 按固定顺序校验 → 装配 `EngineConfig` → 交 `job` 执行；彼此不耦合，
也不含任何引擎逻辑（切割 / 几何 / 排序 / 极性 / 合成全部委托 Core，NFR-0：模式即参数预设）。

**分块依据**：命令的差异只体现在「装配哪些 EngineConfig 字段」，执行与退出码映射统一走 `job`（同一执行通道），
值解析复用 `value_parser`，报错样板复用 `command_support`——均不在命令文件内重实现。

| 文件            | 命令 / 层级        | 极性            | 装配要点                                                                                                                                                                                                                                                            |
|---------------|----------------|---------------|-----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------|
| `extract.cpp` | `extract` / L1 | 恒 KEEP        | `--rect/--hband/--vband` 三选一；输出恒 MERGED+COLLAPSE 单图；带需读图后按 W/H 构造贯穿全图矩形；可附 `--annotation/--annotations` 标注（烧录进工作图）                                                                                                                                              |
| `erase.cpp`   | `erase` / L2   | 恒 REMOVE      | 单 `--rect` = 十字切割；多 `--rect` = MULTI_RECT 并集；`--hband/--vband` = 删整条带；缺省 SEPARATE，`--merge collapse` 时 MERGED（**拒绝 `--merge rearrange`：重排为 L3 专属**）                                                                                                             |
| `grid.cpp`    | `grid` / L3    | KEEP 或 REMOVE | `--grid x0,y0,cw,ch` + `--keep/--remove`（可重复）或 `--sort custom --order`；用 Core `Grid::build`+`cellAt` 把 `r,c` 映射为线性 `index` 填 `selectedCells`；`--compose` 恒 MERGED+REARRANGE（**`--canvas` 的 cols/rows 必填且须为正**），`--merge-sort`/`--merge-decorate` 写 `MergeOrder` |
| `config.cpp`  | `config`       | 由 JSON 决定     | `--load` 读配置（含 `preprocess` 声明式队列与可选 `annotations` 段）；JSON 不含图像 / 输出路径，故执行时仍需 `--input` 与 `--output`/`--output-dir`；命令行 `--annotation/--annotations` 追加在配置自带 `annotations` 段之后；`--save-config` 为加载后再序列化的 dry-run（保留 preprocess / 合并后的 annotations）              |

**关键约定**：
- `grid` 的三种选择来源互斥：`--sort custom` 时用 `--order`（禁 `--keep/--remove/--decorate`，极性 KEEP）；
  否则用 `--keep` 或 `--remove`（二选一，禁 `--order`）。
- 单元 `r,c` → `index` 的映射**必须**经 Core 的 `Grid`（行列数由图像边界自动推导），CLI 不自算几何。
- `config.cpp` 的 `explicitCollapse` 由加载配置的 `emit.mode==MERGED && emit.layout==COLLAPSE` 推导，
  以便坍缩不可行时返回退出码 2（IT-18）。
- **合并重排仅 L3**：`grid --compose` 强制 `--canvas` 的 cols/rows 必填且为正；`config.cpp` 对加载配置中的
  REARRANGE 同样校验「仅 L3 + cols/rows 必填」，违反则退出码 1（不交 Core 默认值兜底）；`erase` 拒绝 `--merge rearrange`。
- **填充顺序与选择排序正交**：`--merge-sort`（row-major/column-major，不支持 custom）与 `--merge-decorate`
  （none/reverse/snake/reverse-snake/snake-reverse）复用 `value_parser` 解析，写入 `MergeOrder{strategy,reverse,snake}`。
- **preprocess 物化于 `job.executeJob`（几何与落盘共用同一张工作图）**：加载配置携带 `preprocess`（如 rotate/resize 会改变尺寸）时，
  `executeJob` 先 `config.preprocess.apply(image)` 物化为 `work`、清空 `config.preprocess` 防二次应用，再让
  `runEngine(work)` 与 `exportImage(composition, work)` 共用 `work`——否则裁剪矩形会套在未预处理的原图上导致坐标错位 / 越界。
  三层命令不设 preprocess，`work` 即原图，路径不变。
- **标注入口与烧录（SPEC §7 `annotations` 元素语法）**：四层命令（extract/erase/grid/config）均经 `annotation_args.collectAnnotations` 收集命令行标注
  （`--annotation` 内联对象可重复、`--annotations` 根数组文件可重复），解析委托 Core 的 `parseAnnotationsFromJsonText`（CLI 不引 nlohmann/json）；
  `config.cpp` 还把命令行标注**追加**在 `--load` 配置自带 `annotations` 段之后。装配到临时 `AnnotationLayer` 后传给 `executeJob`；
  该层坐标以**工作图**为基准，故 `executeJob` 在 `preprocess` 物化**之后**、校正 `source` 之前用 `annotations->burnIn(work)` 把标注烧进 `work`（与 GUI「先重放预处理、再还原标注」同一顺序纪律）；`--save-config` dry-run 把「配置自带 + 命令行」标注一并透写回 `annotations`（往返不丢）。无标注时传 `nullptr`（不烧录、不写段，行为零回归）。
