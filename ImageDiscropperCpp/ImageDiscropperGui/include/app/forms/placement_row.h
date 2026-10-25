// ============================================================================
// 文件：app/forms/placement_row.h
// 作用：PlacementRow——「锚点 / 枢轴 / 自由偏移」落位行组件（供 NewCanvasDialog 复用）：
//       9 向锚点 + 9 向枢轴 + X/Y 偏移三行一组，供「图层落位（图层相对底图）」与
//       「图片落位（图片相对图层）」两处复用。
//       只做采参并发 changed()；显隐由上游 setVisible 控制，语义（inCanvasAnchor /
//       layerPivot / 自由偏移）与 canvas::ImageTransform 字段一一对应。
// ============================================================================
#pragma once

#include <functional>

#include <QWidget>

#include "generate/pattern.h"   // generate::ImageAnchor

class QComboBox;
class QLabel;
class QSpinBox;

namespace idc::gui {

class CoordSpinBox;   // 偏移输入框（整数像素 + "50%" 百分比输入，见 include/util/coord_spin.h）

// ---------------------------------------------------------------------------
// PlacementRow：一组落位参数（锚点 / 枢轴 / X、Y 偏移）。
// ---------------------------------------------------------------------------
class PlacementRow : public QWidget {
    Q_OBJECT
public:
    explicit PlacementRow(const QString& groupTitle, QWidget* parent = nullptr);

    generate::ImageAnchor anchor() const;   // 锚点（ImageTransform::inCanvasAnchor）。
    generate::ImageAnchor pivot() const;    // 枢轴（ImageTransform::layerPivot）。
    int offsetX() const;                    // 自由偏移 X（px）。
    int offsetY() const;                    // 自由偏移 Y（px）。

    // 偏移另接受百分比串（SPEC §2.1）：基准由上游给（新建对话框＝画布宽/高）；返回 <= 0 则不认百分比。
    // 只影响输入语法，值仍以整数像素回读（offsetX/offsetY），故上游无需区分来源。
    void setOffsetBasisProvider(const std::function<int(bool)> &provider) const;

signals:
    void changed();   // 任一控件变化（供上游刷新预览）。

private:
    QLabel* groupLabel_{nullptr};
    QComboBox* anchorCombo_{nullptr};
    QComboBox* pivotCombo_{nullptr};
    CoordSpinBox* posXSpin_{nullptr};
    CoordSpinBox* posYSpin_{nullptr};
};

} // namespace idc::gui
