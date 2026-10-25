// ============================================================================
// 文件：util/color_picker.h
// 作用：把「弹出带 Alpha 通道的取色对话框」收成单点共享实现——非原生对话框装配在各使用面板
//       逐字相同，分散驻留时改一处易漏改另一处，故只在此维护一份。
// 为何要显式装非原生框：因需 Alpha 通道，Qt 在 Windows 无法用原生取色框而回退到自带的固定尺寸
//       对话框 QColorDialogClassWindow（min==max）。若不显式设定初始 geometry，Qt 会以默认小尺寸
//       初始化、show 时被 Windows 强制夹到固定最小尺寸，刷出大量
//       "QWindowsWindow::setGeometry: Unable to set geometry" 噪声告警。故：以顶层窗口为父 +
//       DontUseNativeDialog，并在 exec 前显式给定 >= 对话框最小尺寸的初始 geometry（取自自身
//       sizeHint、随 DPI 自适应、居中到父窗口），从源头规避该告警。
// 类型：header-only inline（同 widget_factory.h / retranslate.h 助手风格）；仅 GUI 目标引用。
// 边界：本头只收「取色对话框」；色块按钮回显样式（applySwatch 等）各站点语义有差（是否含 :disabled
//       分支、是否带 alpha、透明特显文案），刻意不在此统一，留在各面板。
// ============================================================================
#pragma once

#include <QColor>
#include <QColorDialog>
#include <QDialog>
#include <QPoint>
#include <QRect>
#include <QSize>
#include <QString>
#include <QWidget>

namespace idc::gui {

// 弹出带 Alpha 的非原生取色框；inout 既作初始色传入、又在确认且颜色有效时接收所选色并返回 true。
// dlgParent 传顶层窗口（居中基准）；title 为对话框标题（调用方经 i18n 取好译文传入）。
inline bool pickColorWithAlpha(QWidget* dlgParent, QColor& inout, const QString& title) {
    QColorDialog dlg(inout, dlgParent);
    dlg.setWindowTitle(title);
    dlg.setOption(QColorDialog::ShowAlphaChannel, true);
    dlg.setOption(QColorDialog::DontUseNativeDialog, true);
    const QSize dlgSz = dlg.minimumSizeHint().expandedTo(dlg.sizeHint());
    QPoint pos(80, 80);
    if (const QWidget* pw = dlg.parentWidget()) {
        const QRect pg = pw->window()->geometry();   // 顶层窗口 geometry 为全局屏幕坐标。
        pos = pg.center() - QPoint(dlgSz.width() / 2, dlgSz.height() / 2);
    }
    dlg.setGeometry(pos.x(), pos.y(), dlgSz.width(), dlgSz.height());
    if (dlg.exec() != QDialog::Accepted) return false;
    const QColor c = dlg.currentColor();
    if (!c.isValid()) return false;
    inout = c;
    return true;
}

} // namespace idc::gui
