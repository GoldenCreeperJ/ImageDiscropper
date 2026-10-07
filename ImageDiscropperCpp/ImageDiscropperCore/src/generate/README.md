# src/generate

**目录作用**：`include/generate` 声明边界的**实现**——程序化图案的逐像素生成核。

**实现说明**：
- `pattern.cpp` 实现 `makePattern`：先按 `background` 铺满整张（`Solid` 即此终点、直接返回），
  再对纹理类逐像素叠前景：
  - 渐变（线性沿 `orientation` 轴按 `i/(span-1)`、径向按归一化半径 `r/maxR`）走 `background→foreground`
    逐通道（含 alpha）线性插值；
  - 棋盘 `(x/period + y/period)` 奇偶、条纹 `(v/period)` 奇偶（`v` 为 `orientation` 轴坐标）；
  - 网格线用「距最近 `period` 整倍 ≤ lineWidth/2」判前景；点阵取最近格点欧氏距离 ≤ `dotRadius`；
  - 同心环用「距最近整环 ≤ lineWidth/2」（`orientation` 选圆环/方环）；放射线以图像中心为顶点、扇区数按 `period` 在参考半径上定标、奇偶交替。
  - 图片（`Image`）把上层解码传入的 `sourceImage` 按 `imageAnchor`（9 锚点）source-over 合成到底色画布；`imageScale` 选 `Original`（源图大于画布时自动把画布撑大到容纳源图）或 `Stretch`（拉伸铺满、忽略宽高比）；`makePattern` 不读盘。
- 中心取 `((width-1)/2, (height-1)/2)`；参数钳到合法下限；越界坐标不触碰（逐像素遍历天然不越界）。
- 无第三方依赖；`<cmath>` 局部自持 π 常量（避免 MSVC `M_PI` 需 `_USE_MATH_DEFINES`）。
