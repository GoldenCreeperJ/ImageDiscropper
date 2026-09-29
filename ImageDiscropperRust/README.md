# ImageDiscropperRust

> **状态：规划中，尚未开始实现。**

计划中的 **Rust 实现**，与 [`ImageDiscropperCpp/`](../ImageDiscropperCpp) 共享同一份功能规格
（[`SPEC.md`](../SPEC.md)）。

## 规划要点

- 施工方案与 Rust 编码规范见 [`GuideLine.md`](GuideLine.md)（以 `SPEC.md` 为基线、C++ 实现为 golden reference）。
- 采用 workspace 结构：`image-discropper-core`（纯逻辑 crate）/ `image-discropper-cli`（clap 命令行，二进制名 `idc`）/
  `image-discropper-tauri`（Tauri 桌面 + 移动端）。
- 引擎语义与 C++ 版一致：Grid-Selection-Emit 统一流水线，L1/L2/L3 为同一引擎的参数预设（NFR-0）；
  预处理为声明式队列，随 SPEC §7 配置存取。
- 桌面/移动 GUI 已定选 **Tauri v2 + Svelte + Konva.js**（见 GuideLine §8），交互与视觉以 C++ Qt GUI 为行为蓝本。

## 许可

本目录随仓库整体采用 [GPL-3.0](../LICENSE)，未来的 Rust Core 同样适用。
