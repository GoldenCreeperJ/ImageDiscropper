# include/generate

**目录作用**：**程序化图案生成**——按参数产出一张全新的 RGBA 画布图像（不依赖任何已有图像）。
服务于 GUI「文件 → 新建」：用户给出尺寸、底色（含 alpha，可透明）、前景色与图案参数，
经 `EngineBridge::generatePattern` 委托本模块 `makePattern` 生成，作为切割引擎的初始输入图。
CLI 不需要该能力，故不引用本模块。

> 与 `preprocess`（对已有图施加变换）不同，本模块是**从零生成像素**，无输入图。

**分块依据**：单一职责——`pattern.h` 同时承载「参数词表」（`PatternKind` / `Orientation` /
`PatternParams`）与「生成入口」（`makePattern`），生成逻辑内聚，GUI 只搬运参数、不复现像素运算
（CONTRIBUTING.md「分层纪律」）。

| 文件          | 职责                                                                                                                                                                                                                                                                                                                                                                                                                                                        |
|-------------|-----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------|
| `pattern.h` | `PatternKind`（纯色/线性渐变/径向渐变/棋盘/条纹/网格线/点阵/同心环/放射线/色相渐变/噪声）、`Orientation`（Horizontal/Vertical/DiagonalDown↘/DiagonalUp↗，供条纹 4 向与同心环（圆环·方环））、`ImageAnchor`（9 锚点：center + 8 靠边；供图层落位 `canvas::ImageTransform` 复用，非图案参数）、`NoiseMode`（FullColor/Grayscale/TwoColor）、`PatternParams`（底色含 alpha + 前景色 + 周期/线宽/点半径 + seed/noiseMode）、`makePattern`（恒为程序化图案，无输入图；「贴图图案」`PatternKind::Image` 与 `ImageScale` 已彻底移除，图片改由 GUI 侧「附加图片图层」经 `canvas::composeCanvas` 两段合成承载） |

**约定**：`makePattern` 恒输出 `RGBA`；非法尺寸（宽或高 ≤ 0，或任一边 > `kMaxDimension`）返回空图（`empty()` 为真）；
`period/lineWidth/dotRadius` 内部钳到合法下限（≥2 / ≥1 / ≥1）；底色 alpha 直接写入像素。
`seed`/`noiseMode` 仅对 `Noise` 生效（其余图案忽略）；噪声由 `seed`+坐标经确定性 32 位整数哈希逐像素生成（无浮点，跨编译器/跨语言可复现），同 `seed`+同尺寸恒同图，供 C++ Rust 逐像素对拍。
