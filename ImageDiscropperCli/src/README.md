# src

**目录作用**：CLI 层的实现代码（除各子命令外的公共部分）。承载「顶层编排 + 参数解析 + 值解析 +
错误报告 + 帮助文本 + 执行桥 + main 入口」。子命令实现另见 `src/commands/`。

**分块依据**：一个关注点一个 `.cpp`（严禁上帝文件，guideline §10.3）。CLI 是薄壳（A-0.1）——本目录
所有文件只负责「解析 / 编排 / 格式化 / 退出码」，不含任何切割 / 几何 / 排序 / 极性 / 合成 / 编解码逻辑，
后者全部委托 Core（A-0.2 不重复造轮子）。

| 文件 | 职责 |
|---|---|
| `main.cpp` | 程序入口：把 `argv`（去程序名）转发给 `cliMain`，返回其退出码 |
| `cli_app.cpp` | 顶层编排 `cliMain`：全局选项 → help/version → 定位子命令 → 分派 → 未捕获异常兜底(5) |
| `arg_parser.cpp` | `ArgParser::parse`（token 归类）+ `parseGlobalOptions`（全局选项扫描） |
| `value_parser.cpp` | 「字符串 → 类型」纯转换与格式校验（严格整数解析、十六进制颜色、枚举、命名模板） |
| `cli_error.cpp` | `reportError` / `makeError`：按 §5.2 三段式格式写 stderr |
| `help.cpp` | 顶层 / 子命令帮助与版本文本（§6）；版本来自编译期宏 `IDC_CLI_VERSION` / `IDC_CORE_VERSION` |
| `job.cpp` | 执行桥：读图 → `runEngine` → 坍缩校验 → `exportImage` → 退出码映射（三层命令 + config 共用） |
| `commands/` | 一命令一文件：`extract` / `erase` / `grid` / `config_command`（见该目录 README） |

**关键约定**：
- 参数校验顺序严格遵循 §5.3（命令存在 → 全局选项 → 必填 → 互斥 → 格式 → 输入可读 → 调 Core），
  前 5 步在调用 Core 之前完成，失败一律退出码 1。
- `runEngine` 在「MERGED+COLLAPSE 但不可坍缩」时会自动降级 REARRANGE 并返回 `ok=true, collapsible=false`；
  CLI 据 `JobOptions::explicitCollapse` 决定是否显式报错为退出码 2（§4.2.2.4）。
