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
└── examples/               # 5 个场景示例脚本（见 examples/README.md）
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
# 配置驱动（若 JSON 含可选 annotations 段，执行前会把标注烧录进预处理后的工作图）
idc config --load my-config.json --input photo.jpg --output result.png

# 命令行标注：三层命令 + config 均可附加标注（SPEC §7 annotations 元素语法，坐标基准＝预处理产出）
#   --annotation '<内联 JSON 对象>'：单条标注，可重复；--annotations <file.json>：文件根为标注数组，可重复
idc extract --input photo.jpg --rect 100,100,300,250 \
            --annotation '{"type":"circle","x1":150,"y1":150,"x2":180,"y2":150,"color":"#FFFF0000","fill":true}' \
            --output out.png
# 导出：--save-config 会把命令行标注一并透写进配置 JSON 的 annotations 段
idc extract --input photo.jpg --rect 100,100,300,250 --annotations annos.json \
            --save-config cfg.json --output out.png
```

完整选项见 `idc <command> --help`。

> **标注入口**：四层命令（`extract` / `erase` / `grid` / `config`）均支持命令行标注输入——`--annotation '<内联 JSON 对象>'`
> （一条 SPEC §7 `annotations` 元素，可重复）与 `--annotations <file.json>`（文件根为标注数组，可重复）；两者可混用、累积追加。
> `config` 命令还会把命令行标注**追加**在其 `--load` 配置自带的 `annotations` 段之后。
> 标注解析委托 Core 的 `parseAnnotationsFromJsonText`（内联与文件同一入口），CLI 不自行解析 JSON。
>
> **标注烧录**：`executeJob` 会先物化 `preprocess` 为工作图，再把标注烧录进该工作图（坐标基准＝预处理产出），
> 故标注随像素一起被切割落位。`--save-config` dry-run 会把「配置自带 + 命令行」标注一并透写 `annotations` 段（往返不丢）。

## 标注使用方法

两个入口把标注追加到切割流程中，四层命令（`extract` / `erase` / `grid` / `config`）通用，可混用、按出现顺序累积：

| 选项                     | 取值                                      | 说明                           |
|------------------------|-----------------------------------------|------------------------------|
| `--annotation <json>`  | **单个** SPEC §7 `annotations` 元素对象，或对象数组 | 内联一条/多条；参数须整体单引号包裹，内层双引号保留   |
| `--annotations <file>` | 文件根为标注对象**数组**的 JSON                    | 从文件批量载入；文件解析失败＝退出码 1，提示含「标注」 |

**元素字段（按 `type` 分派几何，样式全类型通用）**：

| 几何类别    | `type`                                                                                                        | 必填几何字段                                                                                    | 可选                      |
|---------|---------------------------------------------------------------------------------------------------------------|-------------------------------------------------------------------------------------------|-------------------------|
| 约束图元    | `rectangle` / `square` / `rhombus` / `isos-triangle` / `equ-triangle` / `rect-triangle` / `equ-rect-triangle` | `x1` `y1` `x2` `y2`（双锚点，几何由 Core 依锚点派生、恒合法）                                               | `cornerRadius`          |
| 直线 / 折线 | `line` / `polyline`                                                                                           | `points`: `[[x,y],…]`（`line` 恰 2、`polyline` ≥2）                                           | `closed`、`cornerRadius` |
| 椭圆 / 圆  | `ellipse` / `circle`                                                                                          | `x1` `y1` `x2` `y2`（双锚点：`(x1,y1)` 为圆心；`circle` 半径＝两点距离、`ellipse` 两半轴＝`(Δx,Δy)` 绝对值，均须 >0） | —                       |
| 自由路径    | `path`                                                                                                        | `segments`: `[{"t":"M/L/Q/C/Z","coords":[…]},…]`（须非空有效）                                   | —                       |
| 文字      | `text`                                                                                                        | `x` `y` `text` `fontSize`（`fontSize` 若给出须 >0）                                             | —                       |

> **约束图元双锚点语义**（`(x1,y1)` 为第一锚点、`(x2,y2)` 定尺寸/朝向，与 GUI 拖拽构造器同一套）：矩形取对角两角；
> 正方形以 `(x1,y1)` 为顶点、边长 `max(|Δx|,|Δy|)` 朝 `(x2,y2)` 象限；菱形 `(x1,y1)` 为中心、`(|Δx|,|Δy|)` 为半对角线；
> 等腰 / 等边三角形 `(x1,y1)` 为底边中点、`(|Δx|,|Δy|)` 定半底与高；直角 / 等腰直角三角形 `(x1,y1)` 为直角顶点、`(|Δx|,|Δy|)` 定两直角边；
> 圆 `circle` `(x1,y1)` 为圆心、半径＝`(x1,y1)→(x2,y2)` 距离（经 Core 构造恒正圆）；椭圆 `ellipse` `(x1,y1)` 为圆心、`(|Δx|,|Δy|)` 为两半轴。

- **样式**（逐条携带，均可省略取默认）：`color`（描边）/ `fillColor`（填充，仅 `fill:true` 时生效）——颜色**同时支持 `#RRGGBB`（不透明）与 `#AARRGGBB`**（前两位为 alpha，`00` 全透~`FF` 全不透）；`strokeWidth`（线宽像素，默认 2）、`fill`（布尔，默认 false）；`zOrder`（整数，叠放次序，`0`＝最底、越大越靠上；省略时回退为该元素在数组中的下标，还原端按其稳定排序后规范化为下标；小数/字符串等非整数判非法、整段报错，负数合法——视为向底层靠拢的排序意图，归一化后导出恒为非负下标）。
- **坐标基准**＝预处理队列产出后的工作图（非原图）；几何字段始终为**局部坐标**，`transform`（整体快照）恒等时省略、一般无需手填。
- **强制校验、逐条报因**（任一条非法即整段退出码 1）：缺/未知 `type`；锚点退化（矩形/菱形需 |Δx|、|Δy| 均 >0，正方形/等腰直角需 max >0，圆需半径 >0、椭圆需两半轴 >0 等）；`line` 点数≠2；`text` 的 `fontSize`≤0；`path` 无有效段；非法 `#RRGGBB`/`#AARRGGBB` 颜色；非整数 `strokeWidth` / `zOrder`（`zOrder` 为小数/字符串即判非法；负数合法，归一化为非负下标）。stderr 逐条列出「第 N 个元素：原因」，不静默丢弃。

```bash
# 内联单条：红色描边矩形（双锚点：对角两角）+ 半透明填充
idc extract --input in.png --rect 0,0,200,150 --output out.png \
    --annotation '{"type":"rectangle","x1":10,"y1":10,"x2":120,"y2":90,"color":"#FFFF0000","fill":true,"fillColor":"#40FF0000","strokeWidth":3}'

# 内联单条：圆（双锚点：圆心 + 圆上点，半径＝两点距离）
… --annotation '{"type":"circle","x1":70,"y1":70,"x2":100,"y2":70,"color":"#FF00FF00","strokeWidth":2}'

# 内联单条：文字
… --annotation '{"type":"text","x":20,"y":30,"text":"标注 A","fontSize":18,"color":"#FF0000FF"}'

# 内联多条：重复 --annotation，或一次传数组
… --annotation '{"type":"line","points":[[0,0],[100,100]],"color":"#FF000000"}' \
    --annotation '{"type":"line","points":[[0,100],[100,0]],"color":"#FF000000"}'

# 从文件批量载入（sample_annotations.json 根为数组）
… --annotations sample_annotations.json

# 导出：--save-config 把命令行标注一并透写进配置（extract 的 dry-run 仍需 --output 过参数校验）
idc config --load base.json --annotation '{"type":"text","x":10,"y":10,"text":"hi","fontSize":16}' \
    --save-config out.json
```

> Windows PowerShell 直接向 `idc.exe` 传内联 `--annotation '{…}'` 时，双引号可能被 shell 剥离而解析失败——
> 建议改用 `--annotations <file>` 文件入口，或经 `cmd` / 脚本封装传参。示例见
> [`examples/l_annotate.sh`](examples/l_annotate.sh)。

> **行为契约**（参数规则 / 全局选项 / 退出码 / 错误输出格式）已收编至
> [`SPEC.md`](../../SPEC.md) §8.1，本文件不再重复维护。

## 使用的 Core API（接口确认清单）

- 引擎编排：`engine::runEngine` / `EngineConfig` / `EngineResult`
- 网格几何：`engine::Grid::build` / `cellAt` / `cellCount`（把 `r,c` 映射为线性 `index`）
- 区域：`engine::RectRegion` / `horizontalBand` / `verticalBand`
- 配置存取：`engine::loadEngineConfig` / `saveEngineConfig`（可选 `AnnotationLayer*` 参：加载还原 / 保存附带 SPEC §7 `annotations` 段）
- 标注文本解析：`engine::parseAnnotationsFromJsonText`（内联对象 / 根数组 → `vector<Annotation>`，供 `--annotation` / `--annotations` 复用；CLI 不引 nlohmann/json）
- 标注烧录：`annotation::AnnotationLayer::burnIn(base)` / `restoreAnnotations` / `annotations()`（把命令行 / 配置标注烧进工作图）
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
