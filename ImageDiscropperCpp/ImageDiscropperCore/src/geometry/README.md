# src/geometry

**目录作用**：`include/geometry` 下头文件的实现代码。

**分块依据**：
- `path.cpp` 实现路径构建、扁平化、命中检测、包围盒计算等纯几何算法，
  与具体形状类型无关，独立维护。
- `shapes.cpp` 实现每个具体形状类到 `Path` 的转换与自身属性计算；各 `clone()` 经基类 `copyXformTo(*s)`
  一并复制非破坏性变换 `xform_` 与累积 OBB 参数（`obbScaleX_/obbScaleY_/obbRotationDeg_`），保证深拷贝（含撤销重做快照、GUI 副本）不丢变换与面板回显基准。

| 文件           | 职责                                                                                                                                                      |
|--------------|---------------------------------------------------------------------------------------------------------------------------------------------------------|
| `path.cpp`   | Path 类实现（贝塞尔离散化、扫描线命中、点到折线距离）                                                                                                                           |
| `shapes.cpp` | `EllipseShape` / `PolygonShape` / `PathShape` / `TextShape` 实现（直线 / 折线 / 矩形类统一由 `PolygonShape` 承载，`toPath` 中 `cornerRadius>0` 时以二次贝塞尔对顶点倒角，开放折线仅内部顶点生效） |
