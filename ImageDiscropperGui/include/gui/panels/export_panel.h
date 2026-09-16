// ============================================================================
// 文件：panels/export_panel.h
// 作用：右侧「导出面板」（guideline §4.6）——输出模式（分离/坍缩/重排）、目标目录或文件、
//       格式、JPEG 质量、命名模板与导出按钮。坍缩不可行时禁用「合并坍缩」项并给出原因。
//       面板把导出参数写回 Document，点击导出发 exportRequested 交 MainWindow 执行（A-0.1）。
// 分块依据：导出参数（写 Document）与导出动作（发信号交上层调 Core）分离；路径可手动键入或经
//           文件对话框选择，均在面板内回灌 Document，实际 runEngine+exportImage 由 EngineBridge 承担。
// 说明：§4.6「导出前预览缩略图」在本阶段以文字信息（画布尺寸/保留块数）替代，缩略图列入后续阶段。
// ============================================================================
#pragma once

#include <QWidget>

class QComboBox;
class QLineEdit;
class QSpinBox;
class QPushButton;
class QLabel;

namespace idc::gui {

class Document;

// ---------------------------------------------------------------------------
// ExportPanel：导出参数与导出触发。
// ---------------------------------------------------------------------------
class ExportPanel : public QWidget {
    Q_OBJECT
public:
    explicit ExportPanel(QWidget* parent = nullptr);

    void setDocument(Document* doc);
    // 从 Document 反向同步控件（模式/格式/质量/命名/路径）与字段可见性。
    void syncFromDocument();
    // 依 Core isCollapsible 结果启用/禁用「合并坍缩」项（不可行时切到重排）。
    void setCollapsible(bool collapsible);
    // 更新导出前信息文字（画布尺寸、保留块数等；由 MainWindow 回灌）。
    void setPreviewInfo(const QString& text);

signals:
    // 请求导出（MainWindow 负责路径校验与调用 EngineBridge）。
    void exportRequested();

private slots:
    void onModeChanged(int index);
    void onFormatChanged(int index);
    void onQualityChanged(int value);
    void onNamingEdited();
    void onDirEdited();   // 目录手动键入提交 → 写回 Document。
    void onFileEdited();  // 文件手动键入提交 → 写回 Document。
    void onBrowseDir();
    void onBrowseFile();

private:
    // 依当前输出模式切换目录/文件字段的可用性与可见性。
    void updateFieldVisibility();
    // 依当前格式返回保存对话框的过滤器字符串。
    QString saveFilter() const;

    Document* doc_{nullptr};

    QComboBox* modeCombo_{nullptr};   // 0=分离导出 1=合并坍缩 2=合并重排
    QWidget* dirRow_{nullptr};        // 分离导出目标目录整行（随模式显隐）
    QLineEdit* dirEdit_{nullptr};     // 分离导出目标目录
    QPushButton* dirBtn_{nullptr};
    QWidget* fileRow_{nullptr};       // 合并导出目标文件整行（随模式显隐）
    QLineEdit* fileEdit_{nullptr};    // 合并导出目标文件
    QPushButton* fileBtn_{nullptr};
    QComboBox* formatCombo_{nullptr}; // PNG/JPEG/WebP/BMP
    QSpinBox* quality_{nullptr};      // 有损格式质量
    QLabel* qualityLabel_{nullptr};
    QWidget* namingRow_{nullptr};     // 命名模板整行（仅分离导出显示）
    QLineEdit* naming_{nullptr};      // 分离命名模板
    QLabel* namingLabel_{nullptr};
    QLabel* infoLabel_{nullptr};      // 导出前信息（替代缩略图）
    QPushButton* exportBtn_{nullptr};
};

} // namespace idc::gui
