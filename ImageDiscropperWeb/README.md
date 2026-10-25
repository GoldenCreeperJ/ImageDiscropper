# ImageDiscropperWeb

> **状态：规划中，目录暂无代码。**

**Web 技术前端实现的统一 monorepo**——Tauri / Electron / Wails 三类壳共用**同一份 Web 前端**，
与 [`ImageDiscropperCpp/`](../ImageDiscropperCpp)（引擎 + Qt 参考实现）、
[`ImageDiscropperRust/`](../ImageDiscropperRust)（Rust 引擎 crate）共享同一份功能规格
（[`SPEC.md`](../SPEC.md)）。

## 为什么单开一个目录

Tauri / Electron / Wails 的差异只在「壳 + 后端语言 + IPC」，前端是同一份 DOM Web 应用——
UI 代码应当只有一份；按框架平铺目录会让三份前端各自漂移。故本目录按**层**组织，框架只分「壳」。

## 规划结构

```text
ImageDiscropperWeb/
├── web/                        # 多种前端框架(Vue,React,Angular,Svelte...)的实现，保证功能完全一致
├── packages/
│   ├── engine-client/          # 引擎访问抽象（命令式接口；壳各自实现 IPC 桥 / WASM 直连）
│   ├── core-logic/             # 与 UI 无关的状态机 / 选区 / 排序 / 会话逻辑（纯 TS，可单测）
│   └── l10n/                   # 译文源（与 C++ 三层 texts 常量层对账，单一真相）
├── apps/
│   ├── tauri/                  # 壳：Rust 后端（#[tauri::command] → image-discropper-core crate）
│   ├── electron/               # 壳：Node 后端（IPC → WASM 引擎或 CLI 子进程）
│   └── wails/                  # 壳：Go 后端（IPC → WASM 引擎）
├── golden/                     # 跨实现黄金测试：同输入 → 逐字节同输出（与 C++ / Rust 对拍）
└── assets/                     # Web 侧资产（图标等与根 assets/ 同步）
```

## 设计公理（不可违背）

1. **引擎单一真相**：C++ Core 是 golden reference。
2. **前端不自算几何**：遮罩 / 网格 / 坍缩可行性全部来自后端命令，前端只渲染（对齐 Rust GuideLine §8）。
3. **i18n 单一真相**：`packages/l10n` 与 C++ `gui_texts` / `cli_texts` / `core_texts` 三层对账
   （键名同构、目录对账测试同思路），禁止前端另起一份译文。
4. **SPEC 是行为基线**：凡分歧以 [`SPEC.md`](../SPEC.md) 为准；golden 对拍是双实现共存的唯一护栏。
5. **加新框架 = 加一个 apps/ 目录**：前端与逻辑零复制，只写 IPC 桥 + 打包。
6. **框架原生形态，不照抄 Qt 视觉**：各实现的界面与交互形态**优先采用框架与平台自身的原生组件、手势与惯例**，
   不一比一复刻 Qt 参考实现的布局与视觉；承诺的是**功能操作一比一**（SPEC §8 的能力与结果一致），视觉与交互细节由各实现按框架惯例自主设计。

## 与相邻目录的关系

| 目录                     | 角色                                                                          |
|------------------------|-----------------------------------------------------------------------------|
| `ImageDiscropperCpp/`  | 引擎 + CLI + Qt GUI（参考实现、唯一行为真相）                                              |
| `ImageDiscropperRust/` | Rust 引擎 crate（`image-discropper-core` / `-cli`）；`apps/tauri` 后端经 path 依赖消费它 |
| `ImageDiscropperWeb/`  | 本目录：共享前端 + 各壳 + 桥 + 黄金测试                                                    |

## 许可

本目录随仓库整体采用 [GPL-3.0](../LICENSE)；前端第三方库许可审查随各壳依赖清单进行
（对齐 Rust 侧 cargo-deny 思路）。
