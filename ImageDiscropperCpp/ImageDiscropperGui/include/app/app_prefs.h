// ============================================================================
// 文件：app/app_prefs.h
// 作用：GUI 应用级「会话记忆 / 杂项交互偏好」单例——最近打开文件 MRU、上次打开目录、
//       上次导出目录、导出重排前二次确认开关、界面语言偏好的读写与 QSettings 应用级持久化。
// 命名说明：本头**刻意不取 `ui_*.h` 名**——Qt CMake 的 AUTOUIC 会把任何 `#include "ui_xxx.h"`
//       当作由 `xxx.ui` 生成的界面头，找不到对应 .ui 即在 idc_gui_common_autogen 步报错；
//       `ui_` 是 UIC 产物保留前缀，自定义头文件不得占用（类名 UiPrefs 无碍，坏在文件名）。
// 分块依据：
//   - 与 Palette（配色）/ CanvasPrefs（画布交互）同为「纯呈现/交互偏好」三兄弟之一：
//     不进 §7 EngineConfig（作业参数态，CLI 不感知这些 GUI 会话记忆）、不进 DocHistory 撤销栈
//     （记住路径 / 改确认开关不构成可撤销的一步）；持久化各走独立 QSettings 组（palette/ · canvas/ · ui/），互不耦合；
//   - 本件承载「跨会话的应用级记忆」而非画布渲染参数，故与 CanvasPrefs 分家。
// 说明：
//   · 非 QObject：无即时联动消费者——最近文件子菜单经 QMenu::aboutToShow 懒重建读取、
//     导出确认在 onExport 调用点即时读取，均无需 changed() 信号；变更即落盘。
//   · recentFiles 仅收本地可再开路径（content:// 等不可复用 URI 由调用方过滤）；
//     有序（最近在前），上限 kMaxRecent，重复路径前移。
//   · lastOpenDir / lastExportDir 仅作文件对话框起始目录的跨会话种子（与 Document 内
//     当前会话的 outputDir/outputFile 正交：后者是本次作业复用值，前者是下次对话框落点）。
//   · confirmRearrangeRisk：导出面板「合并重排参数风险」内联红字之外的模态二次确认开关，默认开。
//   · language：界面语言偏好（auto / 语言码），一期为重启生效，故与其余各项一样只落盘不发信号。
// ============================================================================
#pragma once

#include <QString>
#include <QStringList>

namespace idc::gui {

// ---------------------------------------------------------------------------
// UiPrefs：GUI 会话记忆 / 杂项偏好单例（进程内唯一，首次访问惰性从 QSettings 载入）。
// ---------------------------------------------------------------------------
class UiPrefs {
public:
    static UiPrefs& instance();

    // 最近打开文件（有序：最近在前；最多 kMaxRecent 条）。
    const QStringList& recentFiles() const { return recentFiles_; }
    void addRecentFile(const QString& absPath);   // 规整绝对路径、去重前移、截断、落盘。
    void clearRecentFiles();

    // 上次打开目录 / 上次导出目录（文件对话框起始目录种子；空 = 交系统默认）。
    QString lastOpenDir() const { return lastOpenDir_; }
    void setLastOpenDir(const QString& dir);
    QString lastExportDir() const { return lastExportDir_; }
    void setLastExportDir(const QString& dir);

    // 导出「合并重排参数风险」前是否弹模态二次确认（默认开）。
    bool confirmRearrangeRisk() const { return confirmRearrangeRisk_; }
    void setConfirmRearrangeRisk(bool on);

    // 界面语言偏好（存的是**语言码串**，不是当前生效语言）：
    //   ident::kLangAuto（缺省，跟随系统）/ core::kSourceLanguage / core::kLangCodeEn。
    // 本单例只存取该串，不解释它——决议与切换全在 i18n/gui_i18n.cpp（一期只在启动时走一次，
    // 故改这里不改变本次会话的语言，对话框据此提示「重启后生效」）。
    QString language() const { return language_; }
    void setLanguage(const QString& code);

    // 深浅色主题偏好（存的是 **mode 串**："system" / "light" / "dark"，缺省 "system"）：
    //   本单例只存取该串，不解释它——解析与应用全在 util/theme.h（启动一次 + 外观对话框选定即时再应用）。
    //   与 language 同形：只落盘不发 changed()（无跨会话即时联动消费者，chrome 重抛光由 theme::apply 承担）。
    QString themeMode() const { return themeMode_; }
    void setThemeMode(const QString& mode);

    // 默认输出文件夹（**仅桌面**）：用户预设的导出起始目录，与 lastExportDir（上次实际使用）互补。
    // 允许清空为空串（= 不预设，交下方 exportSeedDir 回退到 lastExportDir）。
    QString defaultExportDir() const { return defaultExportDir_; }
    void setDefaultExportDir(const QString& dir);
    // 导出对话框的起始目录种子：默认值存在且为有效目录时优先，否则回退上次使用（空则由调用方弹框）。
    // 决议只反映「默认 > 上次」两级；「本次手输」由导出调用点在 doc_ 层先行短路，不进本方法。
    QString exportSeedDir() const;

    static constexpr int kMaxRecent = 8;

private:
    UiPrefs();
    void saveRecents() const;

    QStringList recentFiles_;
    QString lastOpenDir_;
    QString lastExportDir_;
    QString language_;   // 界面语言偏好（初值取 ident::kLangAuto，由构造函数从 QSettings 覆盖）。
    QString themeMode_;  // 深浅色主题偏好（初值取 "system"，由构造函数从 QSettings 覆盖）。
    QString defaultExportDir_;   // 默认输出文件夹（初值空，由构造函数从 QSettings 覆盖；仅桌面）。
    bool confirmRearrangeRisk_ = true;
};

} // namespace idc::gui
