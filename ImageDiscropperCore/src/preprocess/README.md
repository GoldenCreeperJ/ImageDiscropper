# src/preprocess

**目录作用**：`include/preprocess` 下头文件的实现代码，即「预处理层」——在切割 / 剔除之前对原图做
灰度、反色、旋转、缩放等预处理。对应终稿 FR-1「基础图像处理前置层」与 §9 配置中的 `preprocess` 段。

**分块依据**：
- `preprocess_config.cpp` 只负责数据模型的辅助函数（枚举 ↔ 字符串、默认配置构造）。
- `preprocess_pipeline.cpp` 只负责按顺序调度各 processing 算法，是「编排层」，
  不包含任何具体像素处理代码。

| 文件                        | 职责                                  |
|---------------------------|-------------------------------------|
| `preprocess_config.cpp`   | 枚举转字符串、默认参数构造、类型标签提取                |
| `preprocess_pipeline.cpp` | 流水线增删改查与按序执行；调用 processing 模块完成实际计算 |
