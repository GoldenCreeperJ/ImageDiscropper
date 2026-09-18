# examples

**目录作用**：CLI（`idc`）四个场景的可运行示例脚本。每个脚本聚焦一层模式的
典型用法，输出到系统临时目录，**不污染仓库**，且逐步带中文注释。

## 脚本清单

| 脚本                     | 覆盖场景                             | 关键命令                                 |
|------------------------|----------------------------------|--------------------------------------|
| `l1_extract.sh`        | L1 矩形 / 横带 / 竖带保留                | `extract --rect / --hband / --vband` |
| `l2_erase_collapse.sh` | L2 十字切割 + 坍缩合并（含横带 / 竖带 / 多矩形并集） | `erase --merge collapse`             |
| `l2_erase_separate.sh` | L2 十字切割 + 分离导出到文件夹               | `erase --output-dir`                 |
| `l3_grid_compose.sh`   | L3 网格选择 + 排序 + 重排合并 / 分离         | `grid --keep/--remove --compose`     |

## 前置条件

1. **已构建 `idc`**：在仓库根用 CLion（或 `cmake -S . -B build && cmake --build build`）构建后，
   可执行文件位于 `build/bin/idc`（Windows 为 `idc.exe`）。脚本会自动在常见构建输出目录中查找；
   也可用环境变量显式指定：`IDC=/path/to/idc bash l1_extract.sh`。
2. **示例图片 `examples/sample.png`**：脚本硬编码以本目录下的 `sample.png` 为输入。
   仓库当前**未提交**该图片，运行前请自备一张放到 `examples/sample.png`：
   - 建议尺寸 **640×480**（脚本内坐标按此设计；更小可能触发坐标越界 → 退出码 1/2）。
   - 任意来源的 PNG 均可；也可用 Core 的 `demo` 可执行程序生成一张测试图后重命名为 `sample.png`。

## 运行方式（Git Bash / Linux / macOS）

```bash
cd ImageDiscropperCli/examples
bash l1_extract.sh
bash l2_erase_collapse.sh
bash l2_erase_separate.sh
bash l3_grid_compose.sh
```

脚本结束时会打印本次输出所在的临时目录路径，可前往查看结果图；临时目录位于系统 `tmp`，不写入仓库。

## 说明

- 单元序号约定：网格按**行主序** `index = row * 列数 + col`；`--keep r,c` / `--remove r,c` / `--order r,c;r,c`
  使用的是「行,列」坐标（从 0 起），由 CLI 经 Core 的 `Grid` 映射为线性序号（不在 CLI 侧自算几何）。
- `l3_grid_compose.sh` 以 `--grid 0,0,160,120` 为例：640×480 图恰好铺成 4 列 × 4 行 = 16 个单元。
- 分离导出（`--output-dir`）与合并导出（`--output`）的目标文件夹 / 父目录不存在时均由 Core 自动创建；
  仅当父路径被普通文件占用等 E-8 情形才失败（退出码 4）。
