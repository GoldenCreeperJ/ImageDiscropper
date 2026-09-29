# include/model — 状态源与 Core 桥接对外声明

**目录作用**：GUI **模型层**的公共头，声明会话状态单一真相源 `Document`、唯一触碰切割引擎的 `EngineBridge`，
以及唯一持有并驱动 Core `annotation::AnnotationLayer` 的标注域桥 `AnnotationBridge`。
本目录只放**类声明边界**；实现与详尽字段 / 信号说明见 [src/model/](../../src/model/README.md)。

**分块依据**：状态与 Core 调用**分离**——`Document` 只管状态（模式 / 极性 / 选区 / L2 多矩形 / L3 网格与选择集 /
导出与重排参数）并组装 `EngineConfig`；`EngineBridge` 只管调 Core API。二者分离保证「GUI 不含切割 / 几何 / 导出逻辑」
，其余 GUI 代码只经 `EngineBridge` 间接使用引擎，不直接 include 引擎实现细节。

| 文件                    | 声明                                         | 职责                                                                                                                                                                                                                                                                                                                                                                            |
|-----------------------|--------------------------------------------|-------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------|
| `document.h`          | `Document`（`QObject`）                      | 会话状态单一真相源：持有原图 `original_` / 声明式预处理队列 `steps_`（`PreprocessPipeline`）/ 工作图 `working_` / 模式 / 极性 / 选区 / L2 多矩形 / L3 网格与选择集 / 导出与重排参数；`buildEngineConfig`（运行配置，preprocess 恒空）/ `buildSerializableConfig`（序列化配置，携带完整 `steps_`、source 取原图尺寸）组装、`applyEngineConfig` 反向还原；`geomSig`（`steps_.geometryOutput`）供尺寸门、`hasPreprocess`（`!steps_.empty`），变更发 `changed()` / `imageChanged()` |
| `engine_bridge.h`     | `EngineBridge`（无状态、可拷贝）                    | GUI 中**唯一**触碰切割引擎的类：`loadImage` / `runPreviewGeometry`（仅尺寸跑几何、忽略 preprocess）/ `cutLines` / `buildGrid` / `exportResult` / `saveConfig` / `loadConfig`（后两者带可选 `AnnotationLayer*` 参，随 SPEC §7 `annotations` 段往返）委托 `engine::*`；声明式队列物化 `applyPipeline`（委托 Core `PreprocessPipeline::apply`）                                                                                     |
| `annotation_bridge.h` | `AnnoTool` / `AnnotationBridge`（`QObject`） | 标注域桥：把画布手势/面板意图翻译为对 Core `buildShape`/`addAnnotation`/`hitTest`/`rasterize` 的调用（绘制/选择/移动/删除/**层序上抬/下移/置顶/置底（`raiseSelected`/`lowerSelected`/`bringSelectedToFront`/`sendSelectedToBack` → Core `moveAnnotation`）**/撤销重做/图层可见/导出烧录），另提供 `layer()`（只读引用供保存序列化）与 `replaceAnnotations()`（配置还原整体替换），发 `changed()`/`selectionChanged()`/`toolChanged()` 驱动重绘                        |

> 命名空间统一 `idc::gui`。数据流：交互 → `Document` → `EngineBridge`（runEngine / generateCutLines / exportImage）→ `CanvasScene` 渲染。
