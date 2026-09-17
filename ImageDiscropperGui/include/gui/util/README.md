# include/gui/util — 图像适配与预览工具对外声明

**目录作用**：GUI **工具层**的公共头，声明 `core::Image` ↔ Qt 图像类型适配、`geometry::Path`/`core::Color` ↔ Qt 绘图类型适配与大图降采样预览工具。
本目录只放**类声明边界**；实现与详尽说明见 [src/util/](../../../src/util/README.md)。

**分块依据**：均为 GUI 侧的**视图关注点**工具，不含任何切割 / 几何 / 引擎逻辑（A-0.1）。降采样**复用 Core
`pixel_ops::resize`**，不自造缩放（A-0.3）。按「图像适配 / 几何·颜色适配 / 预览生成」关注点各成一文件。

| 文件 | 声明 | 职责 |
|---|---|---|
| `image_qt_adapter.h` | `toQImage` / `toPixmap` | `core::Image` → `QImage` / `QPixmap`（按 `format()` 深拷贝像素，Core 缓冲生命周期独立于 Qt，避免悬垂引用） |
| `path_qt_adapter.h` | `toQPainterPath` / `toQColor` / `toCoreColor` | `geometry::Path` → `QPainterPath`（逐段映射 moveTo/lineTo/quadTo/cubicTo/close，保留贝塞尔精度）、`core::Color` ↔ `QColor`（供标注矢量渲染与属性面板取色） |
| `preview_scaler.h` | `PreviewImage` / `makePreview` | 大图降采样预览：最长边超阈值时调 Core `pixel_ops::resize(NEAREST)`，回传预览副本 + 预览→原图放大系数 |

> 命名空间统一 `idc::gui`。场景坐标统一为原图像素坐标：底图 pixmap 用 `scaleX/scaleY` 变换铺回原图尺寸（见 canvas/）。
