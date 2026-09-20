# ImageDiscropperRust

> **状态：规划中，尚未开始实现。**

计划中的 **Rust 实现**，与 [`ImageDiscropperCpp/`](../ImageDiscropperCpp) 共享同一份功能规格
（[`SPEC.md`](../SPEC.md)）。

## 规划要点

- 采用 workspace 结构：`image-tool-core`（纯逻辑 crate）/ `image-tool-cli`（clap 命令行）/
  `image-tool-ffi`（C ABI，稳定后再加）。
- 引擎语义与 C++ 版一致：Grid-Selection-Emit 统一流水线，L1/L2/L3 为同一引擎的参数预设（NFR-0）。
- GUI 方案待定（Tauri / egui 等），尚未立项。

## 许可

本目录随仓库整体采用 [GPL-3.0](../LICENSE)，未来的 Rust Core 同样适用。
