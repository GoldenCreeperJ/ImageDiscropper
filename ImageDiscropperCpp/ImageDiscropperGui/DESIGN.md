# ImageDiscropper GUI 设计说明

> 面向维护者的设计文档（对应 guideline §7.2 / 交付物 D-2）。
> 最终用户操作手册请读同目录 [`USAGE.md`](USAGE.md)（§7.1 使用说明文档）。

本层是可执行目标 `idc_gui`，基于 **Qt 6.x + Widgets + QGraphicsView**，命名空间统一为 `idc::gui`。
GUI 是 Core（`image_discropper_core` 静态库）的**纯消费者**：只采集交互、把交互翻译为 Core 参数、
调用 Core、渲染 Core 结果；**不实现任何切割/几何/排序/极性/导出逻辑**（guideline §0 A-0.1）。

---

## 1. 分层结构与职责

采用与 Core / CLI 一致的 **include/src 分离**：头文件与其「声明边界 README」在 `include/<模块>/`，
实现与其「实现说明 README」在 `src/<模块>/`。构建**包含根为 `include`**，故源码内以 `"canvas/…"`、
`"model/…"` 形式互相引用（无 `gui/` 前缀）。

| 模块 | 头文件 | 实现 | 职责 |
| ---- | ------ | ---- | ---- |
| `app` | `include/app/` | `src/app/` | 主窗口装配与编排（`MainWindow`）、程序入口（`main.cpp`） |
| `model` | `include/model/` | `src/model/` | 会话状态单一真相源 `Document`；唯一切割引擎入口 `EngineBridge`；唯一标注驱动 `AnnotationBridge` |
| `canvas` | `include/canvas/` | `src/canvas/` | `QGraphicsView`/`Scene` 画布：图层化渲染 + 交互 |
| `panels` | `include/panels/` | `src/panels/` | 左侧模式/极性/工具/图层面板；右侧参数/导出/图像/标注属性面板 |
| `util` | `include/util/` | `src/util/` | 无状态适配与渲染工具：Core↔Qt 类型适配、降采样预览、输出缩略图合成/标注烘焙 |

**依赖方向**：`app → panels/canvas/model → util → Core`。`model` 是唯一触碰 Core 引擎/标注的层，
面板与画布只读写 `Document`/桥，不各自持有真相（降低耦合）。

---

## 2. 布局结构与响应式规则

- 顶层 `QSplitter` 水平三分：**左侧 `QScrollArea`（竖排 LeftPanel + ToolPanel + LayerPanel）** | **中央 `CanvasView`** | **右侧 `QTabWidget`（参数页 / 导出页 / 图像页 / 标注页）**。
- **左侧面板固定宽约 220px**；**右侧面板约 300px**、可随 `QSplitter` 拖动折叠；中央画布占最大空间、伸缩优先。
- 左侧用 `QScrollArea` 包裹，窗口变窄/内容变高时出现纵向滚动条而不挤压画布。
- 右侧用 `QTabWidget` 分组，避免面板过长；**输出预览缩略图仅在「导出」页为当前选项卡时才渲染**（见 §6 按需渲染）。
- 主窗口尺寸/分割比例由 `QSplitter` 记忆；控件用布局管理器（`QVBoxLayout`/`QGridLayout`）自适应，无绝对定位。

---

## 3. 画布交互模型（事件流 + 状态机）

### 3.1 图层 z 序（`include/canvas/z_order.h`，单一定义点）

| z 值 | 常量 | 图层 |
| ---- | ---- | ---- |
| 0 | `kBase` | 底图（预处理后） |
| 10 | `kAnnotation` | 标注图层（矢量叠加） |
| 20 | `kDelete` | 删除块遮罩（红，铺满全图作底） |
| 30 | `kKeep` | 保留块遮罩（绿，叠加于红之上） |
| 40 | `kGrid` | 网格线（L3 / L2 多矩形诱导） |
| 50 | `kCutLine` | 切割线层（保留值；切割线已并入选区图元） |
| 60 | `kSelection` | 选区边框（橙）+ 手柄 + 贯穿切割线延伸（同一图元） |
| 70 | `kCellNumber` | 单元格编号角标（L3 自定义序） |

> **遮罩渲染法（不做几何布尔）**：`EngineResult.kept` 只给出保留块。画布先铺一层覆盖全图的红色「删除底」，
> 再按 `kept` 各片段叠加绿色「保留块」；绿覆盖处即保留、透红处即删除。GUI 无需自算「删除集」（A-0.1）。
> 因此 `kDelete < kKeep`，视觉与 §5.3 一致。

### 3.2 场景坐标

**场景坐标 = 原图像素坐标**。底图用降采样 pixmap 经 `QTransform::fromScale(scaleX, scaleY)` 铺到原图尺寸，
遮罩/切割线/选区/标注都直接按原图坐标叠加，避免到处换算。几何在全分辨率算、只在预览图上画（NFR-2/3）。

### 3.3 交互事件流

```
鼠标/键盘 (CanvasView)
   │  拖拽框选 rubberSelect / 微调 nudgeSelection / 光标 cursorScenePos / 缩放 zoomChanged
   ▼
MainWindow 编排槽 (onRubberSelect / onNudge / onCursor …)
   │  写回 Document（L1/L2 setRect、MULTI_RECT addRect、L3 命中单元 addCells）
   ▼
Document.changed ──► onDocChanged ──► refreshPreview()
   │                                      │ buildEngineConfig + EngineBridge.runPreview
   │                                      ▼
   │                          CanvasScene 刷新：切割线 / 遮罩 / 选区 / 网格 / 编号 + 输出预览
   ▼
scheduleHistoryCapture（500ms 防抖）──► docHistory_ 压入 EngineConfig 快照（G-12）
```

- **选区即切割线**：单矩形诱导的四条切割线恰落在选区矩形四边，是选区边的「贯穿全图延伸」。
  **无独立切割线图元**——由唯一的橙色 `SelectionRectItem` 一并绘制选区框与被标记边的贯穿延伸线，
  支持直接抓任一条边沿法向拖动（拖边＝移动该切割线）。哪几条边延伸由 `CanvasScene` 依 Core
  `generateCutLines` 结果下发（`setCutEdges`），GUI 不自算几何（A-0.1/A-0.11）。
- **拖拽期防抖**：`isDraggingSelection()` / `isDraggingMultiRect()` 期间跳过 `syncPanels()` 与对正在拖图元的回设，
  避免逐帧量化抖动与「拖拽不跟手」；释放时照常同步一次。
- **吸附**：`selection_rect_item` 的 `snap1(v, th, targets)` 在拖动/缩放时对图像边缘 `(0/W/H)` 与中心 `(W/2,H/2)`
  吸附，阈值默认 8px；Move 模式整体平移、Resize 模式逐边吸附（保持尺寸不突变）。
- **L3 单元交互**：`CellPickerItem` 支持单击切换、框选（`cellsIntersecting`）、自定义序拖拽调序
  （被拖单元橙粗框、目标单元黄虚线框），并在 CUSTOM 排序下以设备像素绘制序号角标（字号恒定屏幕大小）。

### 3.4 绘制/编辑状态机（标注）

- **绘制态门控**：SELECT 工具时 `setAnnotationDrawActive(false)`，画布维持橡皮筋选区/单元交互；
  任一绘制工具时置 `true`，左键拖拽路由到标注绘制（`annoDragStart/Move/End`）。
- 两点形状：`begin → updateShape（逐帧 pendingChanged 实时预览）→ commit`；
  折线/画笔：`begin → append → commit`（Esc 或切工具收笔）；文字：`QInputDialog` 取文本 → `addText`。
- 右键在绘制态下＝收笔（`annoFinish`），不弹视图菜单；非绘制态弹「清除切割线 / 重置视图 / 切换预览遮罩」。

---

## 4. 视觉规范

| 项 | 规范 |
| -- | ---- |
| 画布背景 | 深灰 `rgb(45,45,45)` |
| 保留遮罩 | 绿 `rgba(0,200,0,60)`（`mask_layer.cpp` `kKeepColor`） |
| 删除遮罩 | 红 `rgba(200,0,0,60)`（`kDeleteColor`） |
| 网格线 | 灰 `rgb(150,150,150)`，1px 虚线（L3）；橙 `rgb(255,140,0)` 实线（L2 多矩形诱导切割线） |
| 选区/切割线 | 橙 `rgb(255,140,0)`，高亮 `rgb(255,90,0)`；填充 `rgba(255,165,0,30~60)` |
| 标注高亮/手柄 | 蓝 `rgb(0,160,230)`；旋转手柄白底蓝边圆形（与缩放手柄区分） |
| 单元编号角标 | 黑底 `rgba(0,0,0,175)` + 白字，13px bold，圆角 3px |
| 工具按钮 | 中性底 `#f2f2f2` + 深色字 `#1b1b1b`；选中态蓝 `#3b7ddd` + 白字（深色模式下显式指定文字色，避免白字浅底不可读） |
| 线宽 | 选区/切割线/标注手柄用 cosmetic pen（屏幕线宽恒定，不随缩放变粗） |
| 字体/文案 | 界面文字全中文；`QStringLiteral` 包裹字面量 |
| 反馈 | 错误经 `notify()` 写状态栏、限时显示（非模态，§5.2）；所有交互 100ms 内视觉反馈 |

---

## 5. 与 Core 的数据流

`EngineBridge` 是**唯一**触碰切割引擎的类，`AnnotationBridge` 是**唯一**驱动 Core 标注的类。

```
Document（状态：模式/极性/选区/导出参数）── buildEngineConfig ──► EngineConfig
        │                                                        │
        ▼                                                        ▼
EngineBridge.runEngine / generateCutLines / exportImage    （Core engine::*）
        │  EngineResult{ok,error,kept,collapsible,composition} / CutLineSet
        ▼
CanvasScene 渲染 + MainWindow 状态栏/面板回显
```

**已核对的 Core API**（均来自实际头文件）：

- 编排：`engine::runEngine(const core::Image&, const EngineConfig&) -> EngineResult`
- 切割线：`engine::generateCutLines(const CutConfig&, const SourceInfo&) -> CutLineSet`
- 配置：`EngineConfig{source,preprocess,cut,selectedCells,order,emitParams}`；
  `CutConfig{tier,generator(RECT/HORIZONTAL_LINE/VERTICAL_LINE/MULTI_RECT/GRID),rect,rects,grid,polarity}`
- 配置存取（G-13）：`engine::saveEngineConfig(path,cfg)` / `engine::loadEngineConfig(path,out&)`（§9 schema JSON，nlohmann 在 Core 侧 PRIVATE）
- 撤销历史（G-12）：`history::HistoryManager<T>{push,popToRedo,popFromRedo,top,canUndo,canRedo,undoSize,clearAll}`（GUI 以 `T=EngineConfig` 实例化）
- 导出：`CompositionParams{mode,layout,mergeOrder,cols,rows,cellWidth,cellHeight,padColor,format,naming,quality,keepMetadata}`；`Composition{canvasWidth,canvasHeight,placements,…}`；`engine::exportImage(...)`
- 区域：`RectRegion{left,top,right,bottom,width(),height(),area()}`；`RegionSet{size(),empty(),fragments(),boundingBox()}`；`Fragment{region,index,kind,row,col}`
- I/O 与处理：`engine::readImageFile`（统一解码 RGBA）；`pixel_ops::resize/rotate/flip/...`；`core::Image`；`core::Color{r,g,b,a}`
- 标注：`geometry::Shape::worldPath()/obbPreviewTransform/applyObbTransform/translateWorld`；`annotation::AnnotationLayer`（`revoke/redo/removeAnnotation/rasterize`）

---

## 6. 预处理与标注的数据流

### 6.1 预处理（FR-1 / G-3）

`ImagePanel` 发意图信号（`rotateRequested`/`flipRequested`/`scaleRequested`/`resizeRequested`/`grayRequested`/
`invertRequested`/`splitRequested`/`resetRequested`）→ `MainWindow` 对应 `on*` 槽经 `EngineBridge` 调 Core
`pixel_ops::*` 变换 `doc_.working()` → 公共收尾 `applyWorkingImage(next, okMsg)`：

- 结果为空图则报错；
- **维度变化（旋转 90/270、缩放）时先清除失效选区**（`clearRect/clearRects/clearCells`，坐标基于旧尺寸）再 `setWorkingImage`（触发 `imageChanged` → 重建预览底图 + 刷新）；
- `onResetPreprocess` 把工作图还原为 `original_`（原图始终保留）；
- 色道反色/分离共用面板 R/G/B 复选框作「作用通道」选择器（`invertChannels` 本就支持掩码，无需改 Core）；
- 大图缩放/重采样经 `runWithBusyDialog(text, op)`：应用级模态、不可取消的 `QProgressDialog`，配 `busyResample_` 重入守卫。

### 6.2 标注（G-4 / G-5）

标注是**独立矢量图层**，与底图分离、不改像素（A-0.15）。每个 Core `Annotation` 对应一个 `AnnotationItem`
（`QGraphicsObject`），用 `Shape::worldPath()`→`QPainterPath` 矢量叠加绘制（z=`kAnnotation`）。

```
AnnotationBridge.changed/selectionChanged/toolChanged → MainWindow onAnno* → scene_->updateAnnotations + annoPropPanel_->syncFromModel
ToolPanel.toolSelected → onToolSelected（先 commitPath 收笔 → annoBridge_.setTool）
CanvasView.annoDrag* → onAnnoDrag*（依工具分派 begin/update/commit）
CanvasScene.annotationSelectRequested/Moved/Transformed → annoBridge_.selectAt/moveSelectedBy/applyObbTransform
```

- **OBB 非破坏性变换**：拖拽经 Core `obbPreviewTransform`（逐帧预览）/`applyObbTransform`（释放提交），
  以矩阵 `xform_` 累积、不改子类参数化几何（类型/字形不退化）；属性面板显示 Core 累积的绝对带符号值、改动即生效。
- **烧录仅在导出时发生**：`AnnotationBridge::burnIn(base)` 以当前工作图为底逐个调 Core `rasterize` 合成，
  再送引擎切割——标注随像素被切开（A-0.16：预处理→标注→切割→导出）。切割预览几何不受标注影响。
- **撤销/重做**：复用 Core `AnnotationLayer` 内建分层快照（`revoke/redo`），GUI 不自建。

---

## 7. 全局撤销/重做（G-12）与配置（G-13）

**双历史 + 上下文路由（不改 Core）**：

1. **文档参数态**：`MainWindow` 持 `HistoryManager<EngineConfig> docHistory_`，快照＝`Document::buildEngineConfig`、
   还原＝`Document::applyEngineConfig`（与 G-13 配置同构、复用同一对互逆映射）。栈顶恒为「当前态」：
   `undoDocument` 先 `popToRedo()` 弹出当前态、再还原新栈顶；`undoSize()>1` 才可撤销。
2. **标注**：沿用 Core `AnnotationLayer` 内建分层快照（`annoBridge_.undo/redo`）。

- `Document::changed()` → `scheduleHistoryCapture()` 重启 **500ms 防抖定时器** → `onDocHistoryTimeout()` 压入快照
  （把拖拽/连点合并为一条）；还原期以 `suppressHistory_` 抑制再采集防回环；换图/构造时 `resetDocHistory()` 以当前态为基线。
- **上下文路由**：`Ctrl+Z/Y` → `onUndo/onRedo` → `annotationContextActive()`（标注工具非 SELECT 或有选中标注）
  为真则转发标注撤销，否则走文档参数撤销（消除标注菜单与编辑菜单的快捷键冲突）。
- **预处理不纳入全局撤销**（快照仅参数态、不含像素），靠「重置预处理」回退——此为产品决策，与 §4.7 理想态有意取舍。
- **配置**：`onSaveConfig`/`onLoadConfig` 经 `EngineBridge::saveConfig/loadConfig` 委托 Core；加载走
  `Document::applyEngineConfig` 反向映射（守卫「非 L3 不得重排」「网格单元尺寸为正」不变式），且加载本身入同一撤销栈。

---

## 8. 输出预览渲染（G-11，util 下沉）

导出页缩略图的渲染逻辑已**下沉到 `util/output_preview_renderer`**（无状态自由函数），`MainWindow` 只编排缓存与门控（A-0.1）：

- `util::composeOutputThumbnail(composition, srcPixmap, invScaleX, invScaleY, maxDim=192)`：按已算好的
  `Composition.placements` 用 `QPainter::drawPixmap` 把源区域 blit 到小画布（合并模式按 `dest` 等比落位、
  分离模式拼成触图 cols=ceil(√n)）——**不跑 Core、不落盘**。
- `util::bakeAnnotationsInto(...)`：烧录开启且有标注时，把各标注按 `worldPath` 矢量烘焙到小源图副本
  （内部逐条调 `util::paintAnnotation`，与画布 `AnnotationItem` 同一外观），**只烘焙一次**。
- **按需渲染 + 缓存（A）**：`updateExportPreview` 先缓存 `lastComposition_/lastExportResOk_`，
  再由 `renderExportPreviewFromCache` 仅在导出页为当前选项卡时渲染，否则置 `exportPreviewDirty_`；
  `onRightTabChanged` 切回补渲染。烧录开关/标注变更不重跑 Core，仅用缓存重渲染。
- 小源图 `exportSrcPixmap_` 最长边 ≤ 512（与画布底图分离），坐标经 `exportSrcScale*` 映射，故原图 8000×8000 也只在小图上运算。

---

## 9. 扩展指引

### 9.1 新增一个右侧面板

1. 在 `include/panels/` 加头文件（`<模块>_panel.h`）、`src/panels/` 加实现，类继承 `QWidget`、`namespace idc::gui`。
2. 面板**只发意图信号 / 只读写 `Document`**，不含业务逻辑（A-0.1）；构造用布局管理器，控件加中文 tooltip。
3. 在 `MainWindow` 构造中实例化并加入 `rightTabs_`（`addTab`），在 `connectAll()` 里把面板信号连到编排槽。
4. 若面板状态需持久化，扩展 `Document::buildEngineConfig/applyEngineConfig`（保持与 G-13 配置互逆）。
5. 在 `include/panels/README.md` 与 `src/panels/README.md` 补声明边界/实现说明（§7 工程规范）。

### 9.2 新增一个画布交互

1. 在 `CanvasView`（视图级手势）或对应图元（`SelectionRectItem`/`CellPickerItem`/`AnnotationItem`）里处理事件，
   发语义信号（如 `nudgeSelection`），**不在图元内直接改 Document**。
2. 在 `MainWindow::connectAll()` 连接信号到编排槽，槽内写回 `Document` → 触发 `changed` → `refreshPreview`。
3. 新增图层图元时在 `z_order.h` 追加 z 常量（保持单一定义点），并在 `CanvasScene` 提供 `update*/clear*` 接口。
4. 拖拽类交互注意：拖拽期跳过 `syncPanels` 与对正在拖图元的回设（避免抖动），释放时同步一次。

### 9.3 新增一种标注形状

> ⚠️ 标注几何/命中/光栅化全在 Core，**须先扩展 Core `ShapeType` 与 `ShapeFactory`**（触发 A-0.2，改 Core 前先征得同意）。
> Core 支持后：在 `AnnotationBridge::AnnoTool` 加枚举、`tool_panel.cpp` 的 `kTools[]` 加一行、
> `util::paintAnnotation`/`AnnotationItem` 走 `worldPath()` 自动获得渲染，无需在 GUI 补几何。

### 9.4 调用 Core 的纪律

- 任何切割/几何/排序/极性/导出/JSON/编码逻辑**一律调 Core**，GUI 不重复实现（A-0.3）。
- 发现 Core 缺接口：不在 GUI 补，改为报告 + `// TODO(core): 需要 Core 提供 xxx` 标注（§2.3）。
- L1/L2/L3 必须走同一引擎路径，模式切换只切参数面板与预览呈现（A-0.5）。

---

## 10. 已知限制

| 项 | 现状 | 根因 |
| -- | ---- | ---- |
| 箭头标注 | 未实现 | Core `ShapeType` 无 `ARROW`（`annotation_bridge.h`/`tool_panel.h` 已注明），需先扩展 Core（A-0.2） |
| 扇形标注 | 未实现 | Core `ShapeType` 无 `SECTOR/PIE`，同上 |
| 预处理全局撤销 | 不纳入 `Ctrl+Z` | `docHistory_` 仅快照 `EngineConfig` 参数态、不含像素；靠「重置预处理」回退（产品取舍，README 决策 #10） |
| 网格线吸附 | 未接入 | `selection_rect_item` 吸附目标仅图像边缘 + 中心（NFR-7 要求三选，尚缺网格线） |
| 插值方式下拉 | 未提供 | `engine_bridge` resize 固定双线性；Core 支持 `ResampleMode{NEAREST,BILINEAR}`，图像面板未暴露选择 |
| 8000×8000 ≥30fps | 未正式验证 | 预览已用降采样副本满足设计目标，但性能验收（§10.2）尚未落地实测 |

---

## 11. 构建

```bash
cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE=<vcpkg>/scripts/buildsystems/vcpkg.cmake
cmake --build build --target idc_gui
```

- 仅链接 `Qt6::Widgets Qt6::Gui Qt6::Core` + `image_discropper_core`。
- vcpkg qtbase 附带的 `DB2/PPS/OCI/Libb2` 等可选驱动目标本 GUI 不需要，故不链接。
- Core 的第三方依赖（stb / WebP / nlohmann_json）在 Core 侧以 PRIVATE 接入，GUI 无需感知。
