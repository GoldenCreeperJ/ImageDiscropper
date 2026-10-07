// ============================================================================
// 文件：panels/image_panel.h
// 作用：右侧「图像处理面板」（FR-1）——含三块相互独立的编辑区：
//       ① 顶部「画布尺寸（Resize）」常驻独立区：对原图设定绝对/按比例目标尺寸，即时生效、清除回原尺寸；
//          resize **不入队列**、恒最先重放（工作图 = 轻操作队列(resize(original))）。
//       ② 「几何操作」独立区 + 列表：旋转 / 翻转 的增 / 删 / 移 / 改（各自携带配置控件与列表）。
//       ③ 「色彩操作」独立区 + 列表：黑白 / 色道反色 / 色道分离 的增 / 删 / 移 / 改（各自携带配置控件与列表）。
//          两列表面向用户各自成序、可独立上下移；底层 steps 恒为「全部几何块在前 + 全部色彩块在后」，
//          故底部提交按 resize→geo→color 顺序合并（PreprocessController 保证此不变式）。
//       面板只采集意图并发信号（信号携带扁平队列下标：色彩项 = 几何项数 + 色彩行），真正的重放物化由
//       PreprocessController 经 EngineBridge 调 Core 完成（CONTRIBUTING.md「分层纪律」：GUI 面板不实现像素运算、不持有真相）。
// 分块依据：两区各自「操作类型（枚举）→ 参数控件启停 + PreprocessConfig 构造」，是纯前端意图翻译；
//           队列仅承载轻操作（几何 Gray/Split/Invert 之外的 Rotate/Flip；色彩 Gray/Split/Invert），Resize 从队列剥离为独立常驻设置。
// 说明：桌面与移动复用本面板（同一类，由各自 shell 嵌入）；行为同构，仅外层容器形态不同（SPEC §8.2）。
// ============================================================================
#pragma once

#include <QWidget>

#include "preprocess/preprocess_config.h"

class QComboBox;
class QGroupBox;
class QPushButton;
class QSpinBox;
class QCheckBox;
class QLabel;
class QListWidget;

namespace idc::gui {

class Document;

// ---------------------------------------------------------------------------
// ImagePanel：独立画布尺寸区 + 几何 / 色彩两个独立「配置区 + 列表」。
// ---------------------------------------------------------------------------
class ImagePanel : public QWidget {
    Q_OBJECT
public:
    explicit ImagePanel(QWidget* parent = nullptr);

    // 绑定会话状态源：读取当前输出尺寸（尺寸框默认值 + 比例换算基准）与队列内容。
    void setDocument(Document* doc);
    // 从 Document 反向同步：刷新尺寸区控件 / 按类型重建几何与色彩两列表 / 依 hasImage/选中态启停控件。
    void syncFromDocument();

signals:
    // ---- 轻操作队列增删移改（扁平队列下标：色彩项 = 几何项数 + 色彩行）----
    // 增：按 cfg 类型追加一个轻操作步骤（几何落几何块尾、色彩落队尾——由控制器保证不变式）。
    void stepAppendRequested(preprocess::PreprocessConfig cfg);
    // 改：就地替换指定扁平下标步骤（重放时其位置不变，仅参数变；类型不变，因各区只能产出本类操作）。
    void stepReplaceRequested(int index, preprocess::PreprocessConfig cfg);
    // 删：移除指定扁平下标步骤。
    void stepRemoveRequested(int index);
    // 移：上移(down=false) / 下移(down=true) 指定扁平下标步骤（限块内换序，跨块由控制器忽略）。
    void stepMoveRequested(int index, bool down);
    // 重置：清空 resize 与整条队列、恢复原图。
    void resetRequested();

    // ---- 独立画布尺寸（Resize）----
    // 应用绝对目标尺寸（恒从原图重采样）。
    void resizeAppliedRequested(int width, int height);
    // 按比例缩放（factor 相对当前 resize 目标 / 原图尺寸）。
    void resizeScaleRequested(double factor);
    // 清除独立 resize（回到原图尺寸）。
    void resizeClearedRequested();

private slots:
    // ---- 几何区 ----
    void onGeoAdd();         // 「添加几何步骤」：由几何类型 + 参数构造 cfg 发 stepAppendRequested。
    void onGeoReplace();     // 「更新所选几何」：对几何列表选中项发 stepReplaceRequested(扁平下标)。
    void onGeoDelete();      // 「删除所选几何」→ stepRemoveRequested(扁平下标)。
    void onGeoMoveUp();      // 几何列表上移：置 geoPendingRow_ 后 → stepMoveRequested(扁平下标,false)。
    void onGeoMoveDown();    // 几何列表下移：置 geoPendingRow_ 后 → stepMoveRequested(扁平下标,true)。
    void onGeoTypeChanged(int index) const;   // 切几何类型：启停角度 / 方向控件。
    void onGeoSelectionChanged();             // 几何列表选中变化：回灌参数到几何配置区 + 刷新按钮启停。
    // ---- 色彩区 ----
    void onColorAdd();
    void onColorReplace();
    void onColorDelete();
    void onColorMoveUp();
    void onColorMoveDown();
    void onColorTypeChanged(int index) const;
    void onColorSelectionChanged();
    // ---- Resize 区 ----
    void onApplyResize();
    void onScaleResizeClicked();
    void onClearResizeClicked();
    void onResizeWidthEdited(int value) const;   // 保持宽高比时，改宽联动算高（尺寸区内）。

private:
    // 依几何区类型 + 参数控件值构造一个几何轻操作 PreprocessConfig（RotateOp / FlipOp）。
    preprocess::PreprocessConfig buildGeoConfig() const;
    // 依色彩区类型 + 参数控件值构造一个色彩轻操作 PreprocessConfig（GrayOp / InvertOp / SplitOp）。
    preprocess::PreprocessConfig buildColorConfig() const;
    // 把某几何步参数回灌到几何配置区（类型下拉 + 角度 / 方向）。
    void loadGeoIntoControls(const preprocess::PreprocessConfig& cfg);
    // 把某色彩步参数回灌到色彩配置区（类型下拉 + 通道勾选）。
    void loadColorIntoControls(const preprocess::PreprocessConfig& cfg);
    // 某步的中文描述（供列表文本）。
    static QString describeStep(const preprocess::PreprocessConfig& cfg);

    // 两列表的选中扁平下标（色彩行需加几何项数偏移）；无选中返回 -1。
    int geoFlatIndex() const;
    int colorFlatIndex() const;
    int geoRowCount() const;   // 当前队列中几何步数量（色彩扁平下标偏移基准）。

    Document* doc_{nullptr};

    // 「画布尺寸（Resize）」独立区（常驻、有图即可用；不入队列）。
    QSpinBox*  resizeW_{nullptr};         // 目标宽（像素）。
    QSpinBox*  resizeH_{nullptr};         // 目标高（像素）。
    QCheckBox* resizeKeepRatio_{nullptr}; // 保持宽高比：改宽联动算高。
    QPushButton* applySizeBtn_{nullptr};  // 应用尺寸。
    QSpinBox*  resizeScalePct_{nullptr};  // 按比例缩放百分比（1..1000）。
    QPushButton* scaleSizeBtn_{nullptr};  // 按比例缩放。
    QPushButton* clearSizeBtn_{nullptr};  // 清除尺寸。
    QLabel*    currentSizeLabel_{nullptr};// 只读：当前输出尺寸 W×H。
    QLabel*    targetLabel_{nullptr};     // 只读：当前预处理目标（三元：画布 / 底图 / 选中图片图层），置顶显式标注。
    QGroupBox* sizeBox_{nullptr};          // 「画布尺寸（Resize）」整区容器（Base 目标禁用：底图不改尺寸）。
    QGroupBox* geoBox_{nullptr};           // 「几何操作」整区容器（Base 目标禁用：底图仅色彩预处理）。

    // 各列表移动后的高亮跟随：非负表示「重建本列表后应选中此行」（被移动项的新行）。
    int geoPendingRow_{-1};
    int colorPendingRow_{-1};

    // ---- 几何操作区（旋转 / 翻转）----
    QComboBox* geoTypeCombo_{nullptr};    // 几何类型：旋转 / 翻转。
    QComboBox* rotAngle_{nullptr};        // 旋转角度：-90 / 90 / 180。
    QComboBox* flipDir_{nullptr};         // 翻转方向：水平 / 垂直。
    QPushButton* geoAddBtn_{nullptr};     // 添加几何步骤。
    QPushButton* geoReplaceBtn_{nullptr}; // 更新所选几何。
    QListWidget* geoList_{nullptr};       // 几何步列表（队列前段）。
    QPushButton* geoDelBtn_{nullptr};     // 删除所选几何。
    QPushButton* geoUpBtn_{nullptr};      // 上移所选几何。
    QPushButton* geoDownBtn_{nullptr};    // 下移所选几何。

    // ---- 色彩操作区（黑白 / 反色 / 分离）----
    QComboBox* colorTypeCombo_{nullptr};  // 色彩类型：黑白 / 色道反色 / 色道分离。
    QCheckBox* chR_{nullptr};             // 通道 R（反色＝反相它 / 分离＝保留它）。
    QCheckBox* chG_{nullptr};             // 通道 G。
    QCheckBox* chB_{nullptr};             // 通道 B。
    QWidget*   colorChanRow_{nullptr};    // 「作用通道」整行容器（仅反色/分离显隐）。
    QPushButton* colorAddBtn_{nullptr};   // 添加色彩步骤。
    QPushButton* colorReplaceBtn_{nullptr};// 更新所选色彩。
    QListWidget* colorList_{nullptr};     // 色彩步列表（队列后段）。
    QPushButton* colorDelBtn_{nullptr};   // 删除所选色彩。
    QPushButton* colorUpBtn_{nullptr};    // 上移所选色彩。
    QPushButton* colorDownBtn_{nullptr};  // 下移所选色彩。

    // 重置。
    QPushButton* resetBtn_{nullptr};      // 重置预处理（清空 resize 与两列表队列）。
};

} // namespace idc::gui
