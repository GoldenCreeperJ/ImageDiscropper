// ============================================================================
// 文件：src/i18n/gui_i18n.cpp
// 作用：GUI 门面的装配实现——按偏好决议语言码并切换到 Core 的进程级语言状态。
// 分块依据：门面头只放无状态的查表适配（qs 内联）与声明；本 TU 收「需要 Qt  locale 知识」
//           的那一段——语言决议属启动期装配，与业务无关，也不进 UiPrefs（UiPrefs 只存字符串，
//           不解释其含义， util → app 的反向依赖由此避免）。
// 说明：
//   · 热切换即时生效：本函数在 app/main.cpp 顶层窗口构造前调用一次装配初值；运行期改语言后，
//     于模态对话框关闭后的静止点再调一次，随即销毁重建窗口——重建即按新语言重跑全部构造期文案
//     （约 464 处），无需 retranslate 通道；全 GUI 无工作线程，单线程切换安全。
//   · 决议链：pref == auto/空 → QLocale::system() 的 BCP-47 名（"en-US" / "zh-CN" / "ja"…）；
//     其余 → 直接用 pref。两者都交给 core::i18n::setLanguage 归一，认不出的写法自动回退源语言，
//     故「未知语言」在 GUI 侧不构成错误（CLI 侧才需要报错，因它是显式命令行输入）。
//   · Android / iOS 一期不加语言 UI，pref 恒为 auto，即跟随系统语言。
// ============================================================================
#include "i18n/gui_i18n.h"

#include <QLocale>
#include <QString>

namespace idc::gui::i18n {

namespace {
// 偏好 → 候选语言串（auto/空 → 系统 locale 的 BCP-47 名，其余直接用 pref）。setup 与 resolve 共用。
QString candidateFromPreference(const QString& pref) {
    const bool followSystem = pref.isEmpty() || pref == QString::fromUtf8(texts::ident::kLangAuto);
    return followSystem ? QLocale::system().bcp47Name() : pref;
}
} // namespace

void setupFromPreference(const QString& pref) {
    // setLanguage 内部再经 resolveLanguage 归一；认不出自动回退源语言（GUI 侧不报错）。
    core::i18n::setLanguage(candidateFromPreference(pref).toStdString());
}

std::string resolveForPreference(const QString& pref) {
    const std::string resolved = core::i18n::resolveLanguage(candidateFromPreference(pref).toStdString());
    return resolved.empty() ? std::string(core::kSourceLanguage) : resolved;
}

} // namespace idc::gui::i18n
