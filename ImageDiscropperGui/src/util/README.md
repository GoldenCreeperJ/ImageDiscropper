# util/ — 图像适配与降采样预览

GUI 侧的**视图关注点**工具，不含任何切割 / 几何 / 引擎逻辑（A-0.1）。

| 文件 | 职责 |
| ---- | ---- |
| `image_qt_adapter.{h,cpp}` | `core::Image` ↔ `QImage` / `QPixmap` 适配 |
| `path_qt_adapter.{h,cpp}`   | `geometry::Path` → `QPainterPath`、`core::Color` ↔ `QColor` 适配（标注矢量渲染/取色） |
| `preview_scaler.{h,cpp}`   | 大图降采样预览（复用 Core `pixel_ops::resize`） |

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

> 场景坐标统一为原图像素坐标：底图 pixmap 用 `scaleX/scaleY` 变换铺回原图尺寸，
> 于是遮罩 / 切割线 / 选区可直接按原图坐标叠加（见 canvas/README.md）。
