# ImageDiscropper

**图像区域提取、反向剔除与网格分割工具**。

> 一句话定位：常规裁剪只回答「保留哪一块」；本工具回答「**沿哪些线切开、留下哪些块、怎么重新拼起来**」。

功能规格（引擎 / CLI / GUI 的行为契约）见 [`SPEC.md`](../SPEC.md)；贡献约定见 [`../CONTRIBUTING.md`](../CONTRIBUTING.md)。

---

## 1. 应用介绍

所有功能归约为同一条 **Grid-Selection-Emit** 流水线（SPEC §1）：

```text
原图 → ① 切割线集合 → ② 诱导网格 → ③ 选择集 → ④ 极性(Keep/Remove) → ⑤ 排布导出(Collapse/Rearrange)
```

**公理**：切割线是贯穿全图的直线，不是线段。三种模式在数学上是包含关系（L1 ⊂ L2 ⊂ L3），
在产品上并列呈现，但**共用同一套引擎代码**（NFR-0：模式仅为参数预设）：

| 层级     | 模式     | 定位                         | CLI 命令    |
|--------|--------|----------------------------|-----------|
| **L1** | 标准提取模式 | 兼容基线（极性恒 `keep`）           | `extract` |
| **L2** | 反向剔除模式 | ⭐ 差异化内核（按线删除、零伪造、无损）       | `erase`   |
| **L3** | 网格分割模式 | ⭐ 完备表达层（参数化网格 + 显式排序 + 重排） | `grid`    |

---

## 2. 工程组成

采用三层结构（Core / CLI / GUI），Core 作为静态库被上层引用：

```text
GUI ──────┐
          ├── Core
CLI ──────┘
```

| 模块目录                                          | 层    | 产物                            | 职责                                             | 状态             |
|-----------------------------------------------|------|-------------------------------|------------------------------------------------|----------------|
| [`ImageDiscropperCore/`](ImageDiscropperCore) | Core | 静态库 `image_discropper_core`   | 纯逻辑：Grid-Selection-Emit 统一引擎 + 基础图像处理，无 GUI 依赖 | ✅ MVP / v2 已落地 |
| [`ImageDiscropperCli/`](ImageDiscropperCli)   | CLI  | 可执行 `idc` + 静态库 `idc_cli_lib` | 命令行薄壳：解析参数 → 调用 Core → 格式化输出 → 返回退出码           | ✅ 已落地          |
| [`ImageDiscropperGui/`](ImageDiscropperGui)   | GUI  | 可执行 `idc_gui`                 | Qt6 桌面前端：交互式画布、拖拽切割线 / 选区、实时预览、参数面板            | ✅ 已落地          |

顶层 [`CMakeLists.txt`](CMakeLists.txt) 只做「模块编排 + 全局约定」：统一 C++23、
按依赖顺序先引入 Core 再引入 CLI 与 GUI、开放 `ctest`。第三方库（stb / libwebp / nlohmann_json）
由 Core 以 PRIVATE 封装，CLI / GUI 经 Core 公共头间接使用，无需感知；GUI 另需 vcpkg 的 qtbase。

### 2.1 三层共用的 i18n（多语言文案）

三层各有一份**常量层**（`core_texts.h` / `cli_texts.h` / `gui_texts.h`），其中的中文文案即**源语言**；
面向人的可译组经门面宏查表取译文，**不引入 Qt Linguist / `tr()` / `lupdate` / `.ts` / `.qm`**
（lupdate 抽不到常量层的值，且 `.qm` 是 Qt6 内部二进制格式，零 Qt 的 Core / CLI 读不了）。

| 层    | 门面宏                                   | 进目录的组                                           | 英文译文表                                                             |
|------|---------------------------------------|-------------------------------------------------|-------------------------------------------------------------------|
| Core | `IDC_KTR` / `IDC_KTRF`                | `msg`                                           | `ImageDiscropperCore/src/i18n/lang_en.cpp`                        |
| CLI  | `IDC_CTR`                             | `msg` / `help`                                  | `ImageDiscropperCli/src/i18n/lang_en.cpp`（+ `lang_en_help.cpp`）   |
| GUI  | `IDC_GTR`（→`QString`）/ `IDC_GT`（清单表位） | `menu` / `label` / `tooltip` / `status` / `msg` | `ImageDiscropperGui/src/i18n/lang_en.cpp`（+ `lang_en_detail.cpp`） |

约定要点：

- **键 = `分组::常量名`**，由预处理器从常量名拼出，回退值就是同名的中文常量 —— 两处同源，手写键串不可能拼错。
- 三层把表登记给 Core 的同一份**进程级语言状态**；源语言 zh-CN **不建表**，全部走回退，故中文路径逐字节零行为变化。
- **不可译契约永不进表**：GUI `texts::ident`、CLI `opt` / `cmd` / `env`、以及 SPEC §7 JSON token（键名 / 枚举值）。
- 缺译文自动回退源语言，译文可逐批补；`ImageDiscropperCore/tests/test_i18n_catalog.cpp` 反向解析常量层做对账 ——
  孤儿键 / 层内重复键 / 跨层撞键一律 fail，覆盖率只打印不 fail。
- 生效方式：**GUI 重启生效**（`视图 → 外观/交互…` 的「应用语言」下拉，落 `QSettings ui/language`）；
  **CLI 按调用生效**，优先级 `--lang <code>` > `IDC_LANG` > `LC_ALL` > `LANG` > zh-CN；移动端与桌面共用同一外观/交互对话框的语言下拉（偏好缺省「跟随系统」，改后同样重启生效）。
- **加一门语言 = 新增一份 `lang_xx.cpp` + 在本层 `installCatalogs()` 登记一行 + `core_i18n.h` 补一个语言码**，重编即可，无需构建期工具链。

**一期边界与二期遗留**：一期只做「机制 + 中文源语言 + 英文示范译文 + 重启生效」，以下结构性功课
留待真正扩量英文或加第二门语言时再补，均**不影响当前中文路径**：

- **复数机制**：计量词嵌在整句模板里（`已导出 %1 张…` / `%1 个标注` / `%1 单元` / `%3 层`），
  译成英文需按数量变形（one/few/many）。Qt `tr("%n")` 内建复数规则，自研门面暂无——扩量英文时须先补一层
  按 number 选单的复数表，或把量词改写为不依赖单复数的句式。
- **列宽自适应（已处理，且本就与译文无关）**：曾把 CLI `nums::help::k*OptWidth` 列宽、GUI
  `nums::panels::kCoordSpinWidthPx` / `kLeftPanelMinWidthPx` 列为「译文变长会截断」的遗留，核查后确认它们
  pad 的内容都**不随语言变**：CLI 定宽列对齐的是不可译用法列（`hp::kUse*`＝选项名 + ASCII 占位符），现已改由
  `src/help.cpp` 的 constexpr `optWidth()` 按各命令实际打印的串长编译期取 max（加/减选项自动跟进，
  根除旧手数 off-by-one，无需再手设常量）；GUI 坐标框 `kCoordSpinWidthPx` 是纯数字 `QSpinBox`（无前后缀 / 无
  specialValueText），左栏 `kLeftPanelMinWidthPx` 已取 `std::max(下限, sizeHint)` 运行时自适应——三者均不构成
  i18n 截断风险，故移出待补清单。
- **动态常驻文本需重放**：状态栏提示（`交互模式：%1 | 预处理目标：%2`）、`notify` 弹示等**运行期现取**的文案，
  语言切换（重启生效）后不会自动重写；若二期改热切换，须在语言变更钩子里用**当前状态**再取一遍译文覆盖，否则留旧语言至下次事件。
- **模板化列表行需重建**：预处理队列行（`status::kStepInvertDesc` 等）、多矩形列表、最近打开菜单——每行是「数据 + 模板」产物，
  热切换钩子里须按当前数据重新生成整行，而非只换静态标签。
- **RTL 布局**：若加右起语言（阿拉伯语等），除译文外还须同步切 `LayoutDirection`，镜像固定布局 / 图标方向，非纯文案工作。
- **已打开窗口的临时 UI**：正在显示的对话框 / 菜单在下次构建前不随语言事件刷新（通常可接受，热切换时同理）。

> 句式纪律见 [`ImageDiscropperCore/include/i18n/core_i18n.h`](ImageDiscropperCore/include/i18n/core_i18n.h) 头注：
> 句中嵌运行期值一律写「单条整句 + `%N`」（配 `IDC_KTRF` / `IDC_CTRF`），**不拆 lead/mid/tail 碎片**——拆开后译者无法重组语序。

---

## 3. 与功能规格核心概念的映射

SPEC §1 流水线概念在本工程中的落点（详见 [`ImageDiscropperCore/README.md`](ImageDiscropperCore/README.md)）：

| 规格概念          | 类型 / 接口                                          | 头文件                           |
|---------------|--------------------------------------------------|-------------------------------|
| `Image`       | `idc::core::Image`                               | `core/image.h`                |
| `Region`      | `idc::engine::RectRegion` / `RegionKind`         | `engine/region.h`             |
| `RegionSet`   | `idc::engine::RegionSet` / `Fragment`            | `engine/region_set.h`         |
| 切割线（cut line） | `idc::engine::CutLine` / `CutLineSet`            | `engine/cut_line.h`           |
| `Grid`        | `idc::engine::Grid` / `Cell` / `GridParams`      | `engine/grid.h`               |
| `Selection`   | `idc::engine::Selection` / `Polarity`            | `engine/selection.h`          |
| `Sequence`    | `idc::engine::Sequence` / `SortStrategy`         | `engine/sequence.h`           |
| `Composition` | `idc::engine::Composition` / `MergeLayout`       | `engine/composition.h`        |
| 流水线编排         | `runEngine`（`split → select → compose → export`） | `engine/engine.h`             |
| 图像 I/O        | `readImageFile` / `writeImageFile`               | `engine/image_io.h`           |
| 配置 JSON       | `loadEngineConfig` / `saveEngineConfig`          | `engine/engine_config_json.h` |

CLI 命令与规格功能的对应（详见 [`ImageDiscropperCli/README.md`](ImageDiscropperCli/README.md)）：

| CLI 命令    | 层级      | 极性            | 输出               | 对应规格                    |
|-----------|---------|---------------|------------------|-------------------------|
| `extract` | L1 标准提取 | 恒 keep        | 合并单图（坍缩）         | SPEC §3.2               |
| `erase`   | L2 反向剔除 | 恒 remove      | 分离 / 坍缩 / 重排     | SPEC §3.3（FR-L2.1~L2.6） |
| `grid`    | L3 网格分割 | keep 或 remove | 分离 / 重排合并        | SPEC §3.4（FR-L3.1~L3.8） |
| `config`  | 配置驱动    | 由 JSON 决定     | 由 JSON 的 emit 决定 | SPEC §7 / FR-L3.8       |

---

## 4. 构建与运行

工程使用 CMake（>= 3.28）+ vcpkg（`stb` / `libwebp` / `nlohmann-json` 由 Core PRIVATE 接入；`qtbase` 供 GUI 使用）。
依赖由 `vcpkg.json` 清单自动安装（设置环境变量 `VCPKG_ROOT` 指向 vcpkg 根目录）。
各平台经 overlay triplet（[`triplets/`](triplets/README.md)）锁定单一构建配置，避免 dbg+rel 双配置重复构建。
**以本目录为 CMake 源目录**加载工程，推荐用 Presets：

```bash
cmake --preset windows-debug   # Windows：Ninja + MSVC；Linux/macOS 用 linux-debug / macos-debug
cmake --build build/windows-debug    # 产出 bin/idc(.exe)、idc_gui(.exe) 与 demo(.exe)
ctest --test-dir build/windows-debug --output-on-failure   # 同时运行 Core unit_tests 与 CLI cli_tests（集成用例 IT-1~IT-53，覆盖矩阵见 ImageDiscropperCli/tests/README.md）
```

首次配置自动安装清单依赖（qtbase 较慢）。手动方式（清单模式自动生效）：
`cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE=$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake`。
每次提交由 GitHub Actions 自动验证：桌面三平台 Release 构建（含 GUI，与发布产物同构）+ 全套测试 + 移动双端（Android / iOS）构建打包；其余验证手动运行（`.github/workflows/cpp-verify.yml`）。

完整用法与示例见 [`ImageDiscropperCli/README.md`](ImageDiscropperCli/README.md)「用法速览」；行为契约（参数规则 / 退出码）见 [`SPEC.md`](../SPEC.md) §8.1。

退出码与 CLI 行为契约见 [`SPEC.md`](../SPEC.md) §8.1。

---

## 5. 文档索引

| 文档                                                               | 说明                                                 |
|------------------------------------------------------------------|----------------------------------------------------|
| [`../README.md`](../README.md)                                   | 仓库首页（项目定位、快速开始、文档导航；含英文版 `README.en.md`）           |
| [`../CONTRIBUTING.md`](../CONTRIBUTING.md)                       | 贡献指南（构建/测试、代码与文档规范、提交与 PR 约定）                      |
| [`SPEC.md`](../SPEC.md)                                          | 功能规格：模式 / 导出 / 边界（E-1~E-8）/ NFR / 配置 schema，唯一行为基线 |
| [`ImageDiscropperCore/README.md`](ImageDiscropperCore/README.md) | Core 层：模块划分、概念映射、构建                                |
| [`ImageDiscropperCli/README.md`](ImageDiscropperCli/README.md)   | CLI 层：命令、选项、退出码、Core API 清单                        |
| [`ImageDiscropperGui/README.md`](ImageDiscropperGui/README.md)   | GUI 层：Qt6 桌面前端——用户操作速览、画布 / 面板 / 文档模型与交互、关键设计决策、构建 |

每个源码子目录下均有独立的 `README.md` 说明其职责边界与分块依据（全仓库目录说明约定）。

---

## 6. 实现状态

| 阶段      | 范围                                           | 状态                   |
|---------|----------------------------------------------|----------------------|
| **MVP** | 统一引擎 + L2 反向剔除（单矩形/横线/竖线）+ L1 标准提取 + 分离与坍缩导出 | ✅ 已落地                |
| **v2**  | L3 网格分割（排序 + 重排合并）+ 配置预设、多矩形并集剔除             | ✅ 已落地                |
| **v3**  | 基础图像处理（FR-1）、标注图层                            | ✅ 已落地（GUI/CLI 全链路接入） |

> 本地处理、无损优先（NFR-1 / NFR-2）：图像不上传服务器；切割为纯像素搬运，仅 JPEG 输出有损。

---

## 7. 许可

本目录随仓库整体采用 **GPL-3.0**（见根 [`../LICENSE`](../LICENSE)），Core / CLI / GUI 及任何语言实现的 Core 均在统一许可下。
