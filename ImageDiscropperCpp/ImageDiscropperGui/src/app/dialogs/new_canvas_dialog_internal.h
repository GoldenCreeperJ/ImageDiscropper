// ============================================================================
// 文件：app/dialogs/new_canvas_dialog_internal.h（GUI 私有内部头，仅 new_canvas_dialog 三个 TU 包含）
// 作用：NewCanvasDialog 拆分 TU 共用的三个装配件（全部 inline）——
//       scaled（预览代理等比缩）、pinnedTop（并排列顶对齐包装）、CollapsibleSection（纯折叠分组）。
// 分块依据：new_canvas_dialog 按「装配壳 / 三节构建 / 形态编排 / 合成定稿」拆四个 TU（见各文件头注），
//       节构建与预览合成两处都要这三个装配件；集中于此避免拷贝漂移（仿 param_panel_internal.h 先例，
//       不经 include/ 外泄）。CollapsibleSection 无 Q_OBJECT（纯 connect 连接），故可头内联免 moc。
// ============================================================================
#pragma once

#include <algorithm>
#include <cmath>
#include <functional>
#include <utility>

#include <QEvent>
#include <QFont>
#include <QHBoxLayout>
#include <QLabel>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWidget>

#include "util/gui_nums.h"   // nums::dialog::kNewCanvasSectionToggleMinPx（折叠箭头触控下限）

namespace idc::gui {

// 依最长边把尺寸与周期/线宽/点半径等比缩到预览代理（factor ≤ 1）。
inline int scaled(const int v, const double factor) { return std::max(1, static_cast<int>(std::lround(v * factor))); }

// 并排列的顶对齐包装：内容钉顶——列被兄弟列撑高时多余空间沉底（不居中浮空）。
inline QWidget* pinnedTop(QWidget* child) {
    auto* w = new QWidget(child->parentWidget());
    auto* lay = new QVBoxLayout(w);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->addWidget(child);
    lay->addStretch(1);
    return w;
}

// 纯折叠分组：标题行（箭头按钮 + 标题），点击切换内容显隐。无勾选语义——折叠只是显隐，
//   不改任何声明（图案组收起 ≠ 去掉图案元素）。
//   check 可为空（纯折叠）；非空时作为**声明开关**放标题行最左——勾选＝启用并展开、取消＝自动收起，
//   折叠箭头仍可独立收起内容（勾选语义与折叠显隐分离）。
//   onToggled：展开/收起回调（供对话框重算表单最小宽——内容显隐改变最小宽）。
class CollapsibleSection : public QWidget {
public:
    CollapsibleSection(const QString& title, QWidget* content, QWidget* parent, QWidget* check = nullptr,
                       std::function<void(bool)> onToggled = {})
        : QWidget(parent), content_(content), onToggled_(std::move(onToggled)) {
        auto* lay = new QVBoxLayout(this);
        lay->setContentsMargins(0, 0, 0, 0);
        lay->setSpacing(2);
        auto* head = new QHBoxLayout();
        head->setContentsMargins(0, 0, 0, 0);
        if (check) head->addWidget(check);
        arrow_ = new QToolButton(this);
        arrow_->setArrowType(Qt::DownArrow);
        arrow_->setAutoRaise(true);
        arrow_->setCheckable(true);
        arrow_->setChecked(true);
        arrow_->setMinimumSize(nums::dialog::kNewCanvasSectionToggleMinPx, nums::dialog::kNewCanvasSectionToggleMinPx);   // 触控下限（与抽屉按钮同口径；移动端对话框样式表同时兜底）。
        arrow_->setCursor(Qt::PointingHandCursor);
        titleLabel_ = new QLabel(title, this);
        QFont f = titleLabel_->font();
        f.setBold(true);
        titleLabel_->setFont(f);
        titleLabel_->setCursor(Qt::PointingHandCursor);
        titleLabel_->installEventFilter(this);   // 标题文字点击转发到箭头（命中面从小箭头扩到整行）。
        head->addWidget(arrow_);
        head->addWidget(titleLabel_, 1);
        lay->addLayout(head);
        lay->addWidget(content_);
        lay->addStretch(1);   // 内容钉顶：折叠节被所在列等高拉伸时，多余空间沉底（不居中浮空）。
        connect(arrow_, &QToolButton::toggled, this, [this](const bool expanded) {
            arrow_->setArrowType(expanded ? Qt::DownArrow : Qt::RightArrow);
            content_->setVisible(expanded);
            if (onToggled_) onToggled_(expanded);
        });
    }

    // 程序化展开/收起（开关联动：勾选=展开、取消=收起）。
    void setExpanded(const bool expanded) const { arrow_->setChecked(expanded); }

    // 标题行整行可点：标题文字按下即切换（与箭头同义）。
    bool eventFilter(QObject* watched, QEvent* event) override {
        if (watched == titleLabel_ && event->type() == QEvent::MouseButtonRelease) {
            arrow_->toggle();
            return true;
        }
        return QWidget::eventFilter(watched, event);
    }

private:
    QToolButton* arrow_{nullptr};
    QLabel* titleLabel_{nullptr};
    QWidget* content_{nullptr};
    std::function<void(bool)> onToggled_;
};

} // namespace idc::gui
