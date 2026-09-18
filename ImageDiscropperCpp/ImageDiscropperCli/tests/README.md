# tests

**目录作用**：CLI 的单元测试与集成测试（guideline §9）。使用**自研极简 CHECK 宏**（不引第三方框架，
A-0.2/A-0.3：依赖须与 Core 一致，仅用标准库），与 Core 的 `tests/test_harness.h` 同构，风格统一。
产出单一可执行 `cli_tests`，由 `ctest` 运行（§9.3：一条命令、不依赖网络、不污染仓库）。

**分块依据**：`main` 只做调度；各测试段按关注点分文件（严禁上帝文件），经 `cli_test_harness.h` 声明连接。

| 文件                      | 职责                                                                 |
|-------------------------|--------------------------------------------------------------------|
| `cli_test_harness.h`    | 共享 `CHECK` 宏（失败打印文件/行号/表达式并 `exit(1)`）+ 三个测试段声明                    |
| `test_main.cpp`         | 入口 `main`：依次调度 `testArgParser → testValueParser → testIntegration` |
| `test_arg_parser.cpp`   | 单元（§9.1）：`ArgParser` 语法归类 + `parseGlobalOptions` 全局选项              |
| `test_value_parser.cpp` | 单元（§9.1）：`value_parser` 全部「字符串 → 类型」解析与格式校验（成功 + 失败两路）             |
| `test_integration.cpp`  | 集成（§9.2）：IT-1~IT-18，进程内调用 `cliMain`，校验退出码 / 输出尺寸 / 像素落位 / 错误文本     |

## 集成测试做法（IT-1~IT-18）

- **进程内调用**：`cli_tests` 链接 `idc_cli_lib`，直接调 `cliMain`（不启动子进程）；用 `rdbuf` 重定向
  `std::cout`/`std::cerr` 到 `ostringstream` 捕获输出，调用后恢复。
- **合成测试图**：程序化生成 200×150 四象限纯色图（每象限 100×75，恰好对齐 `--grid 0,0,100,75` 的 2×2 网格），
  用 Core `writeImageFile` 写入系统临时目录——**不依赖仓库内示例图、不依赖网络**（§9.3）。
  象限↔单元映射：`cell(0,0)=index0=红`、`(0,1)=1=绿`、`(1,0)=2=蓝`、`(1,1)=3=黄`。
- **验证点**：输出图用 Core `readImageFile` 读回，校验尺寸与关键像素颜色（阈值判定，容忍编码微误差）；
  分离导出校验文件数量与各文件尺寸；错误场景校验退出码与 stderr 文本（如 §5.2 前缀、坍缩提示含 “rearrange”）。
- **临时目录**：`<system-temp>/idc_cli_integration_test`，测试开始 `remove_all`+`create_directories`，结束再 `remove_all` 清理。

## 覆盖矩阵

| 编号       | 场景                                                            | 验证点                                            |
|----------|---------------------------------------------------------------|------------------------------------------------|
| IT-1~3   | `extract --rect/--hband/--vband`                              | 输出尺寸正确（100×80 / 200×80 / 100×150）              |
| IT-4     | `erase --rect --output-dir`                                   | 分离 4 个文件，尺寸两两为 50×40 / 50×30                   |
| IT-5~7   | `erase --rect/--hband/--vband --merge collapse`               | 坍缩尺寸 (W−Δx)×(H−Δy) = 100×70 / 200×70 / 100×150 |
| IT-8     | `erase` 多矩形并集 collapse                                        | 130×80（删整列 70 + 整行 70）                         |
| IT-9~11  | `grid --keep/--sort column-major/--sort custom` + `--compose` | 画布尺寸与像素落位符合排序序列                                |
| IT-12    | `config --load` 执行                                            | 配置参数全部生效（等价 IT-5，100×70）                       |
| IT-13~14 | `--version` / `--help`                                        | 版本格式 / 含全部子命令                                  |
| IT-15~18 | 缺必填 / 输入不存在 / 输出目录不存在 / 坍缩不可行                                 | 退出码 1 / 3 / 4 / 2 + stderr 提示                  |
