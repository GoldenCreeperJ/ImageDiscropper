// ============================================================================
// 文件：canvas/items/grid_handle_item.h
// 作用：网格线层的两个基准手柄（L3 专属）——基准点手柄 (x0,y0) 与单元尺寸手柄 (x0+cw, y0+ch)，
//       供用户在画布上直接拖动 grid 基准 (x0,y0,cw,ch)，与参数面板同义（见 GridLayer）。
// 分块依据：
//   - 手柄只做「拖动 → 预览参数」的交互几何（整数像素吸附 + 越界钳制），不重铺网格、不写 Document；
//     网格线重画由 CanvasScene 依预览参数交 Core Grid::build 完成（GUI 不自算网格几何，CONTRIBUTING.md「分层纪律」）；
//   - 两个角色合为一个图元类：几何与绘制只差一个内标记，拆开只会重复拖动/钳制逻辑。
// 说明：拖拽中逐帧发 previewed（仅供场景重画网格线），释放时发一次 committed——上层据此
//       一次性写回 Document，避免逐帧触发 Core 预览全量重算（拖拽不逐帧写 Document）。
// ============================================================================
#pragma once

#include <QGraphicsObject>
#include <QPointF>

#include "engine/grid.h"
#include "util/gui_nums.h"   // nums::canvas（手柄场景口径兜底初值）

namespace idc::gui {

// ---------------------------------------------------------------------------
// GridHandleItem：网格基准手柄（拖基准点 / 拖单元尺寸）。
// ---------------------------------------------------------------------------
class GridHandleItem : public QGraphicsObject {
    Q_OBJECT
public:
    // Origin：基准点手柄，位于 (x0,y0)，拖动平移网格相位（x0/y0 变，cw/ch 不变）；
    // Cell：单元尺寸手柄，位于 (x0+cw, y0+ch)，拖动改单元宽高（基准点不动）。
    enum class Role { Origin, Cell };

    explicit GridHandleItem(Role role, QGraphicsItem* parent = nullptr);

    QRectF boundingRect() const override;
    void paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget) override;

    // 原图尺寸（越界钳制的边界；<=0 表示无图 → 手柄不吃鼠标）。
    void setImageBounds(int width, int height);
    // 已提交的网格参数：手柄据此落位（自身 pos 随之同步）。拖拽中由场景按预览参数回灌，
    // 拖拽基线仍是按下时快照，故不会被中途回灌打乱。
    void setParams(const engine::GridParams& params);
    const engine::GridParams& params() const { return params_; }
    // 两轴是否周期定义（分割数/比例轴没有可拖的基准点与单元尺寸）：
    //   非周期轴在手柄落位上取 0、且不随拖动改变；两轴皆非周期时手柄整体退场（无可拖之物）。
    void setPitchAxes(bool xPitch, bool yPitch);
    // 手柄边长与抓取区（场景单位，由视图按缩放换算，恒约 8 屏幕px）。
    void setHandleSize(qreal sceneUnits);
    // 显隐 + 可交互（网格线图层隐藏 / 非 L3 → false：隐藏且不吃鼠标）。
    void setEditable(bool on);
    // 仅切「吃不吃鼠标」（四态模式门控：图片拾取 / 标注选取态让位，手柄仍可见）。
    void setInteractive(bool on);
    bool isDragging() const { return dragging_; }

signals:
    // 拖拽中逐帧：新参数（已取整、已钳制在图内），供场景重画网格线（不写 Document）。
    void previewed(const engine::GridParams& params);
    // 释放：一次性提交（原地点击也发，值未变则由 Document 侧早退，不产生重算）。
    void committed(const engine::GridParams& params);

protected:
    void mousePressEvent(QGraphicsSceneMouseEvent* event) override;
    void mouseMoveEvent(QGraphicsSceneMouseEvent* event) override;
    void mouseReleaseEvent(QGraphicsSceneMouseEvent* event) override;

private:
    // 手柄在场景中的落点（Origin → 基准点；Cell → 基准点 + 单元尺寸；非周期轴按 0 计）。
    QPointF handleScenePos() const;
    // 依按点位与当前角色推出新参数（整数像素吸附 + 图内钳制），变化时才发 previewed。
    void applyDrag(const QPointF& scenePos);
    // 是否有可拖的定义（任一轴周期）：两轴皆非周期时手柄不吃鼠标也不显示。
    bool draggable() const { return pitchX_ || pitchY_; }

    Role role_;
    engine::GridParams params_{};    // 已提交参数（落位依据）
    engine::GridParams dragBase_{};  // 本手势按下时的参数快照（拖动增量恒以此为基准，免累积漂移）
    QPointF pressScenePos_;          // 按下点（场景坐标）
    int imgW_{0};
    int imgH_{0};
    qreal handleSize_{nums::canvas::kFallbackHandleScenePx};
    bool pitchX_{true};              // 横轴是否周期（否则分割数/比例：无基准点可拖）
    bool pitchY_{true};              // 纵轴是否周期
    bool dragging_{false};
};

} // namespace idc::gui
