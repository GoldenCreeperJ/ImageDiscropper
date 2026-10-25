// ============================================================================
// 文件：util/scrollable_dialog.cpp
// 作用：实现对话框滚动外壳与屏幕尺寸护栏（见同名头文件说明）。
// 分块依据：wrapScrollable 只做「装 QScrollArea + 预留纵滚宽 + 换装触控滚动」；护栏为自包含事件过滤器（DialogSizeGuard）；
//           showScrollableMessage 组合二者呈现长文本信息框。滚动换装委托 mobile/touch_scroll（归 common/util 组编译）。
// ============================================================================
#include "util/scrollable_dialog.h"

#include <QDialog>
#include <QDialogButtonBox>
#include <QEvent>
#include <QLabel>
#include <QPointer>
#include <QPushButton>
#include <QRect>
#include <QScrollBar>
#include <QScrollArea>
#include <QScreen>
#include <QVBoxLayout>
#include <QWidget>

#include "mobile/touch_scroll.h"
#include "util/gui_nums.h"

namespace idc::gui {

int verticalScrollBarExtent(const QScrollArea* area) {
    if (!area) return 0;
    if (const QScrollBar* bar = area->verticalScrollBar())
        return bar->sizeHint().width();   // 当前样式下纵滚条的横向占用（未显时也据此预留）。
    return 0;
}

QScrollArea* wrapScrollable(QWidget* body) {
    auto* area = new QScrollArea;
    area->setWidgetResizable(true);
    area->setFrameShape(QFrame::NoFrame);
    area->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);   // 内容随视口宽自适应，不出横滚。
    area->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);      // 超高才出纵滚。
    area->setWidget(body);
    // 预留一条纵滚宽度：横滚 AlwaysOff + 纵滚 AsNeeded 时，纵滚一旦显示会吃掉视口一条宽；把滚动区最小宽
    // 抬到「内容最小宽 + 纵滚条宽」，所在对话框的布局下限随之抬升、resize 被钳到此下限，使视口恒 ≥ 内容最小宽，
    // 最右列不再被裁（不预留时须手动拖窗重排才回正）。无纵滚时仅多一条窄空隙，不影响可读性。
    area->setMinimumWidth(body->minimumSizeHint().width() + verticalScrollBarExtent(area));
    installTouchScroll(area);   // 移动端换装触控过滤器；桌面回落 QScroller 鼠标协调器。
    return area;
}

namespace {

// 屏幕尺寸护栏事件过滤器：
//   · 装于对话框本体——Show 时钳最大宽/高 + 首次收尺寸；Resize 时重钳（旋转/拖窗后上限变化）。
//   · 装于父窗口——Resize / Move 时把对话框居中到父窗口全局中心（旋转后位置回正）。
//   设计意图：桌面端 availableGeometry 为物理屏，不随窗口大小/位置变化；真机旋转则父窗口几何直接变。
//   故以父窗口几何变化为触发条件，不依赖屏幕级 availableGeometry 变更检测。
class DialogSizeGuard : public QObject {
public:
    DialogSizeGuard(QDialog* dialog, const QSize& preferredDialogSize)
        : QObject(dialog), dialog_(dialog), preferred_(preferredDialogSize) {
        // 额外注册父窗口事件监听——捕获 parent resize/move 以居中回正。
        if (auto* p = dialog->parentWidget()) {
            parent_ = p;
            p->installEventFilter(this);
        }
    }

    ~DialogSizeGuard() override {
        if (parent_) parent_->removeEventFilter(this);   // 防悬挂（parent 可能比 dialog 活得久）。
    }

protected:
    bool eventFilter(QObject* obj, QEvent* ev) override {
        if (obj == dialog_) {
            switch (ev->type()) {
                case QEvent::Show:
                    clampMaximum();
                    if (!firstSized_) sizeToContent();
                    lastParentGeo_ = parentGlobalGeo();  // 初显时记录 parent 几何基准。
                    break;
                case QEvent::Resize:
                    clampMaximum();
                    break;
                default:
                    break;
            }
        } else if (obj == parent_) {
            // 父窗口尺寸或位置变化（旋转 / 桌面拖窗 / 最大化等）→ 居中回正。
            if (ev->type() == QEvent::Resize || ev->type() == QEvent::Move) {
                reCenterIfNeeded();
            }
        }
        return false;   // 不改事件流，仅旁路设最大尺寸 / 初值 / 居中。
    }

private:
    QRect availableGeometry() const {
        const QScreen* scr = dialog_->screen();
        return scr ? scr->availableGeometry() : QRect();
    }

    // 父窗口的全局几何（顶层窗口 geometry 已含 mapToGlobal；若 parent 非顶层则 mapToGlobal）。
    QRect parentGlobalGeo() const {
        if (!parent_) return QRect();
        if (parent_->isWindow()) return parent_->frameGeometry();
        const QPoint tl = parent_->mapToGlobal(QPoint(0, 0));
        return QRect(tl, parent_->size());
    }

    void clampMaximum() const {
        const QRect avail = availableGeometry();
        if (avail.isNull()) return;
        const int maxW = static_cast<int>(avail.width() * nums::mobile::kDialogMaxWidthRatio);
        const int maxH = static_cast<int>(avail.height() * nums::mobile::kDialogMaxHeightRatio);
        dialog_->setMaximumSize(maxW, maxH);
    }

    // 首次显示：把整窗尺寸收到「首选整窗尺寸（未提供则对话框自身 sizeHint）与可用几何钳值」的较小者。
    void sizeToContent() {
        const QRect avail = availableGeometry();
        if (avail.isNull()) return;
        firstSized_ = true;
        const int maxW = static_cast<int>(avail.width() * nums::mobile::kDialogMaxWidthRatio);
        const int maxH = static_cast<int>(avail.height() * nums::mobile::kDialogMaxHeightRatio);
        const QSize want = preferred_.isValid() ? preferred_ : dialog_->sizeHint();
        if (!want.isValid()) return;
        dialog_->resize(qMin(want.width(), maxW), qMin(want.height(), maxH));
    }

    // 仅当 parent 几何实际变化（与上次记录不同）时才居中回正——避免 dialog 自身事件链无谓触发。
    void reCenterIfNeeded() {
        if (!dialog_->isVisible()) return;
        const QRect pg = parentGlobalGeo();
        if (pg.isNull() || pg == lastParentGeo_) return;
        lastParentGeo_ = pg;
        const QPoint center = pg.center();
        const QSize sz = dialog_->size();
        dialog_->move(center.x() - sz.width() / 2, center.y() - sz.height() / 2);
    }

    QDialog* dialog_;
    QPointer<QWidget> parent_;            // 父窗口（可能先于 dialog 销毁，QPointer 安全）。
    QSize preferred_;
    bool firstSized_ = false;
    QRect lastParentGeo_;                 // 上次 parent 几何，用于去重。
};

} // namespace

void limitDialogToScreen(QDialog* dialog, const QSize& preferredDialogSize) {
    dialog->installEventFilter(new DialogSizeGuard(dialog, preferredDialogSize));
}

void showScrollableMessage(QWidget* parent, const QString& title, const QString& text, const QString& okLabel) {
    QDialog dlg(parent);
    dlg.setWindowTitle(title);
    auto* root = new QVBoxLayout(&dlg);

    auto* body = new QWidget(&dlg);
    auto* bodyLay = new QVBoxLayout(body);
    bodyLay->setContentsMargins(0, 0, 0, 0);
    auto* label = new QLabel(text, body);
    label->setWordWrap(true);
    label->setTextInteractionFlags(Qt::TextSelectableByMouse);
    label->setMinimumWidth(nums::mobile::kInfoDialogMinWidthPx);   // 控宽：窄屏 + word wrap 不顶成竖窄条。
    bodyLay->addWidget(label);
    bodyLay->addStretch(1);

    QScrollArea* scroll = wrapScrollable(body);
    scroll->setMinimumHeight(nums::mobile::kInfoDialogMinHeightPx);
    root->addWidget(scroll, 1);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok, &dlg);
    if (QPushButton* ok = buttons->button(QDialogButtonBox::Ok))
        ok->setText(okLabel);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    root->addWidget(buttons);

    limitDialogToScreen(&dlg);   // 按可用几何钳最大宽 / 高（内容超出走内部滚动，按钮行常驻）。
    dlg.exec();
}

} // namespace idc::gui
