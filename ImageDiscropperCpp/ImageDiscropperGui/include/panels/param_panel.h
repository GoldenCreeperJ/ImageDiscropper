// ============================================================================
// 文件：panels/param_panel.h
// 作用：右侧「参数面板」——随模式切换的 QStackedWidget：
//       L1 形状+坐标、L2 子功能+坐标+坍缩可行性提示、L3 网格定义+选择集+排序。
//       面板只把用户输入写回 Document（数值直接输入回车生效，NFR-5），不含几何逻辑（CONTRIBUTING.md「分层纪律」）。
// 分块依据：每个模式一页，页内控件与该模式的一两个 Document 字段对应；坍缩提示由 MainWindow
//           依 Core 的 isCollapsible 结果回灌（面板不自算可行性）。
// 说明：坐标 spinbox 关闭 keyboardTracking，避免逐键触发预览；提交（回车/失焦/箭头）才写回。
//       堆叠容器高度只随「当前可见页」变化（PageSizedStack 关 heightForWidth + reflowStack 钳 min=max，见 cpp）。
// ============================================================================
#pragma once

#include <QPointer>
#include <QWidget>

#include "engine/engine.h"

class QComboBox;
class QSpinBox;
class QLabel;
class QCheckBox;
class QPushButton;
class QResizeEvent;
class QStackedWidget;
class QListWidget;

namespace idc::gui {

class Document;

// ---------------------------------------------------------------------------
// ParamPanel：模式相关的参数输入面板。
// ---------------------------------------------------------------------------
class ParamPanel : public QWidget {
    Q_OBJECT
public:
    explicit ParamPanel(QWidget* parent = nullptr);

    void setDocument(Document* doc);
    // 从 Document 反向同步控件（形状/子功能/坐标），blockSignals 防回环。
    void syncFromDocument();
    // 切换到某模式对应的页（L1→0, L2→1, L3→2）。
    void setMode(engine::Tier tier) const;
    // 由 MainWindow 依 Core isCollapsible 结果回灌坍缩可行性提示（L2 页）。
    void setCollapseHint(bool collapsible, const QString& reason) const;

    // L2 多矩形：仅刷新列表中第 index 行的坐标文本（若为选中行则同步 spinbox），
    // 不重建整个列表——供画布拖拽期间实时回显矩形尺寸（此时 syncPanels 被跳过）。
    void updateRectListItem(int index) const;
    // L2 多矩形：把列表选中行置为 index（已是则免打扰），触发 rectSelected 与 spinbox 回填。
    void selectRectRow(int index) const;

signals:
    // L2 多矩形：列表选中行变化（-1 = 无），供 MainWindow 高亮画布上对应选区框。
    void rectSelected(int index);

private slots:
    void onL1ShapeChanged(int index) const;
    void onL2SubChanged(int index) const;
    void onCoordEdited() const; // L1/L2 坐标变更统一入口。
    void onConvertToGrid() const; // L2「转为网格模式编辑」：把当前选区送入 L3。
    // L2 多矩形并集（仅 MULTI_RECT）。
    void onRectListSelectionChanged(); // 列表选中项 → 把该矩形坐标载入 spinbox。
    void onRectDelClicked() const;           // 删除选中矩形。
    void onRectClearClicked() const;         // 清空矩形列表。
    // L3 网格参数 / 选择集 / 排序。
    void onGridOriginEdited() const;          // 基准点 x0/y0 变更。
    void onCellSizeEdited() const;            // 单元尺寸 cw/ch 变更。
    void onRemainderChanged(int index) const; // 余量策略变更。
    void onSortStrategyChanged(int index) const; // 排序策略变更。
    void onSortReverseToggled(bool on) const;    // 整体逆序。
    void onSortSnakeToggled(bool on) const;      // 蛇形排序。
    void onSelectAllCells() const;            // 全选。
    void onInvertCells() const;               // 反选。
    void onClearCells() const;                // 清空选择集。

    // 把堆叠容器高度钳到当前可见页所需高度（切页/宽度变化时重算）；公有槽供 currentChanged 直连。
public slots:
    void reflowStack() const;

protected:
    // 宽度变化（分隔条拖拽）时重钳当前页高度（wordWrap 换行高依赖宽度）。
    void resizeEvent(QResizeEvent* ev) override;

private:
    // 构建 L1 / L2 / L3 三页并加入 stack_。
    QWidget* buildL1Page();
    QWidget* buildL2Page();
    QWidget* buildL3Page();
    // 把四个坐标 spinbox 组装为规范化矩形写回 Document。
    void applyCoordsToDocument() const;

    Document* doc_{nullptr};
    QStackedWidget* stack_{nullptr};
    QPointer<QWidget> pages_[3];   // L1/L2/L3 页（构造时按序加入 stack_）。

    // L1 控件。
    QComboBox* l1Shape_{nullptr};
    QSpinBox* l1x1_{nullptr};
    QSpinBox* l1y1_{nullptr};
    QSpinBox* l1x2_{nullptr};
    QSpinBox* l1y2_{nullptr};

    // L2 控件。
    QComboBox* l2Sub_{nullptr};
    QSpinBox* l2x1_{nullptr};
    QSpinBox* l2y1_{nullptr};
    QSpinBox* l2x2_{nullptr};
    QSpinBox* l2y2_{nullptr};
    QLabel* collapseHint_{nullptr};
    QPushButton* l2ToGridBtn_{nullptr};  // 「转为网格模式编辑」入口

    // L2 多矩形并集控件（仅 MULTI_RECT 显示）。
    QWidget* l2MultiBox_{nullptr};         // 多矩形列表分组容器
    QListWidget* l2RectList_{nullptr};     // 矩形列表
    QPushButton* l2RectDelBtn_{nullptr};   // 删除选中
    QPushButton* l2RectClearBtn_{nullptr}; // 清空
    bool syncingRectList_{false};          // 重建列表时抑制选中信号回环

    // L3 控件（网格定义 / 选择集 / 排序）。
    QSpinBox* gx0_{nullptr};
    QSpinBox* gy0_{nullptr};
    QSpinBox* gcw_{nullptr};
    QSpinBox* gch_{nullptr};
    QComboBox* gRemainder_{nullptr};
    QLabel* gGridInfo_{nullptr};   // 派生行列数（只读，由 Core 回灌）
    QLabel* gSelInfo_{nullptr};    // 已选单元数（只读）
    QComboBox* gSort_{nullptr};
    QCheckBox* gReverse_{nullptr};
    QCheckBox* gSnake_{nullptr};
};

} // namespace idc::gui
