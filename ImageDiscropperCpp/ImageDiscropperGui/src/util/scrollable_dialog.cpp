// ============================================================================
// 文件：util/scrollable_dialog.cpp
// 作用：实现对话框滚动外壳与屏幕尺寸护栏（见同名头文件说明）。
// 分块依据：wrapScrollable 只做「装 QScrollArea + 换装触控滚动」；护栏为自包含事件过滤器（DialogSizeGuard）；
//           showScrollableMessage 组合二者呈现长文本信息框。滚动换装委托 mobile/touch_scroll（已升 common 层）。
// ============================================================================
#include "util/scrollable_dialog.h"

#include <QDialog>
#include <QDialogButtonBox>
#include <QEvent>
#include <QLabel>
#include <QPushButton>
#include <QRect>
#include <QScrollArea>
#include <QScreen>
#include <QVBoxLayout>
#include <QWidget>

#include "mobile/touch_scroll.h"
#include "util/gui_nums.h"

namespace idc::gui {

QScrollArea* wrapScrollable(QWidget* body) {
    auto* area = new QScrollArea;
    area->setWidgetResizable(true);
    area->setFrameShape(QFrame::NoFrame);
    area->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);   // 内容随视口宽自适应，不出横滚。
    area->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);      // 超高才出纵滚。
    area->setWidget(body);
    installTouchScroll(area);   // 移动端换装触控过滤器；桌面回落 QScroller 鼠标协调器。
    return area;
}

namespace {

// 屏幕尺寸护栏事件过滤器：Show / Resize 时按可用几何钳对话框最大宽 / 高；首次显示按内容首选尺寸收初值。
// 旋转（表现为 resize）即重钳最大 → 窗口自然再适配、部件不重建（SPEC §8.3）。
class DialogSizeGuard : public QObject {
public:
    DialogSizeGuard(QDialog* dialog, const QSize& preferredContentSize)
        : QObject(dialog), dialog_(dialog), preferred_(preferredContentSize) {}

protected:
    bool eventFilter(QObject* obj, QEvent* ev) override {
        if (obj != dialog_) return false;
        switch (ev->type()) {
            case QEvent::Show:
                clampMaximum();
                if (!firstSized_) sizeToContent();   // 未显示前 screen() 可能为空，Show 时已就位。
                break;
            case QEvent::Resize:
                clampMaximum();
                break;
            default:
                break;
        }
        return false;   // 不改事件流，仅旁路设最大尺寸 / 初值。
    }

private:
    QRect availableGeometry() const {
        const QScreen* scr = dialog_->screen();
        return scr ? scr->availableGeometry() : QRect();
    }

    void clampMaximum() const {
        const QRect avail = availableGeometry();
        if (avail.isNull()) return;
        const int maxW = static_cast<int>(avail.width() * nums::mobile::kDialogMaxWidthRatio);
        const int maxH = static_cast<int>(avail.height() * nums::mobile::kDialogMaxHeightRatio);
        dialog_->setMaximumSize(maxW, maxH);
    }

    // 首次显示：把尺寸收到「内容首选尺寸（未提供则对话框自身 sizeHint）与可用几何钳值」的较小者。
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

    QDialog* dialog_;
    QSize preferred_;
    bool firstSized_ = false;
};

} // namespace

void limitDialogToScreen(QDialog* dialog, const QSize& preferredContentSize) {
    dialog->installEventFilter(new DialogSizeGuard(dialog, preferredContentSize));
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
    label->setMinimumWidth(nums::mobile::kInfoDialogMinWidthPx);   // 控宽：窄屏 + word wrap 不再顶成竖窄条。
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
