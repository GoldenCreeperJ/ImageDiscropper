# examples

**目录作用**：演示程序，展示如何在实际项目中使用 ImageDiscropperCore 库。
每个示例都是独立的可执行入口，覆盖不同模块的典型用法。

**分块依据**：每个示例聚焦一个主题，便于按需阅读与运行。

| 文件              | 职责                                                                                        |
|-----------------|-------------------------------------------------------------------------------------------|
| `demo_main.cpp` | 综合演示：构造图像 → 绘制标注（合成 / 撤销重做 / 命中检测）→ 预处理流水线 → 几何变换 → **真正跑通 Grid-Selection-Emit 引擎并导出到磁盘** |

> 引擎实战段（demo §9）真正调用 `runEngine` + `exportImage`，产物写入 `./demo_output/`：
> - **L2 十字剔除 → 坍缩合并**：`l2_collapsed.png`（保留四角，画布 (W−Δx)×(H−Δy)）；
> - **同作业分离导出 → 文件夹**：`l2_corners/`（四角 4 张 PNG，目录不存在自动创建）；
> - **L3 网格 → 选四角单元 → 重排合并**：`l3_rearranged.png`；
> - **配置 JSON 存取**：`l2_config.json`（`saveEngineConfig` → `loadEngineConfig` 回读校验）。

## 构建与运行

```bash
cmake -S .. -B ../build
cmake --build ../build --target demo
../build/demo                # 运行后查看 ./demo_output/ 下的导出产物
```
