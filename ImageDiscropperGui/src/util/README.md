# util/ — 图像适配、降采样预览与无状态渲染工具

GUI 侧的**视图关注点**工具：Core 类型↔Qt 类型适配、大图降采样、以及从 app/canvas 下沉的无状态渲染（标注矢量绘制、导出离屏预览）；不含任何切割 / 几何 / 引擎逻辑（A-0.1）。

| 文件 | 职责 |
| ---- | ---- |
| `image_qt_adapter.{h,cpp}` | `core::Image` ↔ `QImage` / `QPixmap` 适配 |
| `path_qt_adapter.{h,cpp}`   | `geometry::Path` → `QPainterPath`、`core::Color` ↔ `QColor` 适配（标注矢量渲染/取色） |
| `preview_scaler.{h,cpp}`   | 大图降采样预览（复用 Core `pixel_ops::resize`） |
| `annotation_qt_painter.{h,cpp}` | `paintAnnotation`：把单条标注矢量绘到 `QPainter` 的**公共渲染函数**（画布图元与输出预览烘焙共用） |
| `output_preview_renderer.{h,cpp}` | 导出输出预览的**离屏渲染**：`bakeAnnotationsInto`（标注烘焙）+ `composeOutputThumbnail`（按 `Composition` 缩略图） |

## image_qt_adapter

- `QImage toQImage(const core::Image&)`：按 `format()` 构造对应 `QImage`
  （RGBA→`Format_RGBA8888`、RGB→`Format_RGB888`、GRAY→`Format_Grayscale8`），
  并**深拷贝**像素——Core 缓冲的生命周期独立于 Qt，避免悬垂引用。
- `QPixmap toPixmap(const core::Image&)`：在 `toQImage` 基础上转 `QPixmap`，供画布底图使用。

## path_qt_adapter

标注矢量渲染与属性面板取色的纯翻译层（不含几何计算，曲线离散化/命中/填充全在 Core，A-0.1）。

- `QPainterPath toQPainterPath(const geometry::Path&)`：逐段映射 `MOVE_TO`→`moveTo`、`LINE_TO`→`lineTo`、
  `QUAD_TO`→`quadTo`、`CUBIC_TO`→`cubicTo`、`CLOSE`→`closeSubpath`。QPainterPath 原生支持二/三次贝塞尔，
  故**无需先扁平化**（保留精度）。
- `QColor toQColor(const core::Color&)` / `core::Color toCoreColor(const QColor&)`：RGBA 8 位分量双向搬运，
  供标注画笔/画刷着色与属性面板 `QColorDialog` 取色回写 Core。

## preview_scaler

- `struct PreviewImage { core::Image image; double scaleX, scaleY; }`：预览副本 + 预览→原图的放大系数。
- `PreviewImage makePreview(const core::Image&, int maxDimension)`：最长边 ≤ `maxDimension` 时返回原图副本
  （`scale=1`）；否则调用 Core `pixel_ops::resize(..., NEAREST)` 降采样，`scaleX = 原宽 / 预览宽`。

## annotation_qt_painter

标注绘制的**唯一公共实现**（消除画布图元与导出预览两处重复）：`void paintAnnotation(QPainter&, const Annotation&, const QPainterPath& worldDrawPath, const AffineTransform& textXf)`。
调用方先把 painter 变换设为「世界（原图）坐标 → 设备」；本函数只按标注样式发绘制指令：

- 非文字：用 `worldDrawPath`（普通态＝`Shape::worldPath()`、拖拽预览态＝`xf.applyToPath(toPath())`）按 `color`/`strokeWidth` 描边（RoundJoin/RoundCap），`fillType` 时同色填充。
- 文字（TEXT）：以 `textXf`（局部→世界）临时叠加到 painter 后绘真实字形（字号 `fontSize`，基线偏移 `0.8*高`）——Core `TextShape::toPath` 仅给矩形边界，字形属渲染关注点。
- **不**绘选中高亮 / OBB 手柄（图元专属交互，留在 `AnnotationItem`）。

## output_preview_renderer

导出面板「输出图像预览」（G-11 / §4.6）的**离屏渲染**，从 app 层 `MainWindow` 下沉而来（无状态自由函数）：

- `QPixmap bakeAnnotationsInto(src, invX, invY, annotations)`：把标注矢量烘焙到 `src` 副本（working→源图 用 `invX/invY` 变换），逐条调 `paintAnnotation`——**只在源图（通常 ≤512）上绘一次**，成本与标注数成正比、与切割块数无关；不改传入的 `src`。
- `QPixmap composeOutputThumbnail(comp, src, invX, invY, maxDim)`：按已算好的 `Composition.placements` 把 `src` 用 `QPainter::drawPixmap` 按 `source→dest` blit 到最长边 ≤ `maxDim` 的小画布（**不落盘、不跑 Core**）：合并模式（`canvasWidth/Height>0`）等比缩放、所见即所得；分离模式（画布为 0）拼成触图（cols=ceil(√n)，每块等比居中）。`placements` 空 / `src` 空 / `maxDim<=0` / 触图格子过小 → 返回空 `QPixmap`。
- 坐标映射与 Core `exportSeparate/exportMerged` 的 `source→dest` 语义一致（预览忠实，溢出块由 `drawPixmap` 裁剪）。

> 场景坐标统一为原图像素坐标：底图 pixmap 用 `scaleX/scaleY` 变换铺回原图尺寸，
> 于是遮罩 / 切割线 / 选区可直接按原图坐标叠加（见 canvas/README.md）。
