# src/preprocess

**目录作用**：`include/preprocess` 下头文件的实现代码，即「预处理层」——在切割 / 剔除之前，把一组
**声明式有序步骤**（`PreprocessConfig`）按序作用于原图，产出切割引擎的真正输入。对应规格 FR-1
「基础图像处理前置层」与 SPEC §7 配置中的 `preprocess` 段。

**分块依据**：
- `preprocess_config.cpp` 只负责数据模型的辅助函数（枚举 ↔ 字符串、默认配置构造、类型标签提取）。
  `PreprocessConfig` 是 `GrayOp / SplitOp / InvertOp / RotateOp / FlipOp / ResizeOp` 的 variant。
- `preprocess_pipeline.cpp` 只负责按顺序调度各像素核，是「编排层」，
  不包含任何具体像素处理代码（具体像素核在本目录私有 `pixel_kernels`）。

**能力要点**：
- **队列增删移改（公开继承泛型容器）**：`add / insert / remove / replaceAt / moveUp / moveDown / clear`——
  存储与「按序增删移改 + 越界钳制/忽略」的边界策略由**公开基类 `core::Pipeline<PreprocessConfig>`** 直接提供（与元素类型无关的骨架，见 [core](../core/README.md)），
  本类不手写任何转发；供 GUI 预处理页做「添加 / 就地改参 / 删除 / 上下移 / 重置」的声明式编辑
  （每次编辑由上层从原图重放整条队列物化工作图）。
- **按序执行** `apply(src)`：逐步调 pixel_kernels；`resize` 设绝对目标尺寸、`rotate 90/270` 交换宽高，
  `gray / invert / split / flip` 保持尺寸。充当执行层融合的逐像素等价性参照 oracle。
- **执行层融合入口** `applyExec(src)`：与 `apply` **逐像素完全等价**（含尺寸 / 格式），但先把整条队列压成内部
  `ExecStep` 计划（绝不落回词表步）再单趟执行——① 连续几何步折为**单次 D4 重排** `remapD4`（闭式逆采样，
  几何为「灰度亦无损的纯位置置换」）；② 连续色彩步（gray / invert / split）折为**单次位映射** `channelMap`：
  pre-gray 段任意 Split/Invert 序列合为 `f(v)=(v&K)^J` 一趟完成、`gray` 幂等去重、`gray` 之后 `split` 为死步丢弃、
  `invert` 按奇偶累积。`ResizeOp` 是**唯一块界**（双线性取整与色彩 / 几何均不可交换），块内步数恒 ≤4，
  故**像素趟数与队列长度解耦**；灰度之后的死步在计划构建时自动吞除，无需录入拦截。以「载入强解 RGBA、进队为多通道」为前提。
- **纯元数据几何推演** `geometryOutput(w, h)`：不碰像素，仅按各步语义推演出 `{输出宽高, 净旋转象限}`，
  供 GUI 计算「几何签名」（geomSig）判定编辑前后是否发生几何失效（尺寸或净旋转变化才清选区 / 标注）。

| 文件                        | 职责                                                                                                                |
|---------------------------|-------------------------------------------------------------------------------------------------------------------|
| `preprocess_config.cpp`   | 枚举转字符串、默认参数构造、类型标签提取                                                                                              |
| `preprocess_pipeline.cpp` | 执行与推演：按序执行 `apply`、执行层融合 `applyExec` + `geometryOutput` 几何推演（队列 CRUD 由公开基类 `core::Pipeline<PreprocessConfig>` 提供） |
