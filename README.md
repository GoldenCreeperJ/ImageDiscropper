# ImageDiscropper

**图像区域提取、反向剔除与网格分割工具** | [English](README.en.md)

> 一句话定位：常规裁剪只回答「保留哪一块」；本工具回答「**沿哪些线切开、留下哪些块、怎么重新拼起来**」。

ImageDiscropper 是一款**本地运行**的图像切割工具。所有功能都归约为同一条 **Grid-Selection-Emit** 流水线，
通过贯穿全图的切割线把图像诱导为规则网格，再按「选择集 + 极性」决定保留/剔除，最后以分离、坍缩或重排的方式导出。

---
## 🤖 AI 主导开发声明

本项目主要由 AI 主导开发（约 99%），文档亦为 AI 生成。功能规格见
[`SPEC.md`](ImageDiscropperCpp/SPEC.md)，贡献前请阅读 [`CONTRIBUTING.md`](CONTRIBUTING.md)。

## ✨ 特性

- **三层模式，一个引擎**（NFR-0：模式仅为参数预设，L1 ⊂ L2 ⊂ L3）：

  | 层级     | 模式     | 定位    | 说明                                               |
  |--------|--------|-------|--------------------------------------------------|
  | **L1** | 标准提取   | 兼容基线  | 常规矩形 / 横带 / 竖带提取，极性恒 `keep`                      |
  | **L2** | ⭐ 反向剔除 | 差异化内核 | **按线删除**：抽掉中缝、两侧对接，零伪造、无损；支持十字 / 横线 / 竖线 / 多矩形并集 |
  | **L3** | ⭐ 网格分割 | 完备表达层 | 参数化网格 + 自由点选 + 显式排序 + 重排合并，可做拼图打乱、图集切分           |

- **公理**：切割线是**贯穿全图的直线**，不是线段——这是本工具区别于常规裁剪的根本前提。
- **导出三式**：分离导出（每块一张图）/ 合并坍缩（删除整行整列后紧贴拼接）/ 合并重排（按显式序列填入新画布）。
- **基础图像处理前置层**：旋转、翻转、缩放、黑白、反色、色道分离；独立矢量**标注图层**（导出时可选择烧录）。
- **本地处理、无损优先**（NFR-1/2）：图像不上传服务器；切割为纯像素搬运，仅 JPEG 输出有损。
- **GUI + CLI 双前端**：Qt 6 桌面端（实时预览、拖拽切割线、撤销重做）+ 命令行 `idc`（可脚本化、配置驱动）。

```text
原图 → ① 切割线集合 → ② 诱导网格 → ③ 选择集 → ④ 极性(Keep/Remove) → ⑤ 排布导出(Collapse/Rearrange)
```

## 📁 仓库结构

```text
ImageDiscropper/
├── ImageDiscropperCpp/          # C++ 实现（当前主体，v1.0.0）
│   ├── ImageDiscropperCore/     #   Core：Grid-Selection-Emit 统一引擎 + 基础图像处理（静态库）
│   ├── ImageDiscropperCli/      #   CLI：命令行薄壳（可执行 idc）
│   └── ImageDiscropperGui/      #   GUI：Qt 6 桌面前端（可执行 idc_gui）
└── ImageDiscropperRust/         # Rust 实现（规划中，尚未开始）
```

```text
GUI ──────┐
          ├── Core
CLI ──────┘
```

## 🚀 快速开始

### 前置依赖

- CMake ≥ 3.28 + 支持 C++17 的编译器（MSVC / MinGW / Clang）
- [vcpkg](https://vcpkg.io)（清单模式自动安装，无需手动 `vcpkg install`）：把环境变量 `VCPKG_ROOT` 指向 vcpkg 根目录即可
- 依赖清单见 [`ImageDiscropperCpp/vcpkg.json`](ImageDiscropperCpp/vcpkg.json)（`stb` / `libwebp` / `nlohmann-json`，GUI 另需 `qtbase`）

### 构建（Windows / Linux / macOS）

推荐用 CMake Presets（首次配置会按清单自动安装依赖，其中 `qtbase` 较慢）：

```bash
cd ImageDiscropperCpp
cmake --preset windows         # Windows：VS 2022 多配置
cmake --build --preset debug
ctest --preset test-debug      # Core unit_tests + CLI cli_tests

# Linux / macOS：Ninja 单配置预设（linux-debug / linux-release / macos-debug / macos-release）
cmake --preset linux-debug
cmake --build --preset linux-debug
ctest --preset test-linux-debug
```

也可手动指定工具链（清单模式自动生效，无需预装包）：

```bash
cmake -S ImageDiscropperCpp -B build -DCMAKE_TOOLCHAIN_FILE=$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake
cmake --build build
ctest --test-dir build
```

产物位于构建目录 `bin/`：`idc(.exe)`、`idc_gui(.exe)`、`demo(.exe)`。
（CLion 用户可直接以 `ImageDiscropperCpp/` 为 CMake 源目录打开工程，CLion 会识别 Presets。）

每次提交由 GitHub Actions（[`.github/workflows/ci.yml`](.github/workflows/ci.yml)）在 **Windows / Linux / macOS 三平台**自动构建（含 GUI）并跑 Core/CLI 测试。

### 命令行速览

```bash
idc extract --input photo.jpg --rect 100,100,300,250 --output out.png
idc erase   --input photo.jpg --rect 100,100,300,250 --merge collapse --output out.png
idc erase   --input photo.jpg --rect 100,100,300,250 --output-dir ./out/ --format png
idc grid    --input photo.jpg --grid 100,100,200,150 --keep 0,0 --keep 0,2 --keep 2,0 --keep 2,2 \
            --compose --canvas 2x2 --output result.png
idc config  --load my-config.json --input photo.jpg --output result.png
```

完整选项见 `idc <command> --help`；GUI 操作速览见 [`ImageDiscropperGui/README.md`](ImageDiscropperCpp/ImageDiscropperGui/README.md)。

## 📚 文档索引

文档按层级组织，**每层有明确的内容范围，互相不重复**：

| 层级         | 文档                                                               | 内容范围                                                                      |
|------------|------------------------------------------------------------------|---------------------------------------------------------------------------|
| ① 仓库首页     | 本文件 / [`README.en.md`](README.en.md)                             | 项目定位、特性、快速开始、文档导航、许可——**只做导读**，不展开技术细节                                    |
| ② 贡献约定     | [`CONTRIBUTING.md`](CONTRIBUTING.md)                             | 构建与测试方式、代码/文档规范、提交与 PR 约定                                                 |
| ③ C++ 实现总览 | [`ImageDiscropperCpp/README.md`](ImageDiscropperCpp/README.md)   | C++ 工程的组成、与需求概念的映射、构建运行、实现状态                                              |
| ④ 功能规格     | [`ImageDiscropperCpp/SPEC.md`](ImageDiscropperCpp/SPEC.md)       | **唯一行为基线**：模式 / 导出 / 边界（E-1~E-8）/ NFR / 配置 schema；贡献规范见 `CONTRIBUTING.md` |
| ⑤ 模块说明     | `ImageDiscropper{Core,Cli,Gui}/README.md`                        | 各模块职责、与规格的 API 映射、构建方式；Gui README 另含用户操作速览与设计说明                           |
| ⑥ 目录说明     | 各 `include/`、`src/` 等子目录内的 `README.md`                           | 该目录的职责边界、分块依据、文件清单（全仓库目录说明约定）                                             |
| ⑦ 占位实现     | [`ImageDiscropperRust/README.md`](ImageDiscropperRust/README.md) | Rust 实现的规划状态说明                                                            |

## 📊 项目状态

| 阶段      | 范围                                 | 状态                                           |
|---------|------------------------------------|----------------------------------------------|
| **MVP** | 统一引擎 + L2 反向剔除 + L1 标准提取 + 分离/坍缩导出 | ✅ 已落地                                        |
| **v2**  | L3 网格分割（排序 + 重排合并）+ 配置预设、多矩形并集剔除   | ✅ 已落地                                        |
| **v3**  | 基础图像处理（FR-1）、标注图层                  | ⏳ Core 支撑层已就绪；GUI 前端已落地，CLI/GUI 与引擎的完整衔接尚在推进 |

## 📄 许可

本项目整体采用 [GPL-3.0](LICENSE)，覆盖所有语言实现的 Core（C++ / 未来的 Rust 等）以及 CLI 与 GUI。
