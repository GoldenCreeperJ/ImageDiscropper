# ImageDiscropperGui — Qt 6 图形前端

本目录是 ImageDiscropper 的 **GUI 层**（可执行目标 `idc_gui`），基于 **Qt 6.x + Widgets + QGraphicsView**。
GUI 是 Core（`image_discropper_core` 静态库）的**纯消费者**：只采集交互、把交互翻译为 Core 参数、
调用 Core、渲染 Core 的结果；**不实现任何切割 / 几何 / 排序 / 极性 / 导出逻辑**（guideline §0 A-0.1）。

> 命名空间统一为 `idc::gui`。第一阶段不修改 Core（A-0.2）。

## 目录结构与职责

采用与 Core / CLI 一致的 **include/src 分离**：头文件与其**声明边界 `README.md`** 在 `include/gui/<模块>/`，
实现与其**实现说明 `README.md`** 在 `src/<模块>/`（与 Core 的 include/src 双侧 README 约定一致）。构建包含根为 `include/gui`，故源码内以 `"canvas/…"`、`"model/…"` 形式互相引用。

| 模块     | 头文件                | 实现 + README                  | 职责                                                          |
| -------- | --------------------- | ------------------------------ | ------------------------------------------------------------- |
| `util`   | `include/gui/util/`   | [src/util/](src/util/README.md)     | `core::Image` ↔ Qt 图像适配、`geometry::Path`/`core::Color` ↔ Qt 绘图类型适配、降采样预览（视图关注点） |
| `model`  | `include/gui/model/`  | [src/model/](src/model/README.md)   | 会话状态单一真相源 `Document`、唯一触碰切割引擎的 `EngineBridge`、唯一驱动 Core 标注的 `AnnotationBridge`   |
| `canvas` | `include/gui/canvas/` | [src/canvas/](src/canvas/README.md) | QGraphicsView/Scene 画布：图层化渲染底图/遮罩/切割线/选区/标注矢量叠加 + 交互 |
| `panels` | `include/gui/panels/` | [src/panels/](src/panels/README.md) | 左侧模式/极性面板与标注工具/图层面板、右侧参数/导出/图像/标注属性面板 |
| `app`    | `include/gui/app/`    | [src/app/](src/app/README.md)       | 主窗口装配与编排、程序入口                                      |

## 数据流（一条主线）

```
用户交互（画布拖拽 / 面板输入 / 快捷键）
        │
        ▼
   Document（状态：模式 / 极性 / 选区 / 导出参数）── 组装 EngineConfig
        │  changed() / imageChanged()
        ▼
   EngineBridge（切割引擎唯一入口）── runEngine / generateCutLines / exportImage
        │  EngineResult{ok,error,kept,composition,collapsible} / CutLineSet
        ▼
   CanvasScene（渲染）── 底图 + 红/绿遮罩 + 橙色选区框（含贯穿切割线延伸、可直接拖边）
        │
        ▼
   MainWindow（状态栏 / 面板提示回显）
```

## 关键设计决策

1. **遮罩渲染法（不做几何布尔）**：`EngineResult.kept` 只给出保留块。画布先铺一层覆盖全图的红色半透明
   “删除底”，再在其上按 `kept` 各片段的区域叠加绿色半透明“保留块”；绿色覆盖处即保留、透出红色处即删除。
   这样 GUI 无需自行计算“删除集”，完全符合 A-0.1。
2. **场景坐标 = 原图像素坐标**：底图用降采样 pixmap 经 `QTransform::fromScale(scaleX, scaleY)` 铺到原图尺寸，
   于是遮罩 / 切割线 / 选区都能直接按原图坐标叠加，避免到处换算。
3. **几何在全分辨率算、只在预览图上画**：`runEngine` 只做区域数学（split/compose 不搬像素），故可对全分辨率
   工作图跑预览，把区域按缩放因子映射到预览 pixmap；导出时对全分辨率图跑，保证无损（NFR-2）。
4. **降采样预览复用 `pixel_ops::resize`**：`makePreview(img, maxDim)` 内部调用 Core 的 resize（NEAREST）生成预览副本，
   无需新增 Core 接口。
5. **单一状态源 + 双桥接**：`Document` 是唯一真相源，切割/预处理经 `EngineBridge`、标注域经 `AnnotationBridge`（各自唯一入口）；面板 / 画布只读写
   `Document`，不各自持有真相，降低耦合。
6. **切割线即选区边（唯一橙色图元、可直接拖边）**：单矩形诱导的四条切割线恰好落在选区矩形的四条边上，是选区边的
   「贯穿全图延伸」。故**不再有任何独立的切割线图元**——由唯一的橙色 `SelectionRectItem` 一并绘制选区框与被标记边的
   贯穿延伸线（同一支橙色画笔），并支持**直接抓取任一条边（含其延伸到图像边界的那段）沿法向拖动**：拖动某边＝移动
   该选区边＝移动对应切割线（四角手柄仍优先做对角缩放、框内拖动整体平移）。哪几条边延伸为切割线由 `CanvasScene`
   依 Core `generateCutLines` 结果下发（`setCutEdges`），GUI 不自算几何（A-0.1 / A-0.11）；`rectChanged` → 写回
   `Document` → Core 重算 → 即时刷新遮罩（NFR-6）。因为线与选区本是同一图元、同一位置来源，物理上不可能再出现
   旧实现的「蓝/橙双线并存、拖拽手柄错位、切割线达上限与选区分离」等双重表示问题。
7. **合并重排仅 L3、自动 cols/rows、双警告**：导出面板的「合并重排」项**仅 L3 启用**（非 L3 置灰，`Document.setMode` 离开 L3 亦自动复位为坍缩）；
   重排的 cols/rows 首次由**保留块数开方**自动填入（cols=ceil(√n)、rows=ceil(n/cols)，用户可再改）；当 cols×rows < 保留块数、
   或单元宽/高 < 网格单元宽/高时，面板内联**红字警告** + 导出前**弹窗二次确认**；填充顺序（行/列优先 + 蛇形 + 倒序）与 L3 选择排序正交，写回 `Document.mergeOrder_`。
8. **标注矢量叠加、仅导出时烧录（G-4/G-5）**：标注层与底图**分离**——每个 Core `Annotation` 对应一个 `AnnotationItem`（`QGraphicsObject`），
   用 `Shape::worldPath()`→`QPainterPath` 矢量叠加绘制（含非破坏性变换，z=`kAnnotation`），**不改底图像素**（A-0.15）；同维度预处理不清标注。
   **烧录仅在导出时发生**：`AnnotationBridge::burnIn(base)` 以**当前工作图**为底逐个调 Core `rasterize` 合成（不复用 `AnnotationLayer::burnIn()` 内部可能陈旧的 `image_`），
   再送引擎切割——标注随像素被切开（A-0.16 预处理→标注→切割→导出）。标注几何/命中/光栅化全在 Core，GUI 只采集手势/翻译参数/渲染（A-0.1）；
   标注撤销/重做复用 Core `AnnotationLayer` 内建 `revoke()/redo()`（分层快照）。
9. **标注 OBB 非破坏性变换（画布手柄 + 面板绝对值）**：选中标注后画布绘**定向包围盒（OBB）** 的 8 缩放手柄 + 1 旋转手柄，
   拖拽经 Core `Shape::obbPreviewTransform`（逐帧矢量预览）/`applyObbTransform`（释放提交）做缩放/拉伸/旋转/翻转——变换以矩阵 `xform_`
   累积、**不改子类参数化几何**（类型/文字字形不退化），渲染/命中走 `worldPath()`；负缩放即翻转（拖手柄越过对边自然产生）。
   属性面板变换区 spin 显示 Core 累积的**绝对带符号值**（`obbScaleX()`/`obbScaleY()`/`obbRotationDeg()`，含负）、**改动即生效无「应用」按钮**
   （每 spin 按单轴换算增量下发，避免取整误差牵连其余轴）。矩阵运算全在 Core，GUI 只采集手柄拖拽、把世界光标经 `worldToLocal` 映回局部算系数（A-0.1）。
10. **全局撤销/重做＝双历史 + 上下文路由（G-12，不改 Core）**：撤销分两条独立历史——①**文档参数态**用 Core `HistoryManager<EngineConfig>`
   快照（`capture=Document::buildEngineConfig`、`restore=Document::applyEngineConfig`，与 G-13 配置同构、复用同一对互逆映射）；②**标注**沿用 Core
   `AnnotationLayer` 内建分层快照（`AnnotationBridge::undo/redo`）。编辑菜单/工具栏的 Ctrl+Z/Y 经 `MainWindow::onUndo/onRedo` **上下文路由**：
   标注工具激活（非 SELECT）或存在选中标注时转发到标注撤销，否则走文档参数撤销——两条历史不再各绑 Ctrl+Z/Y（消除原标注菜单与编辑菜单的快捷键冲突）。
   文档历史用 **500ms 防抖定时器**把拖拽/连点的连续 `changed()` 合并为一条（栈顶恒为「当前态」，`undoSize>1` 才可撤销）；还原期以 `suppressHistory_` 抑制再采集防回环。
   **预处理不纳入全局撤销**（快照仅参数态、不含像素），靠现有「重置预处理」回退——此为产品决策，与 guideline §4.7「预处理可撤销」的理想态有意取舍。
11. **配置文件加载/保存（G-13，复用 Core）**：文件菜单「加载配置/保存配置」经 `EngineBridge::saveConfig/loadConfig` 委托 Core `saveEngineConfig/loadEngineConfig`
   （终稿 §9 schema 的 JSON，GUI 不自实现 JSON、不感知 nlohmann，A-0.1/A-0.3）；加载走 `Document::applyEngineConfig` 反向映射回各状态字段（生成器+tier 还原 L1 形状/L2 子功能，
   并守卫「非 L3 不得重排」「网格单元尺寸为正」不变式），且**加载本身入同一撤销栈**（可一步撤销回载入前）。
12. **导出前输出图像预览（G-11 / §4.6，只渲染不重算）**：导出面板新增 `previewLabel_` 缩略图，由 `util::composeOutputThumbnail`（`util/output_preview_renderer`，无状态自由函数）按
   `refreshPreview` **已算好**的 `EngineResult.composition` 渲染——**不再跑 Core、不落盘**：把 `rebuildPreviewPixmap` 缓存的**小源图**
   `exportSrcPixmap_`（最长边 ≤ 512，与画布底图分离）按 `placements` 的 `source→dest` 用 `QPainter::drawPixmap` blit 到最长边 ≤ 192 的小画布。
   合并模式（坍缩/重排）按真实画布等比缩放、所见即所得；分离模式（无统一画布）拼成触图（cols=ceil(√n)）。源区域坐标经 `exportSrcScale*`
   （working→小图放大系数）映射，故即便原图 8000×8000 也只在小图上运算（**像素少、速度快**）。面板只负责展示（`setPreviewPixmap`，仅缩不放）；**渲染逻辑下沉到 `util/`（离屏预览），MainWindow 只编排缓存与门控（A-0.1）**。
   **B（烧录可见）**：`exportPreviewSource()` 委托 `util::bakeAnnotationsInto`，在「导出时烧录标注」开启且有标注时，把各标注按 `worldPath` 矢量绘到小源图副本（working→小图变换，文字绘字形；实际绘制复用 `util::paintAnnotation`，与画布图元同一外观）——**只烘焙一次**，随后逐块 blit 自动让标注随像素被切割落位，与真实导出一致；细线在大图上按缩略比例自然淡化（忠实）。
   **A（按需渲染）**：`updateExportPreview` 先缓存 `lastComposition_/lastExportResOk_`，再由 `renderExportPreviewFromCache` 仅在「导出」页为当前选项卡时渲染，否则置 `exportPreviewDirty_`；`onRightTabChanged` 切回该页时用缓存补渲染。烧录开关/标注变更（`onBurnInChanged`/`onAnnoBridgeChanged`，仅烧录开启时）**不重跑 Core**，仅用缓存重渲染缩略图（切割几何不受标注影响），保证预览实时。

## 已核对的 Core API（均来自实际头文件，非假设）

- 编排：`engine::runEngine(const core::Image&, const EngineConfig&) -> EngineResult{ok,error,kept,collapsible,composition}`。
- 切割线：`engine::generateCutLines(const CutConfig&, const SourceInfo&) -> CutLineSet`。
- 配置：`EngineConfig{source,preprocess,cut,selectedCells,order,emitParams}`；
  `CutConfig{tier,generator(RECT/HORIZONTAL_LINE/VERTICAL_LINE/MULTI_RECT/GRID),rect,rects,grid,polarity}`。
- 配置存取（G-13）：`engine::saveEngineConfig(path, cfg)` / `engine::loadEngineConfig(path, out&)`（§9 schema JSON，nlohmann 在 Core 侧 PRIVATE）。
- 撤销历史（G-12）：`history::HistoryManager<T>{push,popToRedo,popFromRedo,top,canUndo,canRedo,undoSize,clearAll}`（模板头实现，GUI 以 `T=EngineConfig` 实例化）。
- 导出：`CompositionParams{mode,layout,mergeOrder,cols,rows,cellWidth,cellHeight,padColor,format,naming,quality,keepMetadata}`
  （`mergeOrder` = 重排填充顺序 `MergeOrder{strategy,reverse,snake}`，仅 REARRANGE、与选择排序正交）；
  `Composition{canvasWidth,canvasHeight,placements,...}`；`engine::exportImage(...)`。
- 区域：`RectRegion{left,top,right,bottom,width(),height(),area()}`；`RegionSet{size(),empty(),fragments(),boundingBox()}`；
  `Fragment{region,index,kind,row,col}`。
- I/O 与处理：`engine::readImageFile`（统一解码为 RGBA）；`pixel_ops::resize(...)`；`core::Image{width,height,empty,getPixel,inBounds,...}`；
  `core::Color{r,g,b,a}`。

## 构建

前置：CLion（或命令行）以**仓库根**为 CMake 源，并配置好 vcpkg 工具链（`CMAKE_TOOLCHAIN_FILE`），
且 vcpkg 已安装 `qtbase`（classic 模式，仓库无 `vcpkg.json`/`CMakePresets.json`）。

根 `CMakeLists.txt` 已在 `add_subdirectory(ImageDiscropperCli)` 之后追加 `add_subdirectory(ImageDiscropperGui)`。

```bash
cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE=<vcpkg>/scripts/buildsystems/vcpkg.cmake
cmake --build build --target idc_gui
```

- 仅链接 `Qt6::Widgets Qt6::Gui Qt6::Core` + `image_discropper_core`。
- vcpkg qtbase 附带的 `DB2/PPS/OCI/Libb2` 等**可选驱动目标本 GUI 不需要，故不链接**（否则要求额外库、易失败）。
- Core 的第三方依赖（stb / WebP / nlohmann_json）在 Core 侧以 PRIVATE 接入，GUI 无需感知。

## 第一阶段覆盖范围与后续阶段

**第一阶段（本目录当前实现，可运行核心闭环）**：打开图像 → 降采样预览 → 缩放/平移 →
L1（矩形/横带/竖带）与 L2（十字/横线/竖线）切割线与极性遮罩 → 选区拖拽/微调/吸附 + 直接拖动选区边＝移动切割线（切割线即选区边的贯穿延伸，
唯一橙色图元）→ 分离/坍缩/基础重排导出。覆盖验收项 G-1、G-2、G-6、G-7（单矩形）、G-8、G-11、G-14、G-10（选区拖拽/微调/吸附）。

后续阶段（逐步细化）：

- **第二阶段 L2多矩形 + L3 网格（已完成）**：网格线层、单元选择（单击/框选/全选/反选）、排序面板、自定义序拖拽、重排合并、
  多矩形并集剔除、「转为网格模式编辑」入口。覆盖 G-7（多矩形）/G-9/G-15。
- **第三阶段 预处理（已完成）**：图像处理面板（旋转/翻转/缩放/尺寸/黑白/反色/色道分离/重置）
  已接入（覆盖 G-3）：面板发意图信号 → MainWindow 经 `EngineBridge` 调 Core `pixel_ops::*` 变换工作图 → 写回 `Document`
  （原图始终保留供「重置预处理」；维度变化时自动清除失效选区）。撤销重做（G-12）与配置加载/保存（G-13）已于第五阶段补齐。
  §4.5.4「颜色选取（取色器）」与标注属性强相关，并入第四阶段。
- **第四阶段 标注图层 + 图层管理（已完成）**：`AnnotationBridge`（标注域桥）驱动 Core `geometry`+`annotation`，画布 `AnnotationItem` 矢量叠加渲染（不改底图），
  左侧标注工具面板（选择/矩形/正方/菱形/圆/椭圆/圆角矩/四种三角/直线/多线段/文字/画笔）与图层面板（底图/遮罩/网格线/切割线/选取边框/标注显隐 + 烧录开关），
  右侧标注属性页（颜色/粗细/填充/文字/字号）；支持添加/选择/移动/编辑/删除、标注撤销/重做（Core 分层快照）、导出可选烧录（烧录后随像素被切开）。
  覆盖 G-4/G-5。**已知限制**：本轮无箭头（Core `ShapeType` 无 ARROW）。（移动标注经 Core `Shape::translateWorld` 世界系平移，保留具体类型，不再退化为 PATH。）
- **第五阶段 全局撤销重做 + 配置 + 文档与性能（进行中）**：已补齐 G-12（双历史 + 上下文路由的全局撤销/重做，编辑菜单+工具栏 Ctrl+Z/Y）、
  G-13（文件菜单加载/保存配置，复用 Core `save/loadEngineConfig`）与 G-11 的**导出前输出图像预览缩略图**（导出面板，详见「关键设计决策」#10/#11/#12）。
  剩余：完整使用说明与设计说明、8000×8000 ≥30fps 性能验证、逐项对照 §10 验收。
