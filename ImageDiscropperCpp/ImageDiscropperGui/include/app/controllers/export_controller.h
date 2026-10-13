// ============================================================================
// 文件：app/controllers/export_controller.h
// 作用：ExportController——导出编排：烧录源组装（图片图层/标注）→ 路径解析（桌面对话框 /
//       Android 相册发布）→ EngineBridge 导出 → 结果提示。从 MainWindow 拆出（原 onExport，
//       含 Q_OS_ANDROID 发布分支；平台发布子编排在 mobile/android_export_publish）。
// 分块依据：导出是独立关注点（依赖 Document/AnnotationBridge/PreprocessController/ExportPanel），
//       与装配壳无耦合（CONTRIBUTING.md「分层纪律」）。
// ============================================================================
#pragma once

#include <QObject>

class QWidget;

namespace idc::gui {

class Document;
class AnnotationBridge;
class PreprocessController;
class ExportPanel;
class StatusBar;

// ---------------------------------------------------------------------------
// ExportController：导出编排（含 Android 相册发布分支）。
// ---------------------------------------------------------------------------
class ExportController : public QObject {
    Q_OBJECT
public:
    explicit ExportController(QObject* parent = nullptr);

    // 依赖注入（setter 模式，与面板一致）。
    void setDocument(Document* doc);
    void setAnnotationBridge(AnnotationBridge* anno);
    void setPreprocessController(PreprocessController* preprocess);
    void setExportPanel(ExportPanel* panel);
    void setStatusBar(StatusBar* status);
    void setDialogParent(QWidget* parent);   // 重排警告确认框父窗口（MainWindow）
    void connectSignals();                   // exportPanel_->exportRequested → onExport

public slots:
    void onExport();                         // 导出（委托 EngineBridge → Core runEngine + exportImage）。

private:
    Document* doc_{nullptr};
    AnnotationBridge* anno_{nullptr};
    PreprocessController* preprocess_{nullptr};
    ExportPanel* panel_{nullptr};
    StatusBar* status_{nullptr};
    QWidget* dialogParent_{nullptr};
};

} // namespace idc::gui
