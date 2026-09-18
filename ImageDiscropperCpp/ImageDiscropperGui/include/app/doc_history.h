// ============================================================================
// 文件：app/doc_history.h
// 作用：DocHistory——文档参数态撤销/重做控制器：从 MainWindow 拆出 HistoryManager<EngineConfig>
//       快照历史、500ms 防抖采集、还原期抑制与标注上下文判定。
// 分块依据：
//   - 全局撤销的「双历史」之一（标注历史在 AnnotationBridge/Core），独占一块；
//   - 启用态经 availabilityChanged 信号回灌 MainWindow 的 aUndo_/aRedo_（控制器不碰菜单动作）。
// 说明：快照类型为 EngineConfig（与配置加载同构）：capture=buildEngineConfig、restore=applyEngineConfig，
//       栈顶恒为「当前态」；undo 弹栈顶到重做栈后还原新栈顶，redo 反之。仅快照参数态，不含图像/预处理。
// ============================================================================
#pragma once

#include <QObject>
#include <QTimer>

#include "engine/engine.h"
#include "history/history_manager.h"

namespace idc::gui {

class Document;
class AnnotationBridge;
class StatusBar;

// ---------------------------------------------------------------------------
// DocHistory：文档参数态撤销/重做（防抖采集 + 抑制守卫 + 上下文判定 + 启用态信号）。
// ---------------------------------------------------------------------------
class DocHistory : public QObject {
    Q_OBJECT
public:
    explicit DocHistory(QObject* parent = nullptr);

    // 依赖注入（setter 模式，与面板一致）。
    void setDocument(Document* doc);
    void setAnnotationBridge(AnnotationBridge* anno);  // annotationContextActive 判定
    void setStatusBar(StatusBar* status);              // notify 提示
    void start();                                       // 连接 Document::changed → scheduleCapture

    void reset();                       // 清空历史并以当前态为唯一基线（构造/换图后调用）。
    // 加载配置的中间步骤：先冲刷未落定的防抖变更，再抑制采集地应用新配置，最后压栈（等价原 onLoadConfig 中段）。
    void recordConfigLoad(const engine::EngineConfig& cfg);
    // 标注上下文判定：标注工具激活（非 SELECT）或存在选中标注时，Ctrl+Z/Y 路由到标注撤销/重做。
    bool annotationContextActive() const;

public slots:
    void undo();             // 文档参数撤销：弹当前态到重做栈，还原新栈顶（上一态）。
    void redo();             // 文档参数重做：从重做栈弹回并还原。
    void updateEnabled();    // 依文档历史深度与标注上下文重算启用态并 emit availabilityChanged。

signals:
    // 撤销/重做可用性变化（供 MainWindow 刷新编辑菜单/工具栏动作启用态）。
    void availabilityChanged(bool canUndo, bool canRedo);

private slots:
    void scheduleCapture();  // 参数变更→重启防抖定时器（还原期间被 suppress_ 抑制）。

private:
    void captureNow();       // 防抖到点：把当前参数态快照压入历史并刷新启用态。
    void flush();            // 定时器激活则停表并立即 captureNow（载入配置前冲刷未落定变更）。

    Document* doc_{nullptr};
    AnnotationBridge* anno_{nullptr};
    StatusBar* status_{nullptr};

    history::HistoryManager<engine::EngineConfig> history_;
    QTimer timer_;           // 防抖定时器：把拖拽/连点的连续 changed() 合并为一条历史。
    bool suppress_{false};   // 还原（undo/redo/applyEngineConfig）期间抑制历史采集，防回环污染。
};

} // namespace idc::gui
