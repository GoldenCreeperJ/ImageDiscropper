# include/preprocess

**目录作用**：**切割前的基础图像处理流水线**。把 `pixel_ops` 提供的无状态算法组织成
可增删、可排序的步骤序列，作用于原图后产出「切割引擎的输入图像」。
对应 SPEC §3.1 与 §7 配置 JSON 的 `preprocess` 段。

> 本目录由早期的 `operation`（操作）模块重命名而来，以对齐规格术语。

**分块依据**：把「配置数据」与「执行逻辑」分离——
1. `preprocess_config.h` —— 各预处理步骤的参数结构（`GrayOp` / `SplitOp` / `InvertOp` /
   `RotateOp` / `FlipOp` / `ResizeOp`），用 `std::variant` 组合为 `PreprocessConfig`；
   各结构体自带 `operator==`（供流水线逐步相等比较）。
   > 不透明度**不再是像素预处理步**（旧 `OpacityOp` 已彻底移除）：它上移为图层合成期参数
   > `canvas::ImageTransform.opacity`（逐像素 alpha 乘子，属 `canvas` 模块），语义上透明度是合成概念而非像素变换，
   > 且使灰度图层的不透明度不再因 GRAY 无 alpha 而静默失效。
2. `preprocess_pipeline.h` —— `PreprocessPipeline` **公开继承**泛型有序队列 `core::Pipeline<PreprocessConfig>`
   承载「存储 + 增删移改」（`add` / `insert` / `remove` / `setStep` / `moveUp` / `moveDown` / `clear`，
   插入越界钳制为追加、增删改越界忽略等边界策略由容器代劳），自身只保留与像素/几何语义绑定的方法：
   按序执行 `apply`、执行层融合入口 `applyExec`（与 `apply` 逐像素等价；色彩步折单次位映射 `(v&K)^J`、几何步折单次 D4 重排，
   以 `resize` 为块界），纯元数据 `geometryOutput`（输出尺寸 +
   净旋转象限推演，供 GUI 几何签名使用）。

| 文件                      | 职责                                                                                                                                     |
|-------------------------|----------------------------------------------------------------------------------------------------------------------------------------|
| `preprocess_config.h`   | 预处理类型枚举 `PreprocessType`、各步骤参数（含 `ResizeOp`）与 `PreprocessConfig` 变体、`makeDefaultConfig` / `configType` / `isColorStep`                 |
| `preprocess_pipeline.h` | 预处理流水线 `PreprocessPipeline`：**公开继承** `core::Pipeline<PreprocessConfig>` 承载存储 + CRUD；`apply` 执行、`applyExec` 执行层融合、`geometryOutput` 几何推演 |
