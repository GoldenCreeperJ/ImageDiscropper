# ImageDiscropperCli

**模块作用**：ImageDiscropper 的命令行层，产出可执行程序 **`idc`**。
它是一层**薄壳**：只做四件事——解析参数、调用 Core 的统一引擎、格式化输出、返回退出码；
**不含任何切割 / 几何 / 排序 / 极性 / 合成 / 编解码逻辑**，全部委托 `ImageDiscropperCore`。

三层模式共用 Core 的同一条 Grid-Selection-Emit 代码路径（NFR-0：模式即参数预设）：

| 命令        | 层级      | 极性            | 输出                                                    |
|-----------|---------|---------------|-------------------------------------------------------|
| `extract` | L1 标准提取 | 恒 keep        | 合并单图（坍缩）                                              |
| `erase`   | L2 反向剔除 | 恒 remove      | 分离到文件夹 / 坍缩（重排为 L3 专属，`erase` 拒绝 `--merge rearrange`） |
| `grid`    | L3 网格分割 | keep 或 remove | 分离 / 重排合并（`--merge-sort`/`--merge-decorate` 控制填充顺序）   |
| `config`  | 配置驱动    | 由 JSON 决定     | 由 JSON 的 emit 决定                                      |

## 目录结构

```
ImageDiscropperCli/
├── CMakeLists.txt          # 产出静态库 idc_cli_lib + 可执行 idc + 测试 cli_tests
├── include/            # 公共头（见 include/README.md）
├── src/                    # 实现（见 src/README.md）
│   └── commands/           # 一命令一文件（见 src/commands/README.md）
├── tests/                  # 单元 + 集成测试（见 tests/README.md）
└── examples/               # 4 个场景示例脚本（见 examples/README.md）
```

## 构建与运行

CLI 由**仓库根** `CMakeLists.txt` 统一编排（Core 先于 CLI 引入，CLI 链接 `image_discropper_core`）；
构建命令与测试运行见 [`../README.md`](../README.md)「构建与运行」。
第三方库（stb / libwebp / nlohmann_json）由 Core 以 PRIVATE 封装，CLI 无需感知。
版本经编译期宏注入：`IDC_CLI_VERSION`（根工程版本）与 `IDC_CORE_VERSION`（Core 经 PARENT_SCOPE 回传）。

## 用法速览

```bash
idc <command> [options]
idc --help | --version

# L1：保留中心矩形
idc extract --input photo.jpg --rect 100,100,300,250 --output out.png
# L2：十字切割，坍缩合并 / 分离导出
idc erase  --input photo.jpg --rect 100,100,300,250 --merge collapse --output out.png
idc erase  --input photo.jpg --rect 100,100,300,250 --output-dir ./out/ --format png
# L3：网格保留四角并重排到 2x2 画布（--compose 时 --canvas 的 cols/rows 必填且须为正）
idc grid   --input photo.jpg --grid 100,100,200,150 --keep 0,0 --keep 0,2 --keep 2,0 --keep 2,2 \
           --compose --canvas 2x2 --output result.png
# L3：重排时另控填充顺序（与 --sort 选择排序正交；--merge-sort 不支持 custom）
idc grid   --input photo.jpg --grid 100,100,200,150 --keep 0,0 --keep 0,1 --keep 1,0 --keep 1,1 \
           --compose --canvas 2x2 --merge-sort column-major --merge-decorate snake --output result.png
# 配置驱动
idc config --load my-config.json --input photo.jpg --output result.png
```

完整选项见 `idc <command> --help`。

> **行为契约**（参数规则 / 全局选项 / 退出码 / 错误输出格式）已收编至
> [`SPEC.md`](../../SPEC.md) §8.1，本文件不再重复维护。

## 使用的 Core API（接口确认清单）

- 引擎编排：`engine::runEngine` / `EngineConfig` / `EngineResult`
- 网格几何：`engine::Grid::build` / `cellAt` / `cellCount`（把 `r,c` 映射为线性 `index`）
- 区域：`engine::RectRegion` / `horizontalBand` / `verticalBand`
- 配置存取：`engine::loadEngineConfig` / `saveEngineConfig`
- 图像 I/O：`engine::readImageFile`（解码）/ `writeImageFile`（示例 / 测试造图）
- 基础类型：`core::Image` / `core::Color` / `kTransparent`

## 已知限制

1. **合并导出的 JPEG/BMP 透明压平**：不支持透明的格式会把 alpha 压平到 `padColor`；
   若未指定 `--pad-color` 则为透明黑，转 JPEG 后表现为黑色背景（属格式固有特性，非缺陷）。

> 此前记录的 4 项 Core 限制（`--pad-color` 不生效 / `{row}`·`{col}` 不支持 / 分离忽略 `--sort` /
> 合并不建父目录）**均已在 Core 层修复**，见 `ImageDiscropperCore` 的 `composition.*` 与 `export.cpp`。

## 待 Core 补充接口

- 暂无阻塞项。原 3 项 TODO（padColor 透传 / `{row}`·`{col}` 命名 / SEPARATE 尊重 Sequence）已落地。

## 许可

本模块随仓库整体采用 **GPL-3.0**（见根 [`LICENSE`](../../LICENSE)）。
