# ImageDiscropperCli

**模块作用**：ImageDiscropper 的命令行层，产出可执行程序 **`idc`**。
它是一层**薄壳**：只做四件事——解析参数、调用 Core 的统一引擎、格式化输出、返回退出码；
**不含任何切割 / 几何 / 排序 / 极性 / 合成 / 编解码逻辑**，全部委托 `ImageDiscropperCore`。

三层模式共用 Core 的同一条 Grid-Selection-Export 代码路径（NFR-0：模式即参数预设）：

| 命令        | 层级      | 极性            | 输出                                                                                       |
|-----------|---------|---------------|------------------------------------------------------------------------------------------|
| `extract` | L1 标准提取 | 恒 keep        | 合并单图（坍缩）                                                                                 |
| `erase`   | L2 反向剔除 | 恒 remove      | 分离到文件夹 / 坍缩（重排为 L3 专属，`erase` 拒绝 `--merge rearrange`）                                    |
| `grid`    | L3 网格分割 | keep 或 remove | 分离 / 重排合并（`--merge-sort`/`--merge-decorate` 控制填充顺序）；块长宽不全等时按准入门限回退（可坍缩→坍缩，否则→分离，后者退出码 2） |
| `config`  | 配置驱动    | 由 JSON 决定     | 由 JSON 的 export 决定                                                                       |
| `new`     | 新建画布    | 不涉及（不切割）      | 程序生成一张画布（尺寸 / 底色 alpha / 图案）并写盘                                                          |

> **输入来源**：`extract` / `erase` / `grid` / `config` 的输入图像既可 `--input <file>` 读文件，
> 也可用 `--size WxH`（可配 `--pattern` 等）**程序生成一张画布**作为输入（`new` 的同一套生成选项）；
> 二者**互斥、择一**。`config` 还支持 `layers` 段（§7 多层画布）作为**叠加图层**来源：`layers` 仅承载叠加层、不含底图，底图 base 由 `--input` / `--size` / `generate` 段（三者可组合）提供；`layers` 存在时由 CLI 逐层解码 `image.file` 源图（相对 `<配置名>.assets/` / 配置目录，assets 优先）注入像素、`canvas::composeCanvas(layers, w, h, base)` 先铺全透明底、依底图 base→叠加层（按 zOrder）展平为 `config.source` 尺寸的画布（画布无级背景；引擎仍只收一张展平画布图，runEngine 零改动）；无底图时叠加层直接铺于全透明底之上（不视为错误）。
> `new` 则是「创建而非切割」，只接收生成选项（非 dry-run 时需 `--output`），拒绝 `--input`。

> **配置持久化边界**：CLI 只读写 **SPEC §7 作业参数**（`--save-config` 序列化 / `config --load` 反序列化执行，均委托 Core `saveEngineConfig`/`loadEngineConfig`）；**GUI 的画布配色 / 交互偏好（QSettings `palette/`、`canvas/` 组）不属 §7，CLI 不感知也不读写**——CLI 零 Qt 依赖，两类持久化正交、互不触碰（权威边界条文见 [SPEC §7](../../SPEC.md)）。

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

> **Windows 路径 / 参数编码**：入口 `main` 在 Windows 下改用宽字符 `__wargv` 并经 `WideCharToMultiByte(CP_UTF8, …)`
> 转为 **UTF-8** 传下去（非 Windows 保留窄 `argv`）；Core 的文件打开（`readImageFile`/`writeImageFile`、配置读写、导出）
> 统一用 `core::pathFromUtf8` 按 UTF-8 解释路径——全链路 UTF-8 契约一致，中文 / 非 ASCII 的输入输出路径均可正常读写。

## 用法速览

```bash
idc <command> [options]
idc --help | --version

# L1：保留中心矩形
idc extract --input photo.jpg --rect 100,100,300,250 --output out.png
# L1：保留中心矩形（几何坐标也可写百分比，按输入图宽 / 高换算——同串内可与整数混写）
idc extract --input photo.jpg --rect 25%,25%,75%,75% --output center.png
# L2：十字切割，坍缩合并 / 分离导出
idc erase  --input photo.jpg --rect 100,100,300,250 --merge collapse --output out.png
idc erase  --input photo.jpg --rect 100,100,300,250 --output-dir ./out/ --format png
# L3：网格保留四角并重排到 2x2 画布（--compose 时 --canvas 的 cols/rows 必填且须为正）
idc grid   --input photo.jpg --grid 100,100,200,150 --keep 0,0 --keep 0,2 --keep 2,0 --keep 2,2 \
           --compose --canvas 2x2 --output result.png
# L3：重排时另控填充顺序（与 --sort 选择排序正交；--merge-sort 不支持 custom）
idc grid   --input photo.jpg --grid 100,100,200,150 --keep 0,0 --keep 0,1 --keep 1,0 --keep 1,1 \
           --compose --canvas 2x2 --merge-sort column-major --merge-decorate snake --output result.png
# L3：按轴定义——横向 3 等分（基准点恒 0、末份吸收误差）+ 纵向周期 150px
idc grid   --input photo.jpg --divide-x 3 --grid-y 0,150 --keep 0,0 --output-dir ./cells/
# L3：两轴都按比例（1,2,1 与 25%,50%,25% 等价，只取相对比例）
idc grid   --input photo.jpg --weights-x 1,2,1 --weights-y 25%,50%,25% --keep 0,1 --output-dir ./cells/
# 配置驱动（若 JSON 含可选 annotations 段，执行前会把标注烧录进预处理后的工作图）
idc config --load my-config.json --input photo.jpg --output result.png

# 新建：程序生成一张 1024x768 棋盘格（白底 / 半透明蓝纹）并写盘
idc new --size 1024x768 --pattern checkerboard --bg "#FFFFFFFF" --fg "#800000FF" --period 64 --output board.png
# 生成源切割：用程序生成的径向渐变画布作为输入（--size 替代 --input），保留中心矩形
idc extract --size 200x200 --pattern radial-gradient --bg "#FF000000" --fg "#FFFFFFFF" --rect 50,50,150,150 --output grad.png

# 命令行标注：三层命令 + config 均可附加标注（SPEC §7 annotations 元素语法，坐标基准＝预处理产出）
#   --annotation '<内联 JSON 对象>'：单条标注，可重复；--annotations <file.json>：文件根为标注数组，可重复
idc extract --input photo.jpg --rect 100,100,300,250 \
            --annotation '{"type":"circle","x1":150,"y1":150,"x2":180,"y2":150,"color":"#FFFF0000","fill":true}' \
            --output out.png
# 导出：--save-config 会把命令行标注一并透写进配置 JSON 的 annotations 段
idc extract --input photo.jpg --rect 100,100,300,250 --annotations annos.json \
            --save-config cfg.json --output out.png
```

完整选项见 `idc --help <command>`。

**网格定义（`grid` 命令，SPEC §3.4.3）——每轴三选一，两轴可各用各的**：

| 选项                                    | 含义                                            |
|---------------------------------------|-----------------------------------------------|
| `--grid x0,y0,cw,ch`                  | 两轴周期：基准点 + 单元尺寸（`x0/y0` 可负，`cw/ch` 须为正；可写百分比） |
| `--divide ColxRow`                    | 两轴分割数（`3x4`＝3 竖 × 4 横）；基准点恒 `0`、尽量均分、末份吸收取整误差 |
| `--grid-x x0,cw` / `--grid-y y0,ch`   | 单轴周期；未定义的轴＝该方向 1 份                            |
| `--divide-x <n>` / `--divide-y <n>`   | 单轴分割数（≥1 且 ≤ 该方向像素数）                          |
| `--weights-x w,…` / `--weights-y w,…` | 单轴比例：正数或正百分比串，**只取相对比例**、末份吸收取整误差             |

> **冲突**：`--grid` 与 `--divide` 二选一，且二者都不得与按轴选项（`--grid-x/--grid-y`、`--divide-x/--divide-y`、`--weights-x/--weights-y`）同用；同一轴只给一种形态——违反即参数错误（退出码 1）。
> **余量策略**（`--margin`）仅对周期轴生效：分割数 / 比例恒恰好铺满、不产生残缺单元。
> **合并准入（SPEC §4.3）**：`--compose` 要求各保留块**长宽全等**（重排按 `cols×rows` 整数格落位）；不全等但可坍缩 → 自动改为坍缩合成（`--canvas` 不再生效）；也不可坍缩 → 降为分离导出（单文件路径无以为继，报**退出码 2**并提示改用 `--output-dir`）。

> **主题帮助**：`idc --help <目标>` 命令名 → 该命令帮助；主题名 → 三套作业级通用选项的自包含说明页（`annotations` / `layers` / `generate`）；无目标 → 顶层帮助。

> **输出详细度（verbosity）**：默认静默（沉默是金）——正常结果行（数据）恒写 `stdout`、WARN / 错误三段式恒写 `stderr`；`--verbose`（短写 `-v`，位置无关）打开进度诊断日志。

> **输出语言**：帮助与报错文本可选中英文（`idc --help` 的 `--lang <code>` 行即当前支持的语言码）。
> 优先级 `--lang <code>` > `IDC_LANG` > `LC_ALL` > `LANG` > `zh-CN`（源语言）；`--lang` 写了但不认识→退出码 1 并报因。
> **只影响给人看的文案**：选项名 / 子命令名 / §7 JSON 键名与枚举值属契约，恒为字面量不随语言变；
> 退出码、`stderr` 的三段式结构与日志前缀也不因语言而变。zh-CN 下输出与未接 i18n 时逐字节一致（源语言不建表、全部回退常量层）。
> 译文表住 [`src/i18n/lang_en.cpp`](src/i18n/lang_en.cpp)（msg）与 [`src/i18n/lang_en_help.cpp`](src/i18n/lang_en_help.cpp)（help），查表与语言状态本体在 Core（见 [`../README.md`](../README.md)「三层共用的 i18n」）。

## 新建与图像生成（`new` / 生成源）

与 GUI「新建」对话框一一对应，图案生成本体委托 Core 的 `generate::makePattern`（CLI 不实现任何像素 / 图案逻辑）。

| 选项                  | 取值                                                                                                                                                                                                                                                            | 默认                |
|---------------------|---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------|-------------------|
| `--size WxH`        | 像素尺寸（正整数；单边 ≤ 65535 且 宽×高 ≤ 64,000,000≈256MB）                                                                                                                                                                                                                 | new 必填；作生成源时必填    |
| `--pattern <kind>`  | `solid` / `linear-gradient` / `radial-gradient` / `checkerboard` / `stripes` / `grid-lines` / `dot-grid` / `concentric` / `radial-rays` / `hue-gradient` / `noise`（斜向条纹由 `stripes` + `--orientation` 表达、方环由 `concentric` + `--orientation` 表达；`image` 不是图案类型） | `solid`（纯色）       |
| `--orientation <d>` | `horizontal` / `vertical` / `diagonal-down`(↘) / `diagonal-up`(↗)（线性渐变轴 / 条纹 4 向含斜纹 / 同心 `vertical`＝方环否则圆环 / 色相渐变扫描轴；接受缩写 h/v）                                                                                                                                | `horizontal`      |
| `--bg <hex>`        | 底色 `#RRGGBB` / `#AARRGGBB`（**alpha 生效**，`00` 全透~`FF` 全不透）                                                                                                                                                                                                     | `#FFFFFFFF`（不透明白） |
| `--fg <hex>`        | 前景 / 图案色（渐变终点色）                                                                                                                                                                                                                                               | `#FF000000`（黑）    |
| `--period <n>`      | 周期 / 格子边长 / 环间距 / 条纹（含斜纹）带间距 / 色相波长（px）                                                                                                                                                                                                                       | 32                |
| `--line-width <n>`  | 网格线 / 同心环（圆环·方环）线宽（px）                                                                                                                                                                                                                                        | 1                 |
| `--dot-radius <n>`  | 点阵点半径（px）                                                                                                                                                                                                                                                     | 2                 |
| `--seed <n>`        | 噪声种子（非负整数；同种子 + 同尺寸恒同图，供确定性复现 / C++ Rust 对拍）                                                                                                                                                                                                                  | 0                 |
| `--noise <mode>`    | 噪声配色：`full-color`（全彩）/ `grayscale`（灰度）/ `two-color`（双色 bg fg）                                                                                                                                                                                                 | `full-color`      |

> 上表选项仅对应图案生效（如 `solid` 只看 `--bg`、`--orientation` 只对 `linear-gradient`/`stripes`(含斜纹 4 向)/`concentric`(vertical＝方环)/`hue-gradient` 有效、`--line-width` 只对 `grid-lines`/`concentric` 有效、`--dot-radius` 只对 `dot-grid` 有效、`--seed`/`--noise` 只对 `noise` 有效）；完整「图案×参数适用矩阵」见 `idc new --help`。图片不作为图案类型：CLI 侧图像输入仅 `--input`（读文件）与 `--size`（生成图案）两条路径，带图的叠加层只能经配置 `layers` 段（§7）传入，命令行无挂图参数。
> `new` 另支持 `--output`（非 dry-run 时必填）/ `--format`（缺省由扩展名推断）/ `--quality <0~100>`（有损质量，默认 90，仅 JPEG/WebP 生效）/ `--pad-color <hex>`（有损格式压平透明像素所用背景色，透写 `export.padColor`，默认透明→黑底）；生成图作为切割命令的输入时，走同一 `--input` 读图之后的标准流程。
> `new --save-config <file>` 为 dry-run：不落像素，而把本次生成配方（`source` 尺寸 + SPEC §7 `generate` 段 + `export`）序列化进配置，供 `config --load` 据其复现画布（此路径不要求 `--output`）。
> `--quality` 为通用导出选项：`extract` / `erase` / `grid` / `new` 均可传入（经 `applyExportOptions` 透写 `export.quality`，`config` 则由 JSON 的 `export.quality` 决定）。
> 尺寸非法（<=0 / 单边超 65535 / 宽×高超 64,000,000）、图案名 / 颜色 / 方向 / 周期 / 种子 / 噪声模式非法均退出码 1（早于生成，分配前短路不至 OOM）；`--input` 与 `--size` 同时出现→退出码 1；`--quality` 非 0~100 整数→退出码 1；`--pattern image` 非法（图片不作为图案类型），传入报「不支持的图案类型」退出码 1。

> **标注入口**：五命令（`extract` / `erase` / `grid` / `new` / `config`）均支持命令行标注输入——`--annotation '<内联 JSON 对象>'`
> （一条 SPEC §7 `annotations` 元素，可重复）与 `--annotations <file.json>`（文件根为标注数组，可重复）；两者可混用、累积追加。
> `config` 命令还会把命令行标注**追加**在其 `--load` 配置自带的 `annotations` 段之后。
> 标注解析委托 Core 的 `parseAnnotationsFromJsonText`（内联与文件同一入口），CLI 不自行解析 JSON。
>
> **标注烧录**：`executeJob` 先物化 `preprocess` 为工作图，再把标注烧录进该工作图（坐标基准＝预处理产出），
> 故标注随像素一起被切割落位（标注不受画布预处理影响）。
> `--save-config` dry-run 会把「配置自带 + 命令行」标注一并透写 `annotations` 段（往返不丢）。

> **图层入口**（五命令通用，与标注对称）：`--layer '<内联 JSON 对象>'`（一条 SPEC §7 `layers` 元素，可重复；另收两种等价简写——平铺 `{file=a.png,imageOpacity=200,backdrop={"kind":"solid"}}` 外层大括号逗号分隔 / 块形 `image={...} backdrop={...}` 空白或逗号分隔，平铺键 imageFit/imageAnchor/boxWidth 等自动映射进 image 元素）与
> `--layers <file.json>`（文件根为图层数组，可重复）；两者可混用、累积。`config` 命令**追加**在 `--load` 配置自带的
> `layers` 段之后（`zOrder` 未给 → 按「既有最大 zOrder + 1」自增续尾）；`extract` / `erase` / `grid` / `new` 作为**叠加层合成在
> 输入 / 生成画布之上**（`zOrder` 未给从 0 起自增；`file` 资产按当前工作目录解析）。合成统一走 `buildLayerCanvas` 一条展平路径。
> `transform` 的 `x/y/width/height` 可写百分比串（如 `"50%"`，>100% 与负值合法），基准＝**画布**尺寸（`extract`/`erase`/`grid`/`new` 为输入图 /
> 生成画布，`config` 为解出的画布）——需先解图才有基准，故换算与形态校验发生在读图之后（仍为退出码 1）。
> 解析委托 Core 的 `parseImageLayersFromJsonText`（内联与文件同一入口），CLI 不自行解析 JSON；
> 非法 JSON / 根类型非法 / 解析不出有效图层 → 参数错误（退出码 1），`image.file` 相对名越界仍按退出码 3。
> `--save-config` dry-run 会把「配置自带 + 命令行」图层一并透写 `layers` 段（往返不丢）。

## 多层画布与资源打包（`layers` 段 / `--assets-dir`）

`config --load` 的配置可携带 SPEC §7 `layers` 段（可选）描述叠于底图之上的**叠加图层**（`layers` 仅承载叠加层、不含底图——底图走 `--input`/`--size`/`generate` 段）：

> **术语对齐 [SPEC §2.2](../../SPEC.md)**：此处「底图 base」即 §2.2 的**底图**（同构 `ImageLayer`、钉最底、不入 `layers`），`--input` 读入的原图 / `generate` 段图案（+附图片落位）承载的即**底图图片**；`layers` 数组的成员即**图层**（＝图层框节点：有**图片元素**即图片层、其 `image.file` 引用的源图为**图层图片**（位图靠 `<配置名>.assets/` 引用、不入配置）；无图片元素而有**图案元素**（`backdrop`）即纯图案层（非图片））。

- **叠加层构成**：`layers` = 叠加 `ImageLayer` **数组**（不含底图）；画布**无级背景**（`composeCanvas` 恒铺全透明底）；每层携
  `image` 图片元素（可选：`file` 相对文件名 + `fit`/`boxWidth`/`boxHeight`/`anchor`/`pivot`/`x`/`y` 内层落位 + 元素外观 `preprocess`/`opacity`）、
  `backdrop` 图案元素（可选，缺省透明底：图案键族与 `generate` 同源 + 元素外观 `preprocess`/`opacity`）、
  `transform`（画布锚点 + 图片枢轴 + 自由 `x,y` + 目标宽高 + **不透明度 `opacity`**——合成期 alpha 乘子 `a'=(a·opacity+127)/255`，缺省 255＝不透明、灰度层亦生效）、`zOrder`（叠放序）、节点外观 `preprocess`（**不含 `opacity`**——不透明度只由 `transform.opacity` 承载；队列中出现 `opacity` 步时按未知步跳过，见 SPEC §7）。
- **执行链**（`buildLayerCanvas`）：逐层解码 `image.file` 源图（先「`<配置名>.assets / 相对名」后「配置目录 / 相对名」，assets 优先）注入像素
  → `canvas::composeCanvas(layers, w, h, base)` 依底图 base→叠加层展平（画布无级背景、恒铺全透明底；base 由 `--input`/`--size`/`generate` 得来、非空则先铺 (0,0)），叠加层按 z 序逐层物化（已含缩放与逐层预处理）→按 `transform.opacity` 逐像素乘 alpha→source-over 落位、越界自然裁剪 → 得展平画布图 → 交 `runEngine`（引擎零改动）；
  **缺文件降级为空占位**（合成时跳过），不报错；但**恒向 `stderr` 打一条 `idc: warn:` 汇总**（与 `--verbose` 无关）（提示缺失个数），使无人值守批量运行可见产出降级。加全局 **`--strict`** 则缺文件不再降级，**直接报退出码 3**。
  解码失败（文件存在但非图像 / 不可读）始终报退出码 3，与 `--strict` 无关。
- **`--assets-dir` 三态**（作业级通用选项，写在子命令之后）：缺省＝不打包、纯约定查找；flag＝与 `--save-config` 同用时把各图片层 `image.file` 源图按**相对名**（不哈希）
  复制进 `<配置名>.assets/`，使配置可携资源整体分发。相对名含 `..` / 绝对路径 / 盘符 / root_name 一律**越界拒绝**
  （执行时退出码 3；打包时退出码 1）。`=path` 形式：加载时插查找链最前、打包输出到该目录、目录在保存配置目录之内时写 §7 `assetsDir` 字段；同路径（src==dst）幂等跳过。
- **可复现性**：C++/Rust 逐像素对拍仅覆盖纯图案层（backdrop 图案据配方现算）；图片层只对拍**结构**（落位 / z 序 / 管线声明）。

```bash
# 读取含 layers 段的配置（file 层从配置同目录或 <配置名>.assets/ 取图）
idc config --load canvas-config.json --output out.png
# 保存配置时把 file 层源图一并打包进 <配置名>.assets/
idc config --load canvas-config.json --save-config bundle.json --assets-dir
```

## 标注使用方法

两个入口把标注追加到切割流程中，五命令（`extract` / `erase` / `grid` / `new` / `config`）通用，可混用、按出现顺序累积：

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
- **坐标基准**＝工作图（非原图）：`extract` / `erase` / `grid` / `new` 无预处理选项，工作图即输入图 / 生成画布；`config` 为解出的画布经 `preprocess` 重放后的尺寸。几何字段始终为**局部坐标**，`transform`（整体快照）恒等时省略、一般无需手填。
- **百分比坐标**：几何字段（`x/y`、`x1..y2`、`points`、`segments[].coords`）可写 `"50%"` 形式的串，按**工作图**宽 / 高换算（四舍五入，见 [SPEC §2.1](../../SPEC.md)）；`fontSize` / `cornerRadius` / `strokeWidth` / `zOrder` 不收百分比。
- **强制校验、逐条报因**（任一条非法即整段退出码 1）：缺/未知 `type`；锚点退化（矩形/菱形需 |Δx|、|Δy| 均 >0，正方形/等腰直角需 max >0，圆需半径 >0、椭圆需两半轴 >0 等）；`line` 点数≠2；`text` 的 `fontSize`≤0；`path` 无有效段；非法 `#RRGGBB`/`#AARRGGBB` 颜色；非整数 `strokeWidth` / `zOrder`（`zOrder` 为小数/字符串即判非法；负数合法，归一化为非负下标）。stderr 逐条列出「第 N 个元素：原因」，不静默丢弃。`config --load` 的**配置自带 `annotations` 段**非法时同口径：Core 已拼好的逐条原因（「第 N 个标注元素：……」）直接作为错误输出的 `详情` 行原样上抛，不退化为一条通用「无法按配置 JSON schema 解析」（IT-53）。

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

## 报错优先于静默回退（输入拒绝与严格模式）

CLI 对所有「传了却不生效」的输入采取**拒绝优先**策略，杜绝静默吞掉（完整契约见 [`SPEC.md`](../../SPEC.md) §8.1）。分两类：

- **本命令不接受的输入**（命令解析完 token 后立即校验，任一命中报**参数错误、退出码 1**）：
  ① 未知 / 拼错的选项（如 `--strct`）；② 跨命令误用（选项合法但不属本命令，如 `extract --keep`、`grid --rect`）；
  ③ 重复给出的单值选项（如 `--input` 给两次，第二次本会静默失效）；④ 命令后的游离位置参数。合法多值选项
  `--rect` / `--keep` / `--remove` / `--annotation` / `--annotations` / `--layer` / `--layers` 与 flag 选项不计入 ③。
- **`--strict` 伞形严格模式**（把三处「默认降级 + `stderr` 一条 `idc: warn:`」升级为报错，供无人值守脚本据非零退出码中断）：
  · `layers` 段缺文件：默认降级空占位 + WARN，`--strict` → **退出码 3**；
  · 声明 `source` 尺寸 ≠ 实际 `--input` 图尺寸且未用 `resize` 预处理：默认按交集静默裁切 + WARN，`--strict` → **退出码 3**；
  · 引擎成功但零导出单元：默认 WARN，`--strict` → **退出码 2**（保留集为空多已被引擎业务错误退出码 2 覆盖，此项为防御性兜底）。
  上述 WARN 恒写 `stderr`、与 `--verbose` 无关。

> **行为契约**（参数规则 / 运行修饰与作业级通用选项 / 退出码 / 错误输出格式）见
> [`SPEC.md`](../../SPEC.md) §8.1。

## 使用的 Core API（接口确认清单）

- 引擎编排：`engine::runEngine` / `EngineConfig` / `EngineResult`
- 网格几何：`engine::Grid::build` / `cellAt` / `cellCount`（把 `r,c` 映射为线性 `index`）
- 区域：`engine::RectRegion` / `horizontalBand` / `verticalBand`
- 配置存取：`engine::loadEngineConfig` / `saveEngineConfig`（可选 `AnnotationSet*` 参：加载还原 / 保存附带 SPEC §7 `annotations` 段；第 4 出参 `std::string* annoErr` 逐条回传标注段非法原因，`config --load` 直接透传为错误详情（GUI 不消费此参、可传 `nullptr`）；`saveEngineConfig` 另可接 `const core::PercentHints*`：把「以百分比载入且未被编辑」的几何键原样回写 `"N%"`，`config --load … --save-config` 据此不把百分比固化成像素；`EngineConfig` 含可选 `generate` 段（`new --save-config` 写入、`config --load` 据其生成源画布）与可选 `layers` 段）
- 多层画布合成：`canvas::composeCanvas(layers, w, h, base)`（`config --load` 带 `layers` 时由 `job::buildLayerCanvas` 以已解析底图 base 为基、逐层解码叠加 `file` 源图注入后展平为工作图）；资源打包由 `job::bundleLayerAssets` 完成（`--assets-dir`）
- 标注文本解析：`engine::parseAnnotationsFromJsonText`（内联对象 / 根数组 → `vector<Annotation>`，供 `--annotation` / `--annotations` 复用；CLI 不引 nlohmann/json）
- 标注烧录：`annotation::AnnotationSet::burnIn(base)` / `restore` / `annotations()`（把命令行 / 配置标注烧进工作图）
- 图像 I/O：`io::readImageFile`（解码）/ `io::writeImageFile`（示例 / 测试造图）——路径按 **UTF-8** 解析（`pathFromUtf8`），支持中文 / 非 ASCII 路径
- 导出落盘：`engine::exportImage`（`new` 经 `job::writeGeneratedImage` 构造全画布单片段 Composition 复用同一落盘通道：扩展名推断格式 / 自动建父目录 / JPEG 压平）
- 图案生成：`generate::makePattern(w, h, PatternParams)`（`new` 与生成源均委托；尺寸 / 图案名 / 颜色等参数装配见 `src/generate_args.cpp`，CLI 不实现像素逻辑）
- 基础类型：`core::Image` / `core::Color` / `kTransparent`

## 已知限制

1. **合并导出的 JPEG/BMP 透明压平**：不支持透明的格式会把 alpha 压平到 `padColor`；
   `new` 可用 `--pad-color <hex>` 显式指定压平底色；未指定时 `export.padColor` 缺省为透明黑，转 JPEG 后表现为黑色背景（属格式固有特性，非缺陷；透明画布导出 JPEG 时建议给 `--pad-color`）。

## 许可

本模块随仓库整体采用 **GPL-3.0**（见根 [`LICENSE`](../../LICENSE)）。
