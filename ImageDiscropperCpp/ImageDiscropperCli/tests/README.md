# tests

**目录作用**：CLI 的单元测试与集成测试。使用**自研极简 CHECK 宏**（不引第三方框架，
依赖须与 Core 一致，仅用标准库），与 Core 的 `tests/test_harness.h` 同构，风格统一。
产出单一可执行 `cli_tests`，由 `ctest` 运行。

**分块依据**：`main` 只做调度；各测试段按关注点分文件（严禁上帝文件），经 `cli_test_harness.h` 声明连接。

| 文件                      | 职责                                                                 |
|-------------------------|--------------------------------------------------------------------|
| `cli_test_harness.h`    | 共享 `CHECK` 宏（失败打印文件/行号/表达式并 `exit(1)`）+ 三个测试段声明                    |
| `test_main.cpp`         | 入口 `main`：依次调度 `testArgParser → testValueParser → testIntegration` |
| `test_arg_parser.cpp`   | 单元：`ArgParser` 语法归类（含负数值消费）+ `parseGlobalOptions` 全局选项             |
| `test_value_parser.cpp` | 单元：`value_parser` 全部「字符串 → 类型」解析与格式校验（成功 + 失败两路）                   |
| `test_integration.cpp`  | 集成：IT-1~IT-53，进程内调用 `cliMain`，校验退出码 / 输出尺寸 / 像素落位 / 错误文本           |

## 集成测试做法（IT-1~IT-53）

- **进程内调用**：`cli_tests` 链接 `idc_cli_lib`，直接调 `cliMain`（不启动子进程）；用 `rdbuf` 重定向
  `std::cout`/`std::cerr` 到 `ostringstream` 捕获输出，调用后恢复。
- **合成测试图**：程序化生成 200×150 四象限纯色图（每象限 100×75，恰好对齐 `--grid 0,0,100,75` 的 2×2 网格），
  用 Core `io::writeImageFile` 写入系统临时目录——**不依赖仓库内示例图、不依赖网络**。
  象限↔单元映射：`cell(0,0)=index0=红`、`(0,1)=1=绿`、`(1,0)=2=蓝`、`(1,1)=3=黄`。
- **验证点**：输出图用 Core `io::readImageFile` 读回，校验尺寸与关键像素颜色（阈值判定，容忍编码微误差）；
  分离导出校验文件数量与各文件尺寸；错误场景校验退出码与 stderr 文本（如 `idc: error:` 前缀、坍缩提示含 “rearrange”）。
- **临时目录**：`<system-temp>/idc_cli_integration_test`，测试开始 `remove_all`+`create_directories`，结束再 `remove_all` 清理。

## 覆盖矩阵

| 编号       | 场景                                                                             | 验证点                                                                                                                                           |
|----------|--------------------------------------------------------------------------------|-----------------------------------------------------------------------------------------------------------------------------------------------|
| IT-1~3   | `extract --rect/--hband/--vband`                                               | 输出尺寸正确（100×80 / 200×80 / 100×150）                                                                                                             |
| IT-4     | `erase --rect --output-dir`                                                    | 分离 4 个文件，尺寸两两为 50×40 / 50×30                                                                                                                  |
| IT-5~7   | `erase --rect/--hband/--vband --merge collapse`                                | 坍缩尺寸 (W−Δx)×(H−Δy) = 100×70 / 200×70 / 100×150                                                                                                |
| IT-8     | `erase` 多矩形并集 collapse                                                         | 130×80（删整列 70 + 整行 70）                                                                                                                        |
| IT-9~11  | `grid --keep/--sort column-major/--sort custom` + `--compose`                  | 画布尺寸与像素落位符合排序序列                                                                                                                               |
| IT-12    | `config --load` 执行                                                             | 配置参数全部生效（等价 IT-5，100×70）                                                                                                                      |
| IT-13~14 | `--version` / `--help`                                                         | 版本格式 / 含全部子命令                                                                                                                                 |
| IT-15~18 | 缺必填 / 输入不存在 / 输出目录无法创建（同名文件占用） / 坍缩不可行                                         | 退出码 1 / 3 / 4 / 2 + stderr 提示                                                                                                                 |
| IT-19~20 | `grid --merge-sort column-major` / `--merge-decorate reverse` + `--compose`    | 填充路径列优先 / 整体逆序（与选择排序正交）                                                                                                                       |
| IT-21    | `erase --merge rearrange`                                                      | 退出码 1 + 提示（重排为 L3 专属，L2 直接拒绝）                                                                                                                 |
| IT-22    | `extract --annotation` 内联矩形标注                                                  | 退出码 0；标注烧进工作图（内部点变色、未覆盖点保持原色）                                                                                                                 |
| IT-23    | `extract --annotations <file>`（根为数组）                                           | 退出码 0；多条标注一并烧录（绿区 + 蓝区各自变色）                                                                                                                   |
| IT-24    | `extract --annotation '{bad json'`                                             | 退出码 1 + stderr 含“标注”（Core 段解析失败）                                                                                                              |
| IT-25    | `extract --annotation + --save-config`                                         | 退出码 0；写出的配置 JSON 含 `annotations` 段（透写不丢）                                                                                                      |
| IT-26    | `new --size 64x48 --pattern solid --bg "#FFFF0000"`                            | 退出码 0；生成 64×48 全红图（回读尺寸 + 像素色）                                                                                                                |
| IT-27    | `new --bg "#00FFFFFF"`（底色 alpha=00）                                            | 退出码 0；回读全透明画布（alpha==0）                                                                                                                       |
| IT-28    | 生成源切割：`extract --size 200x150 --rect 50,40,150,120`                            | 退出码 0；100×80 全红（--size 替代 --input 后走 L1）                                                                                                      |
| IT-29    | `extract --input` 与 `--size` 同时给出                                              | 退出码 1 + stderr 含“--size”（互斥）                                                                                                                  |
| IT-30    | `new --pattern bogus`                                                          | 退出码 1 + stderr 含“图案”（非法枚举名）                                                                                                                   |
| IT-31    | `new` 缺 --output / 带 --input                                                   | 退出码 1（必填缺失 / new 不接受 --input）                                                                                                                 |
| IT-32    | `new --quality 80`（合法）/ `150` / `abc`                                          | code 0 / code 1（错含“质量”）/ code 1（非整数）                                                                                                          |
| IT-33    | `extract --quality 200`                                                        | 退出码 1 + stderr 含“质量”（导出选项统一校验）                                                                                                                |
| IT-34    | `new --bg "#00FFFFFF" --format jpeg --pad-color "#FF0000FF`                    | 退出码 0；透明画布按 pad-color 压平（回读像素为蓝）                                                                                                              |
| IT-35    | `new --size 32x24 --pattern checkerboard --period 8 --save-config`（无 --output） | 退出码 0；写出的配置含 `generate` 段（kind/period）                                                                                                        |
| IT-36    | `config --load it35.json --save-config`（dry-run 往返）                            | 退出码 0；`generate` 段（checkerboard）读入再写出保持                                                                                                       |
| IT-37    | `config --load`（含 generate 段，无 --input/--size）                                 | 退出码 0；据 source+generate 合成红底→L1 keep rect 得 100×80 全红                                                                                         |
| IT-38    | `new --size 65535x65535`（单边合法但总像素超限）                                           | 退出码 1 + stderr 含“总像素”（分配前短路）                                                                                                                  |
| IT-39    | `new` 四种图案（stripes/concentric/hue-gradient/noise） 64×48 冒烟                     | 退出码 0；均生成非空图（斜向经 stripes --orientation、方环经 concentric --orientation 表达）                                                                       |
| IT-40    | `new --pattern noise --seed 1234` 两次落盘 / `--seed 9999` 一次                      | 同种子字节逐字相等（确定性）；异种子字节不同                                                                                                                        |
| IT-41    | `new --seed -1` / `new --noise bogus`                                          | 退出码 1（种子非法 / 噪声模式非法）                                                                                                                          |
| IT-42    | `new --pattern image` 回归护栏（图片不是图案类型）                                           | 退出码 1 + stderr 含“不支持的图案类型”（`parsePatternKind` 不识别 `image`）；图片层仅能经配置 `layers` 段表达                                                              |
| IT-43    | 无效图案 token 拒绝 + 斜向/方环经 --orientation 表达                                        | `--pattern diagonal-stripes`/`rect-rings`→退出码 1；`stripes --orientation diagonal-down/diagonal-up`、`concentric --orientation vertical`→退出码 0   |
| IT-44    | 多层画布 `layers` 段端到到端（纯图案层）：`config --load` 合成后切割                                | 退出码 0；透明底 + 整幅红色纯图案层 → composeCanvas 展平后据配置切割得预期像素                                                                                            |
| IT-46.7b | `--layer` 双语法（平铺带外层大括号 / 块形）等价归一；未知键 / 缺尾括号报错                                  | 两形态均退出码 0、字段等价落入 v2 元素；`{bogusKey=1}` 与缺尾 `}` 均退出码 1（不截尾静默改义）                                                                                 |
| IT-46.7c | 无内层落位的图片元素（只写 `file` 不写 `fit`）source-over 叠于图案之上                               | 退出码 0；透明源像素透出图案（绿）、不透明源像素覆盖图案（红）——装配序「图案铺底、图片叠上」像素级验证                                                                                         |
| IT-45    | 图片层解码 + `--assets-dir` 打包                                                      | 源图写临时目录、相对名解析→config 合成（图片层 20×20 居中落位、不撑大）；`--assets-dir`（flag）把源图复制进 `<配置名>.assets/`                                                        |
| IT-46    | 图片层越界路径（含 `..`）                                                                | `config` 执行时退出码 3（InputError）+ stderr 含“越界”                                                                                                   |
| IT-47    | 图层大于画布→不撑大、合成自然裁剪                                                              | 输出尺寸恒等于 `source`（不撑大）                                                                                                                         |
| IT-48    | 图层 `transform.opacity`（合成期 alpha 乘子）· PNG 保留 alpha                             | 退出码 0；纯图案层 opacity=90 → 回读像素 alpha==90（PNG 保 alpha）                                                                                           |
| IT-49    | 底图同构 `base`（`generate` 段携 `image`+内层落位+`backdrop`）端到端                          | 退出码 0；`config --load` 据 source 尺寸 `composeBase` 复现蓝源左上 20×20、绿底铺其余；`--save-config` 回写 `generate` 段仅带 `anchor`/`backdrop`/`opacity` 落位键、不写文件路径 |
| IT-50    | 底图整体不透明度：`generate` 段 `opacity=64`                                             | 退出码 0；现算底图不在 `composeBase` 烘焙、只在边界 `applyAlpha` 施加一次 → 回读 alpha==64、颜色不变（非双淡）                                                                 |
| IT-51    | 三元目标端到端：Recipe 底 + 底图色彩预处理（invert）+ 底图 `opacity=180` + 画布 `opacity=200`        | 退出码 0；红→青（色彩步生效）、两边界各乘一次得 alpha==141；配置回写含 `invert` 与两处 `opacity` 键                                                                           |
| IT-52    | `--lang` 全局选项                                                                  | 未知码→退出码 1（报因含可用语言码）；`--lang=en --help`→退出码 0 且用法行 `--lang <code>` 恒在；取值不被误当子命令名；末尾复位回 zh-CN                                                   |
| IT-53    | `config --load` 命中非法 `annotations` 段 / 对照纯 JSON 语法错误                           | 两者均退出码 1；前者**详情逐条透传 Core 报因**（含「第 1 个标注元素：缺少 type 字段」）且提示改为标注语法，不退化为通用 schema 详情；后者仍给通用 schema 详情                                             |
