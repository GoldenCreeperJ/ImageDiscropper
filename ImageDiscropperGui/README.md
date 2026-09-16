# ImageDiscropperGui — Qt 6 图形前端

本目录是 ImageDiscropper 的 **GUI 层**（可执行目标 `idc_gui`），基于 **Qt 6.x + Widgets + QGraphicsView**。
GUI 是 Core（`image_discropper_core` 静态库）的**纯消费者**：只采集交互、把交互翻译为 Core 参数、
调用 Core、渲染 Core 的结果；**不实现任何切割 / 几何 / 排序 / 极性 / 导出逻辑**（guideline §0 A-0.1）。

> 命名空间统一为 `idc::gui`。第一阶段不修改 Core（A-0.2）。

## 目录结构与职责

采用与 Core / CLI 一致的 **include/src 分离**：头文件在 `include/gui/<模块>/`（对外可见的类声明边界），
实现与各模块 `README.md` 在 `src/<模块>/`。构建包含根为 `include/gui`，故源码内以 `"canvas/…"`、`"model/…"` 形式互相引用。

| 模块     | 头文件                | 实现 + README                  | 职责                                                          |
| -------- | --------------------- | ------------------------------ | ------------------------------------------------------------- |
| `util`   | `include/gui/util/`   | [src/util/](src/util/README.md)     | `core::Image` ↔ Qt 图像类型适配、降采样预览（视图关注点，非引擎逻辑） |
| `model`  | `include/gui/model/`  | [src/model/](src/model/README.md)   | 会话状态单一真相源 `Document`、唯一触碰 Core 的 `EngineBridge`   |
| `canvas` | `include/gui/canvas/` | [src/canvas/](src/canvas/README.md) | QGraphicsView/Scene 画布：图层化渲染底图/遮罩/切割线/选区 + 交互 |
| `panels` | `include/gui/panels/` | [src/panels/](src/panels/README.md) | 左侧模式/极性面板、右侧参数面板、导出面板                       |
| `app`    | `include/gui/app/`    | [src/app/](src/app/README.md)       | 主窗口装配与编排、程序入口                                      |

## 数据流（一条主线）

```
用户交互（画布拖拽 / 面板输入 / 快捷键）
        │
        ▼
   Document（状态：模式 / 极性 / 选区 / 导出参数）── 组装 EngineConfig
        │  changed() / imageChanged()
        ▼
   EngineBridge（唯一触 Core）── runEngine / generateCutLines / exportImage
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
4. **降采样预览复用 `processing::resize`**：`makePreview(img, maxDim)` 内部调用 Core 的 resize（NEAREST）生成预览副本，
   无需新增 Core 接口。
5. **单一状态源 + 单一桥接**：`Document` 是唯一真相源，`EngineBridge` 是唯一触碰 Core 的类；面板 / 画布只读写
   `Document`，不各自持有真相，降低耦合。
6. **切割线即选区边（唯一橙色图元、可直接拖边）**：单矩形诱导的四条切割线恰好落在选区矩形的四条边上，是选区边的
   「贯穿全图延伸」。故**不再有任何独立的切割线图元**——由唯一的橙色 `SelectionRectItem` 一并绘制选区框与被标记边的
   贯穿延伸线（同一支橙色画笔），并支持**直接抓取任一条边（含其延伸到图像边界的那段）沿法向拖动**：拖动某边＝移动
   该选区边＝移动对应切割线（四角手柄仍优先做对角缩放、框内拖动整体平移）。哪几条边延伸为切割线由 `CanvasScene`
   依 Core `generateCutLines` 结果下发（`setCutEdges`），GUI 不自算几何（A-0.1 / A-0.11）；`rectChanged` → 写回
   `Document` → Core 重算 → 即时刷新遮罩（NFR-6）。因为线与选区本是同一图元、同一位置来源，物理上不可能再出现
   旧实现的「蓝/橙双线并存、拖拽手柄错位、切割线达上限与选区分离」等双重表示问题。

## 已核对的 Core API（均来自实际头文件，非假设）

- 编排：`engine::runEngine(const core::Image&, const EngineConfig&) -> EngineResult{ok,error,kept,collapsible,composition}`。
- 切割线：`engine::generateCutLines(const CutConfig&, const SourceInfo&) -> CutLineSet`。
- 配置：`EngineConfig{source,preprocess,cut,selectedCells,order,emitParams}`；
  `CutConfig{tier,generator(RECT/HORIZONTAL_LINE/VERTICAL_LINE/MULTI_RECT/GRID),rect,rects,grid,polarity}`。
- 导出：`CompositionParams{mode,layout,cols,rows,cellWidth,cellHeight,padColor,format,naming,quality,keepMetadata}`；
  `Composition{canvasWidth,canvasHeight,placements,...}`；`engine::exportImage(...)`。
- 区域：`RectRegion{left,top,right,bottom,width(),height(),area()}`；`RegionSet{size(),empty(),fragments(),boundingBox()}`；
  `Fragment{region,index,kind,row,col}`。
- I/O 与处理：`engine::readImageFile`（统一解码为 RGBA）；`processing::resize(...)`；`core::Image{width,height,empty,getPixel,inBounds,...}`；
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

- **第二阶段 L3 网格**：网格线层、单元选择（单击/框选/全选/反选）、排序面板、自定义序拖拽、重排合并、
  多矩形并集剔除、「转为网格模式编辑」入口。
- **第三阶段 预处理 + 撤销重做 + 配置**：图像处理面板（旋转/翻转/缩放/黑白/反色/色道/取色）、
  `HistoryManager` 撤销重做、配置加载/保存。
- **第四阶段 标注图层**：全标注类型经 Core `geometry` + `annotation`，图层面板与属性面板，切割时标注随像素切开。
- **第五阶段 文档与性能**：完整使用说明与设计说明、8000×8000 ≥30fps 性能验证、逐项对照 §10 验收。

### 待在 Core 侧补齐的能力缺口（后续阶段，require.md 优先）

- **马赛克**：require FR-1.4 要求“马赛克”，但 `processing` 无对应函数 → 第三阶段在 Core `processing` 新增 `mosaic`。
- **任意角度旋转**：require FR-1.1 要求“任意角度 + 插值 + 背景填充”，但 `processing::rotate` 仅支持 90 的整数倍
  → 第三阶段在 Core 扩展任意角度旋转。

以上两处 GUI 不自行补实现（A-0.1/A-0.3），待 Core 补齐后再接面板。
