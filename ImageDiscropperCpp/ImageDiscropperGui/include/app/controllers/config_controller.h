// ============================================================================
// 文件：app/controllers/config_controller.h
// 作用：ConfigController——配置存取编排（FR-L3.8）：保存（§7 schema JSON，含标注与图层段）/
//       加载（底图像素重生 + 资产解码 + 历史压栈可撤销 + 预处理队列重放 + 标注还原）。
// 分块依据：配置存取是独立关注点（依赖 Document/AnnotationBridge/PreprocessController/
//       DocHistory/EngineBridge），与装配壳无耦合（CONTRIBUTING.md「分层纪律」）。
// ============================================================================
#pragma once

#include <QObject>

class QWidget;

namespace idc::gui {

class Document;
class AnnotationBridge;
class PreprocessController;
class DocHistory;
class StatusBar;

// ---------------------------------------------------------------------------
// ConfigController：配置保存/加载编排。
// ---------------------------------------------------------------------------
class ConfigController : public QObject {
    Q_OBJECT
public:
    explicit ConfigController(QObject* parent = nullptr);

    // 依赖注入（setter 模式，与面板一致）。
    void setDocument(Document* doc);
    void setAnnotationBridge(AnnotationBridge* anno);
    void setPreprocessController(PreprocessController* preprocess);
    void setHistory(DocHistory* history);        // recordConfigLoad（载入压栈可撤销）
    void setStatusBar(StatusBar* status);
    void setDialogParent(QWidget* parent);       // 配置对话框父窗口（桌面 QFileDialog / 移动端 QInputDialog 列表选择框）

public slots:
    void onSaveConfig();   // 保存配置：当前作业配置写为 §7 schema JSON（委托 EngineBridge → Core）。
    void onLoadConfig();   // 加载配置：读 JSON → 底图重生 → DocHistory::recordConfigLoad → 队列重放 → 标注还原。

private:
    Document* doc_{nullptr};
    AnnotationBridge* anno_{nullptr};
    PreprocessController* preprocess_{nullptr};
    DocHistory* history_{nullptr};
    StatusBar* status_{nullptr};
    QWidget* dialogParent_{nullptr};
};

} // namespace idc::gui
