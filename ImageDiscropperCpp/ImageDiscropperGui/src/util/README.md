# util/ — 图像适配、降采样预览与无状态渲染工具

GUI 侧的**视图关注点**工具：Core 类型↔Qt 类型适配、大图降采样、以及从 app/canvas 下沉的无状态渲染（标注矢量绘制、导出离屏预览）。不含任何切割 / 几何 / 引擎逻辑。（i18n 门面的实现与本层英文译文表已迁至 [`src/i18n/`](../i18n/README.md)。）

| 文件                                | 职责                                                                                                                                                                                                                      |
|-----------------------------------|-------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------|
| `image_qt_adapter.{h,cpp}`        | `core::Image` ↔ `QImage` / `QPixmap` 适配                                                                                                                                                                                 |
| `path_qt_adapter.{h,cpp}`         | `geometry::Path` → `QPainterPath`、`core::Color` ↔ `QColor` 适配（标注矢量渲染/取色）                                                                                                                                                |
| `preview_scaler.{h,cpp}`          | 大图降采样预览（复用 Core `core::Image::resized`）                                                                                                                                                                                 |
| `annotation_qt_painter.{h,cpp}`   | `paintAnnotation`：把单条标注矢量绘到 `QPainter` 的**公共渲染函数**（画布图元与输出预览烘焙共用）                                                                                                                                                       |
| `output_preview_renderer.{h,cpp}` | 导出输出预览的**离屏渲染**：`bakeAnnotationsInto`（标注烘焙）+ `composeOutputThumbnail`（按 `Composition` 缩略图）                                                                                                                              |
| `scrollable_dialog.{h,cpp}`       | 对话框滚动外壳与屏幕尺寸护栏（见下节）；依赖 `src/mobile/touch_scroll.cpp`（`installTouchScroll`）——后者虽住 `src/mobile/`，但 CMake 已归 common/util 组编译，供桌面 `idc_gui` 也链入                                                                             |
| `mobile/touch_scroll`（归本组）        | `installTouchScroll` + `WheelToHScrollFilter`：文件在 `src/mobile/`，因 app 层对话框（NewCanvasDialog / AppearanceDialog）与抽屉页签条带均需它，CMake 从移动组**升为 common/util 组**，`idc_gui`（纯桌面）也链入（installTouchScroll 桌面回落 QScroller、行为不变），免链接缺口 |
| `i18n/`（子目录）                      | `gui_i18n.cpp` / `lang_en.cpp` / `lang_en_detail.cpp` 已迁入 [`src/i18n/`](../i18n/README.md)，逐文件职责见该目录说明                                                                                                                  |

## image_qt_adapter

- `QImage toQImage(const core::Image&)`：按 `format()` 构造对应 `QImage`
  （RGBA→`Format_RGBA8888`、RGB→`Format_RGB888`、GRAY→`Format_Grayscale8`），
  并**深拷贝**像素——Core 缓冲的生命周期独立于 Qt，避免悬垂引用。
- `QPixmap toPixmap(const core::Image&)`：在 `toQImage` 基础上转 `QPixmap`，供画布底图使用。

## path_qt_adapter

标注矢量渲染与属性面板取色的纯翻译层（不含几何计算，曲线离散化/命中/填充全在 Core）。

- `QPainterPath toQPainterPath(const geometry::Path&)`：逐段映射 `MOVE_TO`→`moveTo`、`LINE_TO`→`lineTo`、
  `QUAD_TO`→`quadTo`、`CUBIC_TO`→`cubicTo`、`CLOSE`→`closeSubpath`。QPainterPath 原生支持二/三次贝塞尔，
  故**无需先扁平化**（保留精度）。
- `QColor toQColor(const core::Color&)` / `core::Color toCoreColor(const QColor&)`：RGBA 8 位分量双向搬运，
  供标注画笔/画刷着色与属性面板 `QColorDialog` 取色回写 Core。

## preview_scaler

- `struct PreviewImage { core::Image image; double scaleX, scaleY; }`：预览副本 + 预览→原图的放大系数。
- `PreviewImage makePreview(const core::Image&, int maxDimension)`：最长边 ≤ `maxDimension` 时返回原图副本
  （`scale=1`）；否则调用 Core `core::Image::resized(..., NEAREST)` 降采样，`scaleX = 原宽 / 预览宽`。

## annotation_qt_painter

标注绘制的**唯一公共实现**（消除画布图元与导出预览两处重复）：`void paintAnnotation(QPainter&, const Annotation&, const QPainterPath& worldDrawPath, const AffineTransform& textXf)`。
调用方先把 painter 变换设为「世界（原图）坐标 → 设备」；本函数只按标注样式发绘制指令：

- 非文字：用 `worldDrawPath`（普通态＝`Shape::worldPath()`、拖拽预览态＝`xf.applyToPath(toPath())`）以**笔＝`color`**按 `strokeWidth` 描边（RoundJoin/RoundCap）、**刷＝`fillColor`**（`fillType` 为真时填充内部，否则 `NoBrush`）——笔刷分色与 Core `rasterize` 一致；折线 / 画笔等开放路径由 `fillPath` 首尾隐式闭合后填充。
- 文字（TEXT）：以 `textXf`（局部→世界）临时叠加到 painter 后绘真实字形（字号 `fontSize`，基线偏移 `0.8*高`）——Core `TextShape::toPath` 仅给矩形边界，字形属渲染关注点。
- **不**绘选中高亮 / OBB 手柄（图元专属交互，留在 `AnnotationItem`）。

## output_preview_renderer

导出面板「输出图像预览」的**离屏渲染**，从 app 层（`PreviewController`）下沉而来（无状态自由函数）：

- `QPixmap bakeAnnotationsInto(src, invX, invY, annotations)`：把标注矢量烘焙到 `src` 副本（working→源图 用 `invX/invY` 变换），逐条调 `paintAnnotation`——**只在源图（通常 ≤512）上绘一次**，成本与标注数成正比、与切割块数无关；不改传入的 `src`。
- `QPixmap composeOutputThumbnail(comp, src, invX, invY, maxDim)`：按已算好的 `Composition.placements` 把 `src` 用 `QPainter::drawPixmap` 按 `source→dest` blit 到最长边 ≤ `maxDim` 的小画布（**不落盘、不跑 Core**）：合并模式（`canvasWidth/Height>0`）等比缩放、所见即所得；分离模式（画布为 0）拼成触图（cols=ceil(√n)，每块等比居中）。`placements` 空 / `src` 空 / `maxDim<=0` / 触图格子过小 → 返回空 `QPixmap`。
- 坐标映射与 Core `exportSeparate/exportMerged` 的 `source→dest` 语义一致（预览忠实，溢出块由 `drawPixmap` 裁剪）。

## scrollable_dialog

小屏 / 移动端对话框的三件跨对话框通用能力（真机反馈 items 2/4/7/8/9 同一簇根因：窗口被内容撑到超出屏幕、按钮行被顶出画面 → 显示不全且无法关闭）：

- `QScrollArea* wrapScrollable(QWidget* body)`：把主体装进 `QScrollArea`（widgetResizable、横滚 AlwaysOff、纵滚 AsNeeded、无边框）
  并 `installTouchScroll`——移动端拖指滑、桌面回落 QScroller 鼠标协调器（行为不变）。对话框把 `QDialogButtonBox` 留在滚动区**外**固定底部 → 按钮恒可见可达。
- `void limitDialogToScreen(QDialog*, const QSize& preferredContentSize = {})`：装一个 `DialogSizeGuard` 事件过滤器，Show/Resize 时按
  `screen()->availableGeometry()` × `nums::mobile::kDialogMax{Width,Height}Ratio` 钳最大宽/高；首次显示把尺寸收到「内容首选尺寸（缺省用对话框 sizeHint）
  与钳值」的较小者；旋转表现为 resize → 只重钳、部件零重建。桌面大屏钳值远超内容，等效无约束。
- `void showScrollableMessage(parent, title, text, okLabel)`：以可滚动外壳呈现长文本信息框（正文 word wrap + `kInfoDialogMinWidthPx` 最小宽 + 底部确定常驻），
  替代 `QMessageBox` 的一长条窄高呈现（使用说明 / 关于）。入参为已按当前语言解析的字符串（本层零 i18n 依赖）。

> 场景坐标统一为原图像素坐标：底图 pixmap 用 `scaleX/scaleY` 变换铺回原图尺寸，
> 于是遮罩 / 切割线 / 选区可直接按原图坐标叠加（见 canvas/README.md）。

## i18n

i18n 门面的实现与 GUI 英文译文表已迁至 [`src/i18n/`](../i18n/README.md)（`gui_i18n.cpp` 语言决议 + `lang_en.cpp`〔menu/label〕与 `lang_en_detail.cpp`〔tooltip/status/msg〕两张译文表）；装配链与调用时机 / 译文纪律 / 加一门语言 / 热切换重放等详见该目录说明。
