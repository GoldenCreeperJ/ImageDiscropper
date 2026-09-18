# include/preprocess

**目录作用**：**切割前的基础图像处理流水线**。把 `pixel_ops` 提供的无状态算法组织成
可增删、可排序的步骤序列，作用于原图后产出「切割引擎的输入图像」。
对应 SPEC §3.1 与 §7 配置 JSON 的 `preprocess` 段。

> 本目录由早期的 `operation`（操作）模块重命名而来，以对齐规格术语。

**分块依据**：把「配置数据」与「执行逻辑」分离——
1. `preprocess_config.h` —— 各预处理步骤的参数结构（`GrayOp` / `SplitOp` / `InvertOp` /
   `RotateOp` / `FlipOp`），用 `std::variant` 组合为 `PreprocessConfig`。
2. `preprocess_pipeline.h` —— `PreprocessPipeline` 负责步骤的增删改序与按序执行（`apply`）。

| 文件                      | 职责                                                    |
|-------------------------|-------------------------------------------------------|
| `preprocess_config.h`   | 预处理类型枚举 `PreprocessType`、各步骤参数与 `PreprocessConfig` 变体 |
| `preprocess_pipeline.h` | 预处理流水线 `PreprocessPipeline` 与单步执行 `applyOne`          |
