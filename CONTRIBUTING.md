# 贡献指南

感谢你考虑为 ImageDiscropper 贡献代码或文档！在动手之前，请先了解下面的约定。

> 本项目主要由 AI 主导开发（约 99%）。无论改动来自人工还是 AI，**提交前必须经人工审核**，
> 尤其是许可合规（GPL-3.0）与行为一致性（以 [`SPEC.md`](SPEC.md) 为唯一行为基线）。

## 快速上手

```bash
# 依赖：CMake ≥ 3.28 + C++23 编译器 + vcpkg（设置环境变量 VCPKG_ROOT；包由 vcpkg.json 清单自动安装）
bash assets/icons/regenerate.sh    # 图标派生物不入库：首次克隆/改 icon.svg 后必跑，否则 GUI/移动端构建报错（见 assets/icons/README.md）
cd ImageDiscropperCpp
cmake --preset windows-debug
cmake --build build/windows-debug
ctest --test-dir build/windows-debug --output-on-failure    # Core unit_tests + CLI cli_tests，提交前必须全部通过
```

建议使用 CLion，直接以 `ImageDiscropperCpp/` 为 CMake 源目录打开工程（CLion 会识别 Presets）。
Linux / macOS 用 `linux-debug` / `macos-debug` 预设。
每次 push / PR 由 GitHub Actions（[`.github/workflows/ci.yml`](.github/workflows/ci.yml)）在 **Windows / Linux / macOS 三平台**以 **Release 配置**自动构建（含 GUI，与发布产物同构）并跑同一套测试，同时构建 **Android / iOS** 移动产物（打包验证，交叉编译产物不跑测试）；Debug 配置的全量验证见 [`.github/workflows/cpp-verify.yml`](.github/workflows/cpp-verify.yml)（手动 `workflow_dispatch`，桌面三平台跑 ctest + 移动双端仅构建打包）。

## 代码规范

全仓库统一执行的约定：

- **语言标准**：C++23；命名空间统一（Core 为 `idc`，GUI 为 `idc::gui`）。
- **注释语言**：中文。每个文件头说明文件作用与分块依据；每个函数首说明用途；复杂逻辑分块处注明分块依据。
- **严禁上帝文件**：按功能/任务严格分文件，一个关注点一个文件（或一命令一文件）。
- **文案纪律**：面向人的文案不得内联在使用点，必须进本层常量层（`core_texts.h` / `cli_texts.h` / `gui_texts.h`）并经门面宏取用（`IDC_KTR` / `IDC_CTR` / `IDC_GTR`）；中文即源语言与回退值。
  - **日志不豁免**：stderr 上的详细日志 / 进度提示与报错同属面向人的文案，不得以「机器日志」为由继续内联；但日志的**版式元素**（行首前缀 / 分隔符 / 缩进 / 进度字符）属格式而非文案，不入库、不翻译。
  - **不可译契约永不进语言目录**：GUI `texts::ident`（QSettings 键 / MIME / objectName / 快捷键）、CLI `opt` / `cmd` / `env`、以及 SPEC §7 JSON 的键名与枚举值——它们属行为契约，翻译即破坏互通。
  - 机制与行为基线分别见 [`ImageDiscropperCpp/README.md`](ImageDiscropperCpp/README.md)「三层共用的 i18n」与 SPEC §6 NFR-11，本文不重述。
- **数值纪律**：具业务 / 契约含义的魔数不得散在使用点，进本层数字常量层（`core_nums.h` / `cli_nums.h` / `gui_nums.h`）并按来源模块分组；
  SPEC §7 JSON 的键名与枚举 token 一律走 `keys::` / `defs::` 层（写 / 读两侧同源）。**换算 / 边界常量**（`0` / `1` / `255` 一类 clamp 上下界、单位换算因子）
  与仅在使用点可读的一次性量按判据保持就地，并在行尾注明理由。
- **报因上抛**：下层已逐条 / 按元素拼好的失败原因，上层必须原样透传给人，不得压平成一条通用提示（SPEC §8.1「标注入口」与错误输出格式）。
- **每个子目录必须有 `README.md`**：说明该目录的职责边界、分块依据与文件清单（见下文「文档规范」）。
- **命名风格**：类型 `PascalCase`；函数/变量 `camelCase`；常量 `kConstantName`；命名空间 `lowercase`。
- **错误处理**：对外接口不抛异常，经返回值 / `std::optional` 传递失败。
- **分层纪律**：GUI/CLI 是 Core 的消费者，任何切割/几何/排序/极性/导出/编解码逻辑一律调用 Core，
  不在上层重复实现；发现 Core 缺接口时标注 `// TODO(core): …` 并报告，而非在上层补实现。
- **三层模式共用同一引擎路径**（NFR-0）：新增功能不得为 L1/L2/L3 单独写一套切割逻辑。
- **跨实现行为对齐**（NFR-10）：任何语言重实现须与参考实现（C++）经共享黄金测试逐像素对齐；
  新功能先在参考实现落地并通过黄金测试，方可宣称完成。

## 文档规范

文档分七层，每层内容范围在 [`README.md`](README.md)「文档索引」中定义，写作时请勿跨层重复：

| 层级   | 位置                                                             | 写什么 / 不写什么                   |
|------|----------------------------------------------------------------|------------------------------|
| 仓库首页 | 根 `README.md` / `README.en.md`                                 | 只做导读与导航，**不展开**技术细节          |
| 贡献约定 | 根 `CONTRIBUTING.md`                                            | 构建/测试、代码与文档规范、提交约定           |
| 实现总览 | 各语言实现 `README.md`（如 `ImageDiscropperCpp/README.md`）            | 该实现的组成与需求概念映射、实现状态，细节下沉到模块文档 |
| 功能规格 | `SPEC.md`                                                      | 唯一行为基线。改行为先改 `SPEC.md`       |
| 模块说明 | 各模块 `README.md`                                                | 模块职责、API 映射、构建；不重复目录级细节      |
| 目录说明 | 各子目录 `README.md`                                               | 该目录职责边界、分块依据、文件清单            |
| 占位实现 | `ImageDiscropperRust/README.md`、`ImageDiscropperWeb/README.md` | 各规划中实现的定位、状态与分工说明            |

改动代码时**同步更新受影响目录的 `README.md`**；新增目录必须附带 `README.md`。
`README.md` 与 `README.en.md` 需保持内容一致（中文为主，英文同步更新）。

## 提交与 PR

- **提交信息**采用 Conventional Commits 风格（与现有历史一致）：
  `type(scope): 描述`，如 `feat(engine): …`、`fix(gui): …`、`refactor(core): …`、`docs(readme): …`。
- 从默认分支拉取新分支开发，PR 提交到默认分支。
- PR 需：`ctest` 全部通过；相关 `README.md` 已更新；变更说明写清楚「改了什么、为什么、影响面」。
- 单次 PR 聚焦一个主题，避免夹带无关改动。

## 许可

- 本项目整体采用 **GPL-3.0**（见根 [`LICENSE`](LICENSE)），覆盖各语言的 Core 实现（C++ / 未来的 Rust 等）与 CLI / GUI。
- 提交代码即同意以 GPL-3.0 授权。
