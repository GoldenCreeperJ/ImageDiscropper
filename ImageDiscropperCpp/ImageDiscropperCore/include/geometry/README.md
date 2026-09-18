# include/geometry

**目录作用**：定义**标注图形**的几何数据——形状类型枚举、抽象形状接口与通用路径。
它是 `annotation`（标注层）的几何基础，服务于 SPEC §3.1（FR-1.3） 的标注能力
（矩形 / 圆 / 直线 / 文字 / 画笔等），与引擎的「区域几何」（见 `engine/region.h`）相互独立。

**分块依据**：
1. `shape_type.h` —— 形状类型枚举 `ShapeType`，每种类型对应 `ShapeFactory` 的一条构造分支。
2. `path.h` —— 通用路径 `Path`（moveTo / lineTo / quadTo / cubicTo / close）与包围盒
   `BoundingBox`；所有形状都可转为路径，复用其扁平化、命中检测与包围盒计算。
3. `shapes.h` —— 抽象基类 `Shape` 与具体形状（直线 / 矩形 / 圆角矩形 / 椭圆 /
   多边形 / 路径 / 文字）；基类提供 `translate(dx,dy)` 就地平移（各子类偏移自身参数，保留具体类型）。
4. `affine_transform.h` —— header-only 2×3 仿射矩阵 `AffineTransform`（平移/缩放/旋转构造、矩阵乘法、作用于点/路径、行列式/逆/isIdentity），为形状提供**非破坏性变换**。

**shapes.h 的设计要点**（完整机制见头文件注释与 `src/geometry`）：

- **非破坏性变换**：`Shape` 持有 `AffineTransform xform_`，变换**累积存入矩阵、不改子类参数化几何**（旋转矩形仍为矩形、不丢文字字形）；对外经 `worldPath()`/`worldBounds()`/`controlPointsWorld()` 取已变换结果，单位阵时回落 `toPath()` 等（向后兼容）。
- **OBB 交互变换**：`obbPreviewTransform`/`applyObbTransform` 供 GUI 画布手柄做缩放/拉伸/旋转/翻转——先绕局部盒中心沿自身轴缩放、再绕世界中心刚性旋转（旋转走世界系，避免「非均匀缩放 ∘ 旋转」退化为剪切）；预览与提交同一算子，所见即所得。
- **累积 OBB 参数**：带符号 `obbScaleX_/obbScaleY_/obbRotationDeg_` 显式存储（而非从 `xform_` 分解——分解无法区分 `sx<0` 与 `θ+180°&sy<0` 的二重歧义），供属性面板忠实回显翻转的负缩放；`clone()` 经 `copyXformTo` 一并复制。

| 文件                   | 职责                                                                                                                                                                          |
|----------------------|-----------------------------------------------------------------------------------------------------------------------------------------------------------------------------|
| `shape_type.h`       | 形状类型枚举 `ShapeType` 及其到字符串的映射                                                                                                                                                |
| `path.h`             | 路径 `Path` / 路径段 `PathSegment` / 轴对齐包围盒 `BoundingBox`                                                                                                                        |
| `shapes.h`           | `Shape` 抽象接口与各类具体形状（含非破坏性变换：worldPath/worldBounds/controlPointsWorld/translateWorld/transform()；及 OBB 交互变换：localToWorld/worldToLocal/obbPreviewTransform/applyObbTransform） |
| `affine_transform.h` | 2×3 仿射矩阵 `AffineTransform`（header-only）：平移/缩放/旋转、乘法、作用于点/路径、行列式/逆/isIdentity                                                                                                |
