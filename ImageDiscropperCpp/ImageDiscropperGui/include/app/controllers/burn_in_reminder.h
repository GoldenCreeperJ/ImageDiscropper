// ============================================================================
// 文件：app/controllers/burn_in_reminder.h
// 作用：导出/保存配置前的「未烧录提醒」——画布上有标注但未勾选标注烧录、或有叠加图片图层
//       但未勾选图层烧录时，弹一个三键快捷框：
//         「开烧录并导出」——视同用户勾选对应烧录（经 ExportPanel::enable* 发信号链写回真相源 +
//           同步菜单/预览），随后继续；
//         「按现状导出」——不改开关直接继续；
//         「取消 / 关闭」——中止本次动作。
//       由应用级偏好 UiPrefs::confirmBurnInOnExport 控制（默认开）；ExportController（图片导出）
//       与 ConfigController（保存配置）共用同一判定与交互，保证两处提醒语义一致。
// 分块依据：提醒是跨两个控制器的同一交互关注点，独立成件免两处逻辑漂移；不属任一控制器私有状态。
// 说明：
//   · 返回 true 表示继续（开烧录并导出 / 按现状导出 / 偏好关闭时不打扰），false 表示用户取消，
//     调用方须据此中止本次导出/保存。
//   · 「开烧录并导出」的开关落地依赖 panel：panel 为空（装配缺位）时退化为不改开关（等同按现状），
//     仍返回继续——调用方随后按当前 burnInEnabled()/layersBurnIn() 决定后续行为。
// ============================================================================
#pragma once

class QWidget;

namespace idc::gui {

class ExportPanel;
class Document;
class AnnotationBridge;

// 未烧录三键提醒；返回 true = 继续，false = 取消（调用方中止）。
bool confirmBurnInBeforeExport(QWidget* parent, ExportPanel* panel, Document* doc, AnnotationBridge* anno);

} // namespace idc::gui
