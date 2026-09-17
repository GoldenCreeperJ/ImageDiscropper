// ============================================================================
// 文件：panels/export_panel.h
// 作用：右侧「导出面板」（guideline §4.6）——输出模式（分离/坍缩/重排）、目标目录或文件、
//       格式、JPEG 质量、命名模板与导出按钮。坍缩不可行时禁用「合并坍缩」项并给出原因。
//       面板把导出参数写回 Document，点击导出发 exportRequested 交 MainWindow 执行（A-0.1）。
// 分块依据：导出参数（写 Document）与导出动作（发信号交上层调 Core）分离；路径可手动键入或经
//           文件对话框选择，均在面板内回灌 Document，实际 runEngine+exportImage 由 EngineBridge 承担。
// 说明：§4.6「导出前预览缩略图」已实现——MainWindow 按 Core Composition 渲染低分辨率输出缩略图，
//       经 setPreviewPixmap 回灌到本面板的 previewLabel_（与文字信息 setPreviewInfo 并存，G-11）。
// ============================================================================
#pragma once

#include <QWidget>

class QComboBox;
class QLineEdit;
class QSpinBox;
class QPushButton;
class QLabel;
class QCheckBox;
class QPixmap;

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
    // 显示输出图像预览缩略图（G-11 / §4.6；由 MainWindow 按 Core Composition 渲染后回灌）。
    // 传入空 pixmap 表示当前无有效输出，标签回退到占位文案。
    void setPreviewPixmap(const QPixmap& pm);
    // 回灌重排上下文（由 MainWindow 依 Core 引擎结果调用）：保留块数 + 网格单元尺寸。
    // 用于「自动 cols/rows（依格数开方）」与画布/单元尺寸不足的内联警告。
    void setRearrangeContext(int keptCount, int cellW, int cellH);
    // 当前重排参数下的警告文本（cols*rows < 保留块数，或 cw/ch < 网格单元尺寸）；无警告返回空串。
    // MainWindow 在导出前调用，非空则弹窗二次确认。
    QString rearrangeWarning() const;

    // 反向同步「导出时烧录标注」复选框（由 MainWindow 依 AnnotationBridge::burnInEnabled 回灌；blockSignals 防回环）。
    void setBurnInChecked(bool on);

signals:
    // 请求导出（MainWindow 负责路径校验与调用 EngineBridge）。
    void exportRequested();
    // 导出时是否烧录标注（写回 AnnotationBridge::setBurnIn；原属图层面板，现归入导出选项）。
    void burnInChanged(bool on);

private slots:
    void onModeChanged(int index);
    void onFormatChanged(int index);
    void onQualityChanged(int value);
    void onNamingEdited();
    void onDirEdited();   // 目录手动键入提交 → 写回 Document。
    void onFileEdited();  // 文件手动键入提交 → 写回 Document。
    void onBrowseDir();
    void onBrowseFile();
    // 合并重排参数（FR-L3.7）：列/行、单元尺寸、填充色。
    void onMergeGridEdited();   // 列数或行数变更。
    void onMergeCellEdited();   // 单元宽或高变更。
    void onPadColorClicked();   // 点击填充色按钮 → 弹色对话框。
    // 重排填充顺序（MergeOrder）：行/列优先、蛇形、倒序——与 L3 选择排序正交。
    void onMergeSortChanged(int index);
    void onMergeSnakeToggled(bool on);
    void onMergeReverseToggled(bool on);
    void onAutoGridClicked();     // 「按格数自动」：依保留块数开方重算 cols/rows 并填入。

private:
    // 依当前输出模式切换目录/文件字段的可用性与可见性。
    void updateFieldVisibility();
    // 依当前格式返回保存对话框的过滤器字符串。
    QString saveFilter() const;
    // 依 Document 的 padColor 更新填充色按钮的背景色块。
    void updatePadColorSwatch();
    // 依 Core 坍缩可行性与 Document 模式刷新各输出模式项可用性（坍缩依可行性、重排仅 L3）。
    void refreshModeItemStates();
    // 依保留块数开方计算 cols/rows 并填入 spinbox（未手动改过时）。
    void applyAutoGrid();
    // 重算并显示重排内联警告（红字）。
    void updateRearrangeWarning();

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
    QLabel* previewLabel_{nullptr};   // 输出图像预览缩略图（G-11 / §4.6）
    QLabel* infoLabel_{nullptr};      // 导出前文字信息（画布尺寸/保留块数）
    QCheckBox* burnIn_{nullptr};      // 导出时烧录标注（把标注合成进像素随切割）
    QPushButton* exportBtn_{nullptr};

    // 合并重排参数控件（仅「合并重排」模式显示；FR-L3.7 / §4.6）。
    QWidget* rearrangeRow_{nullptr};
    QSpinBox* mergeCols_{nullptr};      // 重排列数（0=自动推导）
    QSpinBox* mergeRows_{nullptr};      // 重排行数（0=自动推导）
    QSpinBox* mergeCellW_{nullptr};     // 重排单元宽（0=用保留块原尺寸）
    QSpinBox* mergeCellH_{nullptr};     // 重排单元高（0=原尺寸）
    QPushButton* padColorBtn_{nullptr}; // 填充色按钮（背景显示当前色）

    // 重排填充顺序控件（MergeOrder；仅「合并重排」显示）与自动/警告。
    QComboBox* mergeSortCombo_{nullptr};   // 0=行优先 1=列优先
    QCheckBox* mergeSnake_{nullptr};       // 蛇形填充
    QCheckBox* mergeReverse_{nullptr};     // 倒序填充
    QPushButton* autoGridBtn_{nullptr};    // 「按格数自动」重算 cols/rows
    QLabel* rearrangeWarn_{nullptr};       // 内联警告（红字）
    // 重排上下文（MainWindow 回灌）：保留块数 + 网格单元尺寸（供自动 cols/rows 与警告）。
    int keptCount_{0};
    int cellW_{0};
    int cellH_{0};
    bool mergeGridTouched_{false};         // 用户手动改过 cols/rows → 停止自动填充
    bool collapsible_{true};               // 最近一次 Core 坍缩可行性（供模式项启用/禁用）
};

} // namespace idc::gui
