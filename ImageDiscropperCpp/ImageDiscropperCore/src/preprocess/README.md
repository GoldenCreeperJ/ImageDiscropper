# src/preprocess

**目录作用**：`include/preprocess` 下头文件的实现代码，即「预处理层」——在切割 / 剔除之前，把一组
**声明式有序步骤**（`PreprocessConfig`）按序作用于原图，产出切割引擎的真正输入。对应规格 FR-1
「基础图像处理前置层」与 SPEC §7 配置中的 `preprocess` 段。

**分块依据**：
- `preprocess_config.cpp` 只负责数据模型的辅助函数（枚举 ↔ 字符串、默认配置构造、类型标签提取）。
  `PreprocessConfig` 是 `GrayOp / SplitOp / InvertOp / RotateOp / FlipOp / ResizeOp` 的 variant。
- `preprocess_pipeline.cpp` 只负责按顺序调度各 pixel_ops 算法，是「编排层」，
  不包含任何具体像素处理代码。

**能力要点**：
- **队列增删移改**：`add / insert / remove / setStep / moveUp / moveDown / clear`——供 GUI 图像页
  做「添加 / 就地改参 / 删除 / 上下移 / 重置」的声明式编辑（每次编辑由上层从原图重放整条队列物化工作图）。
- **按序执行** `apply(src)`：逐步调 pixel_ops；`resize` 设绝对目标尺寸、`rotate 90/270` 交换宽高，
  `gray / invert / split / flip` 保持尺寸。
- **纯元数据几何推演** `geometryOutput(w, h)`：不碰像素，仅按各步语义推演出 `{输出宽高, 净旋转象限}`，
  供 GUI 计算「几何签名」（geomSig）判定编辑前后是否发生几何失效（尺寸或净旋转变化才清选区 / 标注）。
- **逐像素等价的同类段规整** `folded()`：返回一个与原队列**逐像素完全一致但趟数更少**的新队列（不改本对象）——
  对**连续同类段**各自就地规整，段间（含 `ResizeOp`）绝不跨越重排：
  - 连续几何步折叠为规范的 ≤2 个 D4 步（1 RotateOp + 至多 1 FlipOp，flip 可水平或垂直）——几何折叠逐像素恒等，
    是因为 pixel_ops 旋转 / 翻转已修为“灰度亦无损的纯位置置换”；
  - 连续色彩步（gray / invert / split）折叠为规范形：首个 `gray` 前的 `split / invert` 按逐通道位映射 `f(v)=(v&K)^J`
    合并为至多 `SplitOp{K} + InvertOp{J}`（恒等分量省略）；`gray` 幂等（重复丢弃）；`gray` 之后 `split` 为死步丢弃、
    `invert` 按奇偶折叠（两次抵消、末尾至多补 1 步）。灰度态在整个队列内单调不回退（跨几何 / resize 段传播），且色彩
    折叠以“进入队列为多通道（载入器强解 RGBA）”为前提，故 pre-gray 位映射合并逐像素恒等；
  - `ResizeOp` 及各段之间**原地保留、绝不跨越重排**（`resize` 双线性与 `gray` 截断使色彩 / 几何并非总能交换，调序会改值）。
  仅用于减少全分辨率重放趟数与吞除死步，不改 `apply / geometryOutput` 语义。

| 文件                        | 职责                                                                          |
|---------------------------|-----------------------------------------------------------------------------|
| `preprocess_config.cpp`   | 枚举转字符串、默认参数构造、类型标签提取                                                        |
| `preprocess_pipeline.cpp` | 队列增删移改与按序执行（调 pixel_ops 完成实际计算）+ `geometryOutput` 几何推演 + `folded()` 逐像素等价折叠 |
