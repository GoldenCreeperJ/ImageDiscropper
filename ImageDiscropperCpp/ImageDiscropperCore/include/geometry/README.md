# include/geometry

**目录作用**：定义**标注图形**的几何数据——形状类型枚举、抽象形状接口与通用路径。
它是 `annotation`（标注层）的几何基础，服务于终稿 FR-1.3 的标注能力
（矩形 / 圆 / 直线 / 文字 / 画笔等），与引擎的「区域几何」（见 `engine/region.h`）相互独立。

**分块依据**：
1. `shape_type.h` —— 形状类型枚举 `ShapeType`，每种类型对应 `ShapeFactory` 的一条构造分支。
2. `path.h` —— 通用路径 `Path`（moveTo / lineTo / quadTo / cubicTo / close）与包围盒
   `BoundingBox`；所有形状都可转为路径，复用其扁平化、命中检测与包围盒计算。
3. `shapes.h` —— 抽象基类 `Shape` 与具体形状（直线 / 矩形 / 圆角矩形 / 椭圆 /
   多边形 / 路径 / 文字）；基类提供 `translate(dx,dy)` 就地平移（各子类偏移自身参数，保留具体类型）。
4. `affine_transform.h` —— header-only 2×3 仿射矩阵 `AffineTransform`（平移/缩放/旋转构造、矩阵乘法、作用于点/路径、行列式/逆/isIdentity），为形状提供**非破坏性变换**。

**非破坏性变换（方案 A）**：`Shape` 基类持有 `AffineTransform xform_`（默认单位阵），变换**累积存入矩阵、不改子类参数化几何**（旋转矩形仍为矩形，不退化、不丢文字字形）。对外统一经 `worldPath()`/`worldBounds()`/`controlPointsWorld()` 取「已变换」结果供渲染/命中/导出；单位阵时回落 `toPath()`/`bounds()`/`controlPoints()`（向后兼容）。世界系平移经 `translateWorld`（把世界位移用逆线性换算到局部，使旋转/缩放后拖动方向仍跟随光标），`transform()` 只读返回累积矩阵；缩放/拉伸/旋转/翻转统一经下文的 OBB 交互变换（`applyObbTransform`）。`clone()` 必须复制 `xform_` 以保留变换。

**定向包围盒交互变换（阶段 B）**：`Shape` 另提供 `localToWorld`/`worldToLocal`（局部↔世界点映射）与 `obbPreviewTransform(sx,sy,deg)`/`applyObbTransform(sx,sy,deg)`，供 GUI 画布拖拽**定向包围盒（OBB）** 的 8 个缩放手柄 + 1 个旋转手柄做缩放 / 拉伸 / 旋转 / 翻转。复合语义分处矩阵左右两侧：**先**绕局部盒中心沿形状**自身轴**缩放（右乘，不产生世界轴剪切），**再**对当前世界外观做**绕世界中心的刚性旋转**（左乘）。旋转走世界系是关键——若把旋转也塞进局部右乘，则「非均匀缩放 ∘ 旋转」会退化为剪切，使已拉伸的形状一旋转就彻底变形；缩放系数为负即翻转（拖手柄越过对边自然产生负系数，无需独立翻转按钮）。`obbPreviewTransform` 只算不改状态、供拖拽逐帧矢量预览，`applyObbTransform` 写入与之完全一致的结果，保证预览帧与释放后提交帧逐像素一致（所见即所得）。GUI 只采集手柄拖拽、把世界光标经 `worldToLocal` 映回局部算缩放/旋转系数，矩阵运算全在 Core（A-0.1）。

**累积 OBB 参数（供面板忠实回显）**：`applyObbTransform` 同时累积带符号参数 `obbScaleX_ *= sx`、`obbScaleY_ *= sy`、`obbRotationDeg_ += deg`（与 `xform_` 线性部分 `R(Σdeg)·diag(Πsx,Πsy)` 一致），由 `obbScaleX()`/`obbScaleY()`/`obbRotationDeg()` 读出。之所以显式存储而不从 `xform_` 分解：矩阵分解无法区分 `(sx<0)` 与 `(θ+180, sy<0)`（二重歧义），无法忠实展示翻转的负缩放。`clone()` 经 `copyXformTo` 一并复制这组参数（连同 `xform_`），避免深拷贝/撤销重做丢失回显基准。

| 文件 | 职责 |
|---|---|
| `shape_type.h` | 形状类型枚举 `ShapeType` 及其到字符串的映射 |
| `path.h` | 路径 `Path` / 路径段 `PathSegment` / 轴对齐包围盒 `BoundingBox` |
| `shapes.h` | `Shape` 抽象接口与各类具体形状（含非破坏性变换：worldPath/worldBounds/controlPointsWorld/translateWorld/transform()；及 OBB 交互变换：localToWorld/worldToLocal/obbPreviewTransform/applyObbTransform） |
| `affine_transform.h` | 2×3 仿射矩阵 `AffineTransform`（header-only）：平移/缩放/旋转、乘法、作用于点/路径、行列式/逆/isIdentity |
