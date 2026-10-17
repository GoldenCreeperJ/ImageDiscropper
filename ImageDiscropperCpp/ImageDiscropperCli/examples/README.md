# examples

**目录作用**：CLI（`idc`）四个场景的可运行示例脚本。每个脚本聚焦一层模式的
典型用法，输出到系统临时目录，**不污染仓库**，且逐步带中文注释。

## 脚本清单

| 脚本                     | 覆盖场景                                 | 关键命令                                               |
|------------------------|--------------------------------------|----------------------------------------------------|
| `l1_extract.sh`        | L1 矩形 / 横带 / 竖带保留                    | `extract --rect / --hband / --vband`               |
| `l2_erase_collapse.sh` | L2 十字切割 + 坍缩合并（含横带 / 竖带 / 多矩形并集）     | `erase --merge collapse`                           |
| `l2_erase_separate.sh` | L2 十字切割 + 分离导出到文件夹                   | `erase --output-dir`                               |
| `l3_grid_compose.sh`   | L3 网格选择 + 排序 + 重排合并 / 分离             | `grid --keep/--remove --compose`                   |
| `l_annotate.sh`        | 命令行标注输入（内联 / 文件）与 `--save-config` 导出 | `--annotation` / `--annotations` / `--save-config` |

## 前置条件

1. **已构建 `idc`**：在仓库根用 CLion（或 `cmake -S . -B build && cmake --build build`）构建后，
   可执行文件位于 `build/bin/idc`（Windows 为 `idc.exe`）。脚本会自动在常见构建输出目录中查找；
   也可用环境变量显式指定：`IDC=/path/to/idc bash l1_extract.sh`。
2. **示例图片 `assets/sample.png`**：脚本以 `${SCRIPT_DIR}/../../../assets/sample.png`
   （examples 上溯三级目录的 `assets/`）为输入；图片缺失时脚本以退出码 3 报错：
   - 建议尺寸 **640×480**（脚本内坐标按此设计；更小可能触发坐标越界 → 退出码 1/2）。
   - 任意来源的 PNG 均可；也可用 Core 的 `demo` 可执行程序生成一张测试图后放到该路径。

## 运行方式（Git Bash / Linux / macOS）

```bash
cd ImageDiscropperCli/examples
bash l1_extract.sh
bash l2_erase_collapse.sh
bash l2_erase_separate.sh
bash l3_grid_compose.sh
bash l_annotate.sh
```

脚本结束时会打印本次输出所在的临时目录路径，可前往查看结果图；临时目录位于系统 `tmp`，不写入仓库。

## 说明

- 单元序号约定：网格按**行主序** `index = row * 列数 + col`；`--keep r,c` / `--remove r,c` / `--order r,c;r,c`
  使用的是「行,列」坐标（从 0 起），由 CLI 经 Core 的 `Grid` 映射为线性序号（不在 CLI 侧自算几何）。
- `l3_grid_compose.sh` 以 `--grid 0,0,160,120` 为例：640×480 图恰好铺成 4 列 × 4 行 = 16 个单元。
- 分离导出（`--output-dir`）与合并导出（`--output`）的目标文件夹 / 父目录不存在时均由 Core 自动创建；
  仅当父路径被普通文件占用等 E-8 情形才失败（退出码 4）。
- 标注载体两种（SPEC §7 annotations 元素语法，坐标基准＝预处理产出）：`--annotation '<内联 JSON 对象>'`
  可重复（每次一条）、`--annotations <file.json>`（文件根为标注数组，如本目录 `sample_annotations.json`）。
  `--save-config` 会把「配置自带 + 命令行」标注一并透写回 JSON 的 annotations 段（导出）。
