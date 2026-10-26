// ============================================================================
// 文件：app/controllers/burn_in_reminder.cpp
// 作用：实现「未烧录提醒」三键快捷框（见同名头文件说明）。文案取自 i18n msg::kBurnSkip* 系列，
//       开关态经 ExportPanel::enable* 走既有信号链回写真相源并同步菜单/预览。
// ============================================================================
#include "app/controllers/burn_in_reminder.h"

#include <QAbstractButton>
#include <QCheckBox>
#include <QMessageBox>
#include <QPushButton>

#include "app/app_prefs.h"
#include "model/annotation_bridge.h"
#include "model/document.h"
#include "panels/export_panel.h"
#include "i18n/gui_texts.h"
#include "i18n/gui_i18n.h"

namespace idc::gui {

bool confirmBurnInBeforeExport(QWidget* parent, ExportPanel* panel, Document* doc, AnnotationBridge* anno) {
    // 偏好关闭：不打扰，直接继续。
    if (!UiPrefs::instance().confirmBurnInOnExport()) return true;

    const bool annoSkip = anno != nullptr && anno->count() > 0 && !anno->burnInEnabled();
    const bool layerSkip = doc != nullptr && !doc->layerStack().empty() && !doc->layersBurnIn();
    if (!annoSkip && !layerSkip) return true;

    QMessageBox box(parent);
    box.setIcon(QMessageBox::Warning);
    box.setWindowTitle(IDC_GTR(msg, kBurnSkipTitle));
    box.setText(annoSkip && layerSkip ? IDC_GTR(msg, kBurnSkipBodyBoth)
                : annoSkip            ? IDC_GTR(msg, kBurnSkipBodyAnno)
                                      : IDC_GTR(msg, kBurnSkipBodyLayers));
    // addButton(text, role) 与 clickedButton() 均返回基类 QAbstractButton*（非 QPushButton*）；
    //   addButton 内部实建 QPushButton，故设默认按钮时向下 static_cast 安全。
    QAbstractButton* enableBtn = box.addButton(IDC_GTR(msg, kBurnSkipEnableExport), QMessageBox::AcceptRole);
    const QAbstractButton* asIsBtn = box.addButton(IDC_GTR(msg, kBurnSkipExportAsIs), QMessageBox::DestructiveRole);
    box.addButton(QMessageBox::Cancel);
    box.setDefaultButton(dynamic_cast<QPushButton*>(enableBtn));
    // 内嵌「下次不再提醒」复选框（默认不勾，同「重置全部」对话框做法）：无论点哪个按钮，
    //   只要勾选就在弹框关闭后关掉 UiPrefs::confirmBurnInOnExport 偏好（落盘），下次不再弹。
    QCheckBox dontRemind(IDC_GTR(msg, kBurnSkipDontRemind), &box);
    box.setCheckBox(&dontRemind);
    box.exec();
    if (dontRemind.isChecked()) UiPrefs::instance().setConfirmBurnInOnExport(false);
    if (const QAbstractButton* clicked = box.clickedButton(); clicked == enableBtn) {
        // 视同用户勾选对应烧录：经面板信号链写回真相源 + 同步菜单/预览；panel 缺位则不改开关（等同按现状）。
        if (panel != nullptr) {
            if (annoSkip) panel->enableBurnIn();
            if (layerSkip) panel->enableLayersBurnIn();
        }
    } else if (clicked != asIsBtn) {
        return false; // 取消 / 关闭：中止。
    }
    return true;
}

} // namespace idc::gui
