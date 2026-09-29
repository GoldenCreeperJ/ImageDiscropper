# include/preprocess

**目录作用**：**切割前的基础图像处理流水线**。把 `pixel_ops` 提供的无状态算法组织成
可增删、可排序的步骤序列，作用于原图后产出「切割引擎的输入图像」。
对应 SPEC §3.1 与 §7 配置 JSON 的 `preprocess` 段。

> 本目录由早期的 `operation`（操作）模块重命名而来，以对齐规格术语。

**分块依据**：把「配置数据」与「执行逻辑」分离——
1. `preprocess_config.h` —— 各预处理步骤的参数结构（`GrayOp` / `SplitOp` / `InvertOp` /
   `RotateOp` / `FlipOp` / `ResizeOp`），用 `std::variant` 组合为 `PreprocessConfig`；
   各结构体自带 `operator==`（供流水线逐步相等比较）。
2. `preprocess_pipeline.h` —— `PreprocessPipeline` 负责步骤的增删改序（`insert` / `remove` /
   `setStep` / `moveUp` / `moveDown` / `clear`）、按序执行（`apply`），以及两个纯元数据/等价变换能力：
   `folded()`（连续同类段就地规整、逐像素恒等、趟数更少的折叠流水线）与 `geometryOutput()`（输出尺寸 +
   净旋转象限推演，供 GUI 几何签名使用）。

| 文件                      | 职责                                                              |
|-------------------------|-----------------------------------------------------------------|
| `preprocess_config.h`   | 预处理类型枚举 `PreprocessType`、各步骤参数（含 `ResizeOp`）与 `PreprocessConfig` 变体、`makeDefaultConfig` / `configType` |
| `preprocess_pipeline.h` | 预处理流水线 `PreprocessPipeline`：步骤增删改序、`apply` 执行、`folded` 折叠、`geometryOutput` 几何推演 |
