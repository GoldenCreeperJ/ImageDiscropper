# ImageDiscropper

[![CI](https://github.com/GoldenCreeperJ/ImageDiscropper/actions/workflows/ci.yml/badge.svg)](https://github.com/GoldenCreeperJ/ImageDiscropper/actions/workflows/ci.yml)
[![License: GPL v3](https://img.shields.io/badge/License-GPLv3-blue.svg)](LICENSE)
[![Platform](https://img.shields.io/badge/platform-Windows%20%7C%20Linux%20%7C%20macOS-lightgrey)](.github/workflows/ci.yml)

**图像区域提取、反向剔除与网格分割工具** | [English](README.en.md)

> 一句话定位：常规裁剪只回答「保留哪一块」；本工具回答「**沿哪些线切开、留下哪些块、怎么重新拼起来**」。

ImageDiscropper 是一款**本地运行**的图像切割工具。所有功能都归约为同一条 **Grid-Selection-Emit** 流水线，
通过贯穿全图的切割线把图像诱导为规则网格，再按「选择集 + 极性」决定保留/剔除，最后以分离、坍缩或重排的方式导出。

---
## 🤖 AI 主导开发声明

本项目主要由 AI 主导开发（约 99%），文档亦为 AI 生成。功能规格见
[`SPEC.md`](SPEC.md)，贡献前请阅读 [`CONTRIBUTING.md`](CONTRIBUTING.md)。

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
- 依赖清单见 [`ImageDiscropperCpp/vcpkg.json`](ImageDiscropperCpp/vcpkg.json)（`stb` / `libwebp` / `nlohmann-json` / `qtbase`）

### 构建（Windows / Linux / macOS）

推荐用 CMake Presets（首次配置会按清单自动安装依赖，其中 `qtbase` 较慢）：

```bash
cd ImageDiscropperCpp
cmake --preset windows-debug   # Windows：Ninja + MSVC（Debug；Release 用 windows-release，静态 Qt 单文件）
cmake --build build/windows-debug
ctest --test-dir build/windows-debug --output-on-failure   # Core unit_tests + CLI cli_tests

# Linux / macOS：Ninja 单配置预设（linux-debug / linux-release / macos-debug / macos-release）
cmake --preset linux-debug
cmake --build build/linux-debug
ctest --test-dir build/linux-debug --output-on-failure
```

也可手动指定工具链（清单模式自动生效，无需预装包）：

```bash
cmake -S ImageDiscropperCpp -B build -DCMAKE_TOOLCHAIN_FILE=$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake
cmake --build build
ctest --test-dir build
```

产物位于构建目录 `bin/`：`idc(.exe)`、`idc_gui(.exe)`、`demo(.exe)`。
（CLion 用户可直接以 `ImageDiscropperCpp/` 为 CMake 源目录打开工程，CLion 会识别 Presets。）

每次提交由 GitHub Actions（[`.github/workflows/ci.yml`](.github/workflows/ci.yml)）自动验证：**三平台 Release 构建（含 GUI，与发布产物同构）+ 全套测试**（依赖缓存命中后分钟级）；Debug 配置的全量验证手动运行（[`.github/workflows/cpp/dbg-verify.yml`](.github/workflows/cpp/dbg-verify.yml)：桌面三平台 + 移动双端）。

无需构建 GUI 应用时可跳过：`cmake --preset windows-debug -DIDC_BUILD_GUI=OFF`（qtbase 依赖仍会安装）。

### 发布（Release）

发布走**统一入口** [`.github/workflows/cd.yml`](.github/workflows/cd.yml)，按标签名路由：

| 标签                                           | 产物                                                                        | Release 类型  |
|----------------------------------------------|---------------------------------------------------------------------------|-------------|
| `v*`（如 `v1.0.0`）                             | 三平台桌面：6 个可执行文件 + Windows 安装包（Inno Setup）+ macOS `.app` 压缩包 + `SHA256SUMS` | 正式 Release  |
| `v*-alpha*` / `v*-beta*`（如 `v1.0.1-alpha.1`） | 移动端：Android APK + iOS `.app` 压缩包                                          | pre-release |

```bash
git tag v1.0.0 && git push origin v1.0.0                       # 正式发布
git tag v1.0.1-alpha.1 && git push origin v1.0.1-alpha.1       # 移动端预发布
```

也可在 Actions 页面手动触发（填写标签名即可——不存在时会自动在当前 HEAD 创建，无需 push）。

> 产物未做代码签名（Windows SmartScreen 会提示、macOS 首次打开需右键→打开）；Linux 版依赖系统 X11 库（正常桌面环境均有）。

### 命令行速览

```bash
idc extract --input photo.jpg --rect 100,100,300,250 --output out.png   # L1：保留中心矩形
```

完整示例与全部命令见 [`ImageDiscropperCli/README.md`](ImageDiscropperCpp/ImageDiscropperCli/README.md)「用法速览」；
行为契约（参数规则 / 退出码）见 [`SPEC.md`](SPEC.md) §8.1；GUI 操作速览见 [`ImageDiscropperGui/README.md`](ImageDiscropperCpp/ImageDiscropperGui/README.md)。

## 📚 文档索引

文档按层级组织，**每层有明确的内容范围，互相不重复**：

| 层级         | 文档                                                               | 内容范围                                                                      |
|------------|------------------------------------------------------------------|---------------------------------------------------------------------------|
| ① 仓库首页     | 本文件 / [`README.en.md`](README.en.md)                             | 项目定位、特性、快速开始、文档导航、许可——**只做导读**，不展开技术细节                                    |
| ② 贡献约定     | [`CONTRIBUTING.md`](CONTRIBUTING.md)                             | 构建与测试方式、代码/文档规范、提交与 PR 约定                                                 |
| ③ C++ 实现总览 | [`ImageDiscropperCpp/README.md`](ImageDiscropperCpp/README.md)   | C++ 工程的组成、与需求概念的映射、构建运行、实现状态                                              |
| ④ 功能规格     | [`SPEC.md`](SPEC.md)                                             | **唯一行为基线**：模式 / 导出 / 边界（E-1~E-8）/ NFR / 配置 schema；贡献规范见 `CONTRIBUTING.md` |
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
社区规范见 [行为准则](CODE_OF_CONDUCT.md)，漏洞报告方式见 [安全策略](SECURITY.md)。
