# src/geometry

**目录作用**：`include/geometry` 下头文件的实现代码。

**分块依据**：
- `path.cpp` 实现路径构建、扁平化、命中检测、包围盒计算等纯几何算法，
  与具体形状类型无关，独立维护。
- `shapes.cpp` 实现每个具体形状类到 `Path` 的转换与自身属性计算。

| 文件           | 职责                                                                                                         |
|--------------|------------------------------------------------------------------------------------------------------------|
| `path.cpp`   | Path 类实现（贝塞尔离散化、扫描线命中、点到折线距离）                                                                              |
| `shapes.cpp` | LineShape / RectShape / EllipseShape / ArcShape / RoundRectShape / PolygonShape / PathShape / TextShape 实现 |
