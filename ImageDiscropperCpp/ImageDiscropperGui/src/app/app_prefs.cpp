// ============================================================================
// 文件：app/app_prefs.cpp
// 作用：UiPrefs 单例实现——QSettings（ui/ 组）载入/落盘、最近文件 MRU、上次目录、确认开关
//       与界面语言偏好的存取。
// 分块依据：持久化组名与键名取 texts::ident（常量层），本件只将其转成 QString 后复用（载入 / 落盘 / 移除多处同源）；
//           取值器全部内联在头文件，本件只有变更路径。
// 说明：各记忆项独立落盘（改目录不重刷 MRU、改 MRU 不重刷确认开关），故不调统一 save()，
//       仅 recents 有列表结构、抽 saveRecents() 复用于 add/clear。
//       注：QSettings 组名 "ui" 是运行期字符串，与 AUTOUIC 无关（AUTOUIC 只识别 `#include "ui_*.h"`）。
// ============================================================================
#include "app/app_prefs.h"

#include <QFileInfo>
#include <QSettings>

#include "i18n/gui_texts.h"

namespace idc::gui {
namespace {

// QSettings 组名与键（真值存 texts::ident，此处只做一次 QString 转换，使用点保持简短）。
const QString kGroup = QString::fromUtf8(texts::ident::kGroupUi);
const QString kKeyRecent = QString::fromUtf8(texts::ident::kUiRecentFiles);
const QString kKeyLastOpen = QString::fromUtf8(texts::ident::kUiLastOpenDir);
const QString kKeyLastExport = QString::fromUtf8(texts::ident::kUiLastExportDir);
const QString kKeyConfirmRearrange = QString::fromUtf8(texts::ident::kUiConfirmRearrange);
const QString kKeyLanguage = QString::fromUtf8(texts::ident::kUiLanguage);
const QString kKeyThemeMode = QString::fromUtf8(texts::ident::kUiThemeMode);
const QString kKeyDefaultExport = QString::fromUtf8(texts::ident::kUiDefaultExportDir);
const QString kDefaultLanguage = QString::fromUtf8(texts::ident::kLangAuto);   // 缺省跟随系统 locale
const auto kDefaultThemeMode = QStringLiteral("system");   // 缺省跟随系统深浅（与 theme::Mode::System 同字）
constexpr bool kDefaultConfirmRearrange = true;

} // namespace

UiPrefs& UiPrefs::instance() {
    // Meyers 单例：C++11 起线程安全；生命周期随进程（QSettings 写入在变更点即时完成）。
    static UiPrefs s;
    return s;
}

UiPrefs::UiPrefs() {
    QSettings st;
    st.beginGroup(kGroup);
    recentFiles_ = st.value(kKeyRecent).toStringList();
    lastOpenDir_ = st.value(kKeyLastOpen).toString();
    lastExportDir_ = st.value(kKeyLastExport).toString();
    confirmRearrangeRisk_ = st.value(kKeyConfirmRearrange, kDefaultConfirmRearrange).toBool();
    language_ = st.value(kKeyLanguage, kDefaultLanguage).toString();
    themeMode_ = st.value(kKeyThemeMode, kDefaultThemeMode).toString();
    defaultExportDir_ = st.value(kKeyDefaultExport).toString();   // 缺省空（不预设）。
    st.endGroup();
    if (recentFiles_.size() > kMaxRecent) recentFiles_.resize(kMaxRecent);
}

void UiPrefs::saveRecents() const {
    QSettings st;
    st.beginGroup(kGroup);
    st.setValue(kKeyRecent, recentFiles_);
    st.endGroup();
    st.sync();
}

void UiPrefs::addRecentFile(const QString& absPath) {
    if (absPath.isEmpty() || absPath.startsWith(QString::fromUtf8(texts::ident::kContentUriScheme))) return;
    const QString canonical = QFileInfo(absPath).absoluteFilePath();
    recentFiles_.removeAll(canonical);   // 去重后前移。
    recentFiles_.prepend(canonical);
    if (recentFiles_.size() > kMaxRecent) recentFiles_.resize(kMaxRecent);
    saveRecents();
}

void UiPrefs::clearRecentFiles() {
    if (recentFiles_.isEmpty()) return;
    recentFiles_.clear();
    QSettings st;
    st.beginGroup(kGroup);
    st.remove(kKeyRecent);
    st.endGroup();
    st.sync();
}

void UiPrefs::setLastOpenDir(const QString& dir) {
    if (dir.isEmpty() || dir == lastOpenDir_) return;
    lastOpenDir_ = dir;
    QSettings st;
    st.beginGroup(kGroup);
    st.setValue(kKeyLastOpen, lastOpenDir_);
    st.endGroup();
    st.sync();
}

void UiPrefs::setLastExportDir(const QString& dir) {
    if (dir.isEmpty() || dir == lastExportDir_) return;
    lastExportDir_ = dir;
    QSettings st;
    st.beginGroup(kGroup);
    st.setValue(kKeyLastExport, lastExportDir_);
    st.endGroup();
    st.sync();
}

void UiPrefs::setConfirmRearrangeRisk(const bool on) {
    if (confirmRearrangeRisk_ == on) return;
    confirmRearrangeRisk_ = on;
    QSettings st;
    st.beginGroup(kGroup);
    st.setValue(kKeyConfirmRearrange, confirmRearrangeRisk_);
    st.endGroup();
    st.sync();
}

// 界面语言偏好：只落盘、不切语言——语言切换由调用方（外观对话框经 setupFromPreference + 原地重放）完成，main.cpp 仅取启动初值。
// 与其余各项同形：同值不重写盘；本对话框外无消费者，故不发 changed()。
void UiPrefs::setLanguage(const QString& code) {
    if (code.isEmpty() || code == language_) return;
    language_ = code;
    QSettings st;
    st.beginGroup(kGroup);
    st.setValue(kKeyLanguage, language_);
    st.endGroup();
    st.sync();
}

// 深浅色主题偏好：只落盘，不切主题（生效点在 app/main.cpp 启动装配与外观对话框即时调 theme::apply）。
// 与 language 同形：同值不重写盘；本单例不发 changed()（chrome 重抛光由 theme::apply 承担）。
void UiPrefs::setThemeMode(const QString& mode) {
    if (mode.isEmpty() || mode == themeMode_) return;
    themeMode_ = mode;
    QSettings st;
    st.beginGroup(kGroup);
    st.setValue(kKeyThemeMode, themeMode_);
    st.endGroup();
    st.sync();
}

// 默认输出文件夹：与其余各项不同——**允许清空**（空串也是一次有效变更，故只在同值时不重写盘）。
void UiPrefs::setDefaultExportDir(const QString& dir) {
    if (dir == defaultExportDir_) return;
    defaultExportDir_ = dir;
    QSettings st;
    st.beginGroup(kGroup);
    st.setValue(kKeyDefaultExport, defaultExportDir_);
    st.endGroup();
    st.sync();
}

// 导出对话框起始目录种子：默认值存在且为有效目录时优先，否则回退上次使用。
QString UiPrefs::exportSeedDir() const {
    if (!defaultExportDir_.isEmpty() && QFileInfo(defaultExportDir_).isDir()) return defaultExportDir_;
    return lastExportDir_;
}

} // namespace idc::gui
