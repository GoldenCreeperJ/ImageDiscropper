# include/core

**目录作用**：定义整个库最底层、无外部依赖的基础数据类型与通用能力。所有上层模块
（`geometry` / `annotation` / `preprocess` / `engine`）
都直接或间接依赖这里的类型。对应 SPEC §2.2 的 `Image` 概念与 FR-1.5 / NFR-4 的撤销重做能力。

**分块依据**：将「数据」与「算法」解耦，把最基础的像素点、颜色、图像三种数据结构
独立成三个头文件，避免上层出现循环依赖；与业务无关的通用能力（泛型撤销 / 重做栈，
原 `history` 模块）同样落在本层，供标注层 / GUI 文档历史复用。

| 文件                  | 职责                                                                                             |
|---------------------|------------------------------------------------------------------------------------------------|
| `color.h`           | RGBA 颜色结构体 `Color` 及颜色运算（反色 / 感知反色 / 互补色 / HSV 转换）                                             |
| `point.h`           | 二维浮点坐标 `Point2D`、整数点 `Point2I` 及其算术运算                                                          |
| `image.h`           | 图像像素容器 `Image`，支持 RGB / RGBA / GRAY 三种模式，行主序存储                                                 |
| `history_manager.h` | 泛型撤销 / 重做栈 `HistoryManager<T>`：`push` / `popToRedo` / `popFromRedo` 等（纯模板头实现，原 `history` 模块并入） |

> 语义：`push` 追加新条目并清空重做栈（新操作切断「未来」）；`popToRedo` 撤销、
> `popFromRedo` 重做。主栈经 `undoStack()`（只读 / 可写两个重载）对外暴露：可写重载供使用方对「当前有效条目」
> 原地编辑（如标注 EDIT 模式改样式，不新增历史条目）。当前由 `annotation::AnnotationLayer`
> 复用为其标注撤销重做栈，GUI `DocHistory` 以 `T=EngineConfig` 实例化承载文档参数态历史。

> 坐标与像素约定遵循 SPEC §2.1：整数像素、原点左上、`x` 向右 `y` 向下。
