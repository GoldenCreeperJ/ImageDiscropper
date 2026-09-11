# include/geometry

**目录作用**：定义**标注图形**的几何数据——形状类型枚举、抽象形状接口与通用路径。
它是 `annotation`（标注层）的几何基础，服务于终稿 FR-1.3 的标注能力
（矩形 / 圆 / 直线 / 文字 / 画笔等），与引擎的「区域几何」（见 `engine/region.h`）相互独立。

**分块依据**：
1. `shape_type.h` —— 形状类型枚举 `ShapeType`，每种类型对应 `ShapeFactory` 的一条构造分支。
2. `path.h` —— 通用路径 `Path`（moveTo / lineTo / quadTo / cubicTo / close）与包围盒
   `BoundingBox`；所有形状都可转为路径，复用其扁平化、命中检测与包围盒计算。
3. `shapes.h` —— 抽象基类 `Shape` 与具体形状（直线 / 矩形 / 圆角矩形 / 椭圆 / 扇形 /
   多边形 / 路径 / 文字）。

| 文件 | 职责 |
|---|---|
| `shape_type.h` | 形状类型枚举 `ShapeType` 及其到字符串的映射 |
| `path.h` | 路径 `Path` / 路径段 `PathSegment` / 轴对齐包围盒 `BoundingBox` |
| `shapes.h` | `Shape` 抽象接口与各类具体形状 |
