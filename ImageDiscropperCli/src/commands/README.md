# src/commands

**目录作用**：CLI 各子命令的实现——**一命令一文件**（guideline §10.3：严禁上帝文件）。每个命令都是薄壳
（A-0.1）：解析本命令参数 → 按 §5.3 顺序校验 → 装配 `EngineConfig` → 交 `job` 执行；彼此不耦合，
也不含任何引擎逻辑（切割 / 几何 / 排序 / 极性 / 合成全部委托 Core，NFR-0：模式即参数预设）。

**分块依据**：命令的差异只体现在「装配哪些 EngineConfig 字段」，执行与退出码映射统一走 `job`（同一执行通道），
值解析复用 `value_parser`，报错样板复用 `command_support`——均不在命令文件内重实现（A-0.2）。

| 文件 | 命令 / 层级 | 极性 | 装配要点 |
|---|---|---|---|
| `extract.cpp` | `extract` / L1 | 恒 KEEP | `--rect/--hband/--vband` 三选一；输出恒 MERGED+COLLAPSE 单图；带需读图后按 W/H 构造贯穿全图矩形 |
| `erase.cpp` | `erase` / L2 | 恒 REMOVE | 单 `--rect` = 十字切割；多 `--rect` = MULTI_RECT 并集；`--hband/--vband` = 删整条带；缺省 SEPARATE，`--merge collapse/rearrange` 时 MERGED |
| `grid.cpp` | `grid` / L3 | KEEP 或 REMOVE | `--grid x0,y0,cw,ch` + `--keep/--remove`（可重复）或 `--sort custom --order`；用 Core `Grid::build`+`cellAt` 把 `r,c` 映射为线性 `index` 填 `selectedCells`；`--compose` 恒 MERGED+REARRANGE |
| `config_command.cpp` | `config` | 由 JSON 决定 | `--load` 读配置；JSON 不含图像 / 输出路径，故执行时仍需 `--input` 与 `--output`/`--output-dir`；`--save-config` 为加载后再序列化的 dry-run |

**关键约定**：
- `grid` 的三种选择来源互斥：`--sort custom` 时用 `--order`（禁 `--keep/--remove/--decorate`，极性 KEEP）；
  否则用 `--keep` 或 `--remove`（二选一，禁 `--order`）。
- 单元 `r,c` → `index` 的映射**必须**经 Core 的 `Grid`（行列数由图像边界自动推导），CLI 不自算几何（A-0.1）。
- `config_command` 的 `explicitCollapse` 由加载配置的 `emit.mode==MERGED && emit.layout==COLLAPSE` 推导，
  以便坍缩不可行时返回退出码 2（IT-18）。
