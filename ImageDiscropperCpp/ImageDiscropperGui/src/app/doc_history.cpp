// ============================================================================
// 文件：app/doc_history.cpp
// 作用：实现 DocHistory（见同名头文件说明）。
// 分块依据：
//   - 采集（scheduleCapture/captureNow/flush）：防抖定时器 + 抑制守卫；
//   - 回放（undo/redo/recordConfigLoad）：栈操作 + 抑制下 applyEngineConfig；
//   - 启用态（updateEnabled/annotationContextActive）：计算后经 availabilityChanged 回灌。
// ============================================================================
#include "app/doc_history.h"

#include "app/status_bar.h"
#include "model/annotation_bridge.h"
#include "model/document.h"

namespace idc::gui {

DocHistory::DocHistory(QObject* parent)
    : QObject(parent), timer_(this) {
    // 全局撤销/重做：防抖定时器把拖拽/连点的连续参数变更合并为一条历史。
    timer_.setSingleShot(true);
    timer_.setInterval(500); // 500ms 静默即视为一次离散操作结束（拖拽释放/滑块停手）。
    connect(&timer_, &QTimer::timeout, this, &DocHistory::captureNow);
}

void DocHistory::setDocument(Document* doc) { doc_ = doc; }
void DocHistory::setAnnotationBridge(AnnotationBridge* anno) { anno_ = anno; }
void DocHistory::setStatusBar(StatusBar* status) { status_ = status; }

// 参数变更同时驱动防抖历史采集（与 onDocChanged 刷新并行，互不干扰）。
void DocHistory::start() {
    connect(doc_, &Document::changed, this, &DocHistory::scheduleCapture);
}

// 清空历史并以当前参数态为唯一基线（构造/换图后调用；栈顶＝当前态，undoSize==1 时不可撤销）。
void DocHistory::reset() {
    timer_.stop();
    history_.clearAll();
    history_.push(doc_->buildEngineConfig());
    updateEnabled();
}

// 加载配置的中间步骤：先冲刷未落定的防抖变更，保证「栈顶＝载入前当前态」，使加载配置可一步撤销回去。
void DocHistory::recordConfigLoad(const engine::EngineConfig& cfg) {
    flush();
    suppress_ = true;
    doc_->applyEngineConfig(cfg);   // 触发 changed → refreshPreview + syncPanels（还原期间不采集历史）。
    suppress_ = false;
    history_.push(doc_->buildEngineConfig()); // 新态压栈（其下即载入前态），维持「栈顶＝当前态」不变式。
    updateEnabled();
}

// 标注上下文判定：绘制工具激活（非 SELECT）或存在选中标注时，Ctrl+Z/Y 路由到标注撤销/重做。
bool DocHistory::annotationContextActive() const {
    return anno_->currentTool() != AnnoTool::SELECT || anno_->selectedIndex().has_value();
}

// 文档参数撤销：栈顶恒为当前态，需至少两态（基线 + 一次变更）才可撤销；
// 弹出当前态到重做栈后，还原新栈顶（上一态）。还原经 applyEngineConfig 触发 changed 刷新画布/面板。
void DocHistory::undo() {
    if (history_.undoSize() <= 1) { status_->notify(QStringLiteral("没有可撤销的参数变更"), false); return; }
    history_.popToRedo();
    const engine::EngineConfig* prev = history_.top();
    if (!prev) return;
    suppress_ = true;
    doc_->applyEngineConfig(*prev);
    suppress_ = false;
    updateEnabled();
    status_->notify(QStringLiteral("已撤销"), false);
}

// 文档参数重做：从重做栈弹回最近撤销的态并还原（与 undo 对称）。
void DocHistory::redo() {
    if (!history_.canRedo()) { status_->notify(QStringLiteral("没有可重做的参数变更"), false); return; }
    const auto st = history_.popFromRedo();
    if (!st) return;
    suppress_ = true;
    doc_->applyEngineConfig(*st);
    suppress_ = false;
    updateEnabled();
    status_->notify(QStringLiteral("已重做"), false);
}

// 依文档历史深度与标注上下文重算编辑菜单撤销/重做启用态（经 availabilityChanged 回灌 MainWindow）。
void DocHistory::updateEnabled() {
    if (!doc_ || !anno_) return;
    const bool docCanUndo = history_.undoSize() > 1;
    const bool docCanRedo = history_.canRedo();
    const bool annoCtx = annotationContextActive();
    emit availabilityChanged(docCanUndo || annoCtx, docCanRedo || annoCtx);
}

// 参数变更→重启防抖定时器（还原期间被 suppress_ 抑制，避免 undo/redo 自身再入栈）。
void DocHistory::scheduleCapture() {
    if (suppress_) return;
    timer_.start(); // 连续变更（拖拽/连点）只在静默 500ms 后合并为一条历史。
}

// 防抖到点：把当前参数态快照压入历史（栈顶＝当前态），并刷新撤销/重做启用态。
void DocHistory::captureNow() {
    if (suppress_) return;
    history_.push(doc_->buildEngineConfig());
    updateEnabled();
}

// 冲刷未落定的防抖变更（载入配置前调用）。
void DocHistory::flush() {
    if (timer_.isActive()) { timer_.stop(); captureNow(); }
}

} // namespace idc::gui
