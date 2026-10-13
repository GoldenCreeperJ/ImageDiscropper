// ============================================================================
// 文件：app/controllers/session_controller.h
// 作用：SessionController——文档会话生命周期编排：打开/新建画布/添加图片图层/清空图层/
//       重置全部 + 图层编辑面板声明态 CRUD 接线。从 MainWindow 拆出（原 onOpen/onNew/
//       onAddImageLayer/onClearAllLayers/onDestroyAll/openImageFromPath 及
//       LayerEditPanel/LayerPanel 相关信号串联）。
// 分块依据：会话生命周期是独立关注点——换底图/换图/图层栈变更一族均「意图 → Document
//       声明态 → 控制器重组预览」，与画布交互/导出/配置域无耦合（CONTRIBUTING.md「分层纪律」）。
// 说明：openImageFromPath 为公共入口（main.cpp 命令行首参 / 桌面与移动共用），
//       MainWindow 保留一行转发以维持公共面不变。
// ============================================================================
#pragma once

#include <QObject>

class QString;
class QWidget;

namespace idc::gui {

class Document;
class AnnotationBridge;
class PreprocessController;
class DocHistory;
class StatusBar;
class ToolPanel;
class LayerEditPanel;
class LayerPanel;

// ---------------------------------------------------------------------------
// SessionController：文档会话生命周期 + 图层编辑面板接线。
// ---------------------------------------------------------------------------
class SessionController : public QObject {
    Q_OBJECT
public:
    explicit SessionController(QObject* parent = nullptr);

    // 依赖注入（setter 模式，与面板一致）。
    void setDocument(Document* doc);
    void setAnnotationBridge(AnnotationBridge* anno);
    void setPreprocessController(PreprocessController* preprocess);
    void setHistory(DocHistory* history);
    void setStatusBar(StatusBar* status);
    void setToolPanel(ToolPanel* toolPanel);       // 添加图层后切图片拾取态 / 清空后回框选
    void setLayerEditPanel(LayerEditPanel* panel);
    void setLayerPanel(LayerPanel* panel);         // 图片图层整体显隐开关接线
    void setDialogParent(QWidget* parent);
    void connectSignals();   // LayerEditPanel/LayerPanel 意图信号 → Document 声明态 CRUD

    // 从给定路径载入图像（供命令行首参自动打开复用；不弹文件对话框）。
    // 与菜单/工具栏「打开」共用同一套载入 → 建预览 → 刷新逻辑。
    void openImageFromPath(const QString& path) const;

public slots:
    void onOpen() const;           // 打开图像对话框 → openImageFromPath。
    void onNew() const;            // 新建画布（**非破坏性**）：换底图、保留图层/标注/色彩预处理→注入 Document。
    void onAddImageLayer() const;  // 图层编辑：从文件添加一个图片图层到当前图层画布（解码→addImageLayer）。
    void onClearAllLayers() const; // 清空全部图片图层（回退单图底图，保留标注/预处理）。
    void onDestroyAll() const;     // 一键重置全部：清图层（回底图）+清标注+重置预处理+清选区，仅保留底图。

private:
    Document* doc_{nullptr};
    AnnotationBridge* anno_{nullptr};
    PreprocessController* preprocess_{nullptr};
    DocHistory* history_{nullptr};
    StatusBar* status_{nullptr};
    ToolPanel* toolPanel_{nullptr};
    LayerEditPanel* layerEditPanel_{nullptr};
    LayerPanel* layerPanel_{nullptr};
    QWidget* dialogParent_{nullptr};
};

} // namespace idc::gui
