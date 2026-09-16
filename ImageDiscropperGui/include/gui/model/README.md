# include/gui/model — 状态源与 Core 桥接对外声明

**目录作用**：GUI **模型层**的公共头，声明会话状态单一真相源 `Document` 与唯一触碰 Core 的 `EngineBridge`。
本目录只放**类声明边界**；实现与详尽字段 / 信号说明见 [src/model/](../../../src/model/README.md)。

**分块依据**：状态与 Core 调用**分离**——`Document` 只管状态（模式 / 极性 / 选区 / L2 多矩形 / L3 网格与选择集 /
导出与重排参数）并组装 `EngineConfig`；`EngineBridge` 只管调 Core API。二者分离保证「GUI 不含切割 / 几何 / 导出逻辑」
（A-0.1 / A-0.3），其余 GUI 代码只经 `EngineBridge` 间接使用引擎，不直接 include 引擎实现细节。

| 文件 | 声明 | 职责 |
|---|---|---|
| `document.h` | `Document`（`QObject`） | 会话状态单一真相源：持有图像 / 模式 / 极性 / 选区 / L2 多矩形 / L3 网格与选择集 / 导出与重排参数，`buildEngineConfig`，变更发 `changed()` / `imageChanged()` |
| `engine_bridge.h` | `EngineBridge`（无状态、可拷贝） | GUI 中**唯一**触碰 Core 的类：`loadImage` / `runPreview` / `cutLines` / `buildGrid` / `exportResult` 委托 `engine::*` |

> 命名空间统一 `idc::gui`。数据流：交互 → `Document` → `EngineBridge`（runEngine / generateCutLines / exportImage）→ `CanvasScene` 渲染。
