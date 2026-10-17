# src/generate

**目录作用**：`include/generate` 声明边界的**实现**——程序化图案的逐像素生成核。

**实现说明**：
- `pattern.cpp` 实现 `makePattern`：先按 `background` 铺满整张（`Solid` 即此终点、直接返回），
  再对纹理类逐像素叠前景：
  - 渐变（线性沿 `orientation` 轴按 `i/(span-1)`、径向按归一化半径 `r/maxR`）走 `background→foreground`
    逐通道（含 alpha）线性插值；
  - 棋盘 `(x/period + y/period)` 奇偶、条纹 `(v/period)` 奇偶（`v` 为 `orientation` 轴坐标，斜向经 `floorDiv` 处理负号）；
  - 网格线用「距最近 `period` 整倍 ≤ lineWidth/2」判前景；点阵取最近格点欧氏距离 ≤ `dotRadius`（平方距离比较免开方）；
  - 同心环用「距最近整环 ≤ lineWidth/2」（`orientation` 选圆环/方环）；放射线以图像中心为顶点、扇区数按 `period` 在参考半径上定标、奇偶交替；
  - 色相渐变沿 `orientation` 轴扫 `H∈[0,1)`（`period` 为一道彩虹宽），经 `core::hsvToRgb` 全饱和全明度出彩虹；
  - 噪声由 `seed`+坐标经确定性 32 位整数哈希逐像素生成（无浮点，跨编译器/跨语言可复现），`noiseMode` 选全彩 / 灰度 / 双色，同 `seed`+同尺寸恒同图。
- 中心取 `((width-1)/2, (height-1)/2)`；参数钳到合法下限；越界坐标不触碰（逐像素遍历天然不越界）。
- 无第三方依赖；`<cmath>` 局部自持 π 常量（避免 MSVC `M_PI` 需 `_USE_MATH_DEFINES`）。
