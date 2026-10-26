// ============================================================================
// 文件：panels/export_panel.h
// 作用：右侧「导出面板」——输出模式（分离/坍缩/重排）、目标目录或文件、
//       格式、JPEG 质量、命名模板与导出按钮。坍缩不可行时禁用「合并坍缩」项并给出原因。
//       面板把导出参数写回 Document，点击导出发 exportRequested 交 ExportController 执行（CONTRIBUTING.md「分层纪律」）。
// 分块依据：导出参数（写 Document）与导出动作（发信号交上层调 Core）分离；路径可手动键入或经
//           文件对话框选择，均在面板内回灌 Document，实际 runEngine+exportImage 由 EngineBridge 承担。
// 说明：「导出前预览缩略图」已实现——PreviewController 按 Core Composition 渲染低分辨率输出缩略图，
//       经 setPreviewPixmap 回灌到本面板的 previewLabel_（与文字信息 setPreviewInfo 并存）。
// ============================================================================
#pragma once

#include <QWidget>

#include "i18n/retranslate.h"

class QComboBox;
class QLineEdit;
class QSpinBox;
class QPushButton;
class QLabel;
class QCheckBox;
class QPixmap;

namespace idc::gui {

class Document;
class CoordSpinBox;   // 坐标输入框（整数像素 + "50%" 百分比输入，见 include/util/coord_spin.h）

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
    // 最近一次 Core 坍缩可行性（供菜单「导出设置·坍缩」镜像同一置灰规则）。
    bool collapsible() const { return collapsible_; }
    // 「合并重排」项可用性（置灰口径的唯一真相源）：仅 L3 且保留块长宽全等（Core 重排准入门限，
    // SPEC §4.3）。菜单「导出设置·重排」与刷新项共用此判据。
    bool rearrangeAvailable() const;
    // 更新导出前信息文字（画布尺寸、保留块数等；由 PreviewController 回灌）。
    void setPreviewInfo(const QString& text) const;
    // 显示输出图像预览缩略图（由 PreviewController 按 Core Composition 渲染后回灌）。
    // 传入空 pixmap 表示当前无有效输出，标签回退到占位文案。
    void setPreviewPixmap(const QPixmap& pm) const;
    // 回灌重排上下文（由 PreviewController 依 Core 引擎结果调用）：保留块数 + 网格单元尺寸 +
    // 保留块长宽是否全等（Core 重排准入门限）。用于「自动 cols/rows（依格数开方）」、
    // 画布/单元尺寸不足的内联警告，以及块尺寸不全等时从重排回退到坍缩/分离。
    void setRearrangeContext(int keptCount, int cellW, int cellH, bool uniformBlocks = true);
    // 当前重排参数下的警告文本（cols*rows < 保留块数，或 cw/ch < 网格单元尺寸）；无警告返回空串。
    // ExportController 在导出前调用，非空则弹窗二次确认。
    QString rearrangeWarning() const;

    // 反向同步「导出时烧录标注」复选框（由 AnnotationCoordinator 依 AnnotationBridge::burnInEnabled 回灌；blockSignals 防回环）。
    void setBurnInChecked(bool on) const;
    // 反向同步「导出时烧录图片图层」复选框（由 MainWindow 依 Document::layersBurnIn 回灌；blockSignals 防回环）。
    void setLayersBurnInChecked(bool on) const;
    // 视同用户勾选「导出时烧录标注」：仅在当前未勾时 setChecked(true)，触发 toggled→burnInChanged
    // 走既有信号链（写回真相源 + 同步菜单/预览）；已勾则不动。供载入配置后按存在性自动开烧录。
    void enableBurnIn() const;
    // 视同用户勾选「导出时烧录图片图层」：语义同 enableBurnIn，走 layersBurnInChanged 链。
    void enableLayersBurnIn() const;
    // 外部（文件菜单「导出设置」）选择输出方式/格式：经 combo 走与用户点击同一
    // onModeChanged/onFormatChanged 路径（含重排 auto-grid 与警告刷新）；面板为唯一真相源。
    void selectExportMode(int index) const;
    void selectFormat(int index) const;

    // 语言热切换重放：重跑本面板登记的静态文案闭包（行标签/下拉项/按钮/提示）。
    // 动态回显（导出前信息/预览占位/重排警告）由调用方随后的 syncPanels/refreshPreview 重派生。
    void retranslate() const { retranslator_.run(); }

signals:
    // 请求导出（ExportController 负责路径校验与调用 EngineBridge）。
    void exportRequested();
    // 导出时是否烧录标注（写回 AnnotationBridge::setBurnIn）。
    void burnInChanged(bool on);
    // 导出时是否烧录图片图层（写回 Document::setLayersBurnIn；仅影响导出、与图层面板「显示」仅预览正交）。
    void layersBurnInChanged(bool on);
    // 坍缩可行性变化（预览刷新回灌）：供菜单「导出设置·坍缩」镜像置灰。
    void collapsibleChanged(bool collapsible);
    // 重排项可用性变化（预览刷新回灌）：供菜单「导出设置·重排」镜像置灰。
    void rearrangeAvailableChanged(bool available);

private slots:
    void onModeChanged(int index);
    void onFormatChanged(int index) const;
    void onQualityChanged(int value) const;
    void onNamingEdited() const;
    void onDirEdited() const;   // 目录手动键入提交 → 写回 Document。
    void onFileEdited() const;  // 文件手动键入提交 → 写回 Document。
    void onBrowseDir();
    void onBrowseFile();
    // 合并重排参数（FR-L3.7）：列/行、单元尺寸、填充色。
    void onMergeGridEdited();   // 列数或行数变更。
    void onMergeCellEdited() const;   // 单元宽或高变更。
    void onPadColorClicked() const;   // 点击填充色按钮 → 弹色对话框。
    // 重排填充顺序（MergeOrder）：行/列优先、蛇形、倒序——与 L3 选择排序正交。
    void onMergeSortChanged(int index) const;
    void onMergeSnakeToggled(bool on) const;
    void onMergeReverseToggled(bool on) const;
    void onAutoGridClicked();     // 「按格数自动」：依保留块数开方重算 cols/rows 并填入。

private:
    // 依当前输出模式切换目录/文件字段的可用性与可见性。
    void updateFieldVisibility() const;
    // 依当前格式返回保存对话框的过滤器字符串。
    QString saveFilter() const;
    // 依 Document 的 padColor 更新填充色按钮的背景色块。
    void updatePadColorSwatch() const;
    // 依 Core 坍缩可行性与 Document 模式刷新各输出模式项可用性（坍缩依可行性、重排依 rearrangeAvailable）。
    void refreshModeItemStates() const;
    // 依保留块数开方计算 cols/rows 并填入 spinbox（未手动改过时）。
    void applyAutoGrid();
    // 重算并显示重排内联警告（红字）。
    void updateRearrangeWarning() const;

    Document* doc_{nullptr};

    QComboBox* modeCombo_{nullptr};   // 0=分离导出 1=合并坍缩 2=合并重排
    QWidget* dirRow_{nullptr};        // 分离导出目标目录整行（随模式显隐）
    QLineEdit* dirEdit_{nullptr};     // 分离导出目标目录
    QPushButton* dirBtn_{nullptr};
    QWidget* fileRow_{nullptr};       // 合并导出目标文件整行（随模式显隐）
    QLineEdit* fileEdit_{nullptr};    // 合并导出目标文件
    QPushButton* fileBtn_{nullptr};
    QWidget* padRow_{nullptr};        // 填充色整行（恒可见；合并底色 / Pad 补白 / 有损压平）
    QPushButton* padColorBtn_{nullptr}; // 填充色按钮（背景显示当前色）
    QComboBox* formatCombo_{nullptr}; // PNG/JPEG/WebP/BMP
    QSpinBox* quality_{nullptr};      // 有损格式质量
    QLabel* qualityLabel_{nullptr};
    QWidget* namingRow_{nullptr};     // 命名模板整行（仅分离导出显示）
    QLineEdit* naming_{nullptr};      // 分离命名模板
    QLabel* namingLabel_{nullptr};
    QLabel* previewLabel_{nullptr};   // 输出图像预览缩略图（导出前预览）
    QLabel* infoLabel_{nullptr};      // 导出前文字信息（画布尺寸/保留块数）
    QCheckBox* burnIn_{nullptr};      // 导出时烧录标注（把标注合成进像素随切割）
    QCheckBox* layersBurnIn_{nullptr}; // 导出时烧录图片图层（把叠加图片图层并入像素随切割；对标 burnIn_）
    QPushButton* exportBtn_{nullptr};

    // 合并重排参数控件（仅「合并重排」模式显示；FR-L3.7）。
    QWidget* rearrangeRow_{nullptr};
    QSpinBox* mergeCols_{nullptr};      // 重排列数（0=自动推导）
    QSpinBox* mergeRows_{nullptr};      // 重排行数（0=自动推导）
    CoordSpinBox* mergeCellW_{nullptr}; // 重排单元宽（0=用保留块原尺寸；另接受百分比，基准画布宽）
    CoordSpinBox* mergeCellH_{nullptr}; // 重排单元高（0=原尺寸；另接受百分比，基准画布高）

    // 重排填充顺序控件（MergeOrder；仅「合并重排」显示）与自动/警告。
    QComboBox* mergeSortCombo_{nullptr};   // 0=行优先 1=列优先
    QCheckBox* mergeSnake_{nullptr};       // 蛇形填充
    QCheckBox* mergeReverse_{nullptr};     // 倒序填充
    QPushButton* autoGridBtn_{nullptr};    // 「按格数自动」重算 cols/rows
    QLabel* rearrangeWarn_{nullptr};       // 内联警告（红字）
    // 重排上下文（PreviewController 回灌）：保留块数 + 网格单元尺寸（供自动 cols/rows 与警告）。
    int keptCount_{0};
    int cellW_{0};
    int cellH_{0};
    bool uniformBlocks_{true};              // 保留块长宽是否全等（Core 重排准入门限，SPEC §4.3）
    bool mergeGridTouched_{false};         // 用户手动改过 cols/rows → 停止自动填充
    bool collapsible_{true};               // 最近一次 Core 坍缩可行性（供模式项启用/禁用）
    
    i18n::Retranslator retranslator_;      // 本面板文案重放登记表
};

} // namespace idc::gui
