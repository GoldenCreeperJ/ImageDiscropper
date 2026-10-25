# ImageDiscropperRust

> **状态：规划中，尚未开始实现。**

计划中的 **Rust 实现**，与 [`ImageDiscropperCpp/`](../ImageDiscropperCpp) 共享同一份功能规格
（[`SPEC.md`](../SPEC.md)）。

## 规划要点

- 施工方案与 Rust 编码规范见 [`GuideLine.md`](GuideLine.md)（以 `SPEC.md` 为基线、C++ 实现为 golden reference）。
- 采用 workspace 结构：`image-discropper-core`（纯逻辑 crate）/ `image-discropper-cli`（clap 命令行，二进制名 `idc`）——
  **本目录只交付引擎与命令行**；GUI 壳（Tauri 后端 + Web 前端）统一住 [`ImageDiscropperWeb/`](../ImageDiscropperWeb)
  （一份前端多壳共用，结构约定与分工见其 README；后端命令面契约见 GuideLine §8.1）。
- 引擎语义与 C++ 版一致：Grid-Selection-Export 统一流水线，L1/L2/L3 为同一引擎的参数预设（NFR-0）；
  预处理为声明式队列，随 SPEC §7 配置存取。
- 桌面/移动 GUI 已定选 **Tauri v2 + Svelte + Konva.js**（见 GuideLine §8），**行为**（功能操作与结果）以 C++ Qt GUI 为蓝本、一比一；**界面形态不照抄 Qt**，优先框架与平台原生组件、手势与惯例（见 SPEC §8 与 GuideLine §8）。

## 许可

本目录随仓库整体采用 [GPL-3.0](../LICENSE)，未来的 Rust Core 同样适用。
