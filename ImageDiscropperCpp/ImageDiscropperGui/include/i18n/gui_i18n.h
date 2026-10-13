// ============================================================================
// 文件：include/i18n/gui_i18n.h
// 作用：GUI 层的 i18n 门面——IDC_GTR 宏（查表取 QString）+ 本层译文表的装配入口。
// 架构定位：util（与 gui_texts.h / gui_nums.h 同族的基建头）。业务代码只认 IDC_GTR，
//       不直接调 core::i18n——语言状态与查表实现在 Core（三层共用一份进程表），
//       本头只负责「键的拼法」与「QString 适配」这两件 GUI 专属事。
// 与 Core 的分工：本层可译分组（menu / label / tooltip / status / msg）的英文表住
//       src/i18n/lang_en.cpp，由 installCatalogs() 登记；**ident 组不可译**（QSettings 键名、
//       objectName、属性名、CLI 参数名等契约串），一律保持 QString::fromUtf8(texts::ident::…) 原样。
// 为什么宏而不是函数：键必须由**常量名**在编译期字符串化生成（#group "::" #name），
//       同时把同名的中文常量作为回退值传入——两处同源，改写时不可能写成另一条串。
//       手写键字符串会带来「键拼错 → 静默回退 → 永不自检」的失效形态，宏把它变成编译期事实。
// ============================================================================
#pragma once

#include <string>
#include <string_view>

#include <QString>

#include "i18n/core_i18n.h"
#include "i18n/gui_texts.h"

namespace idc::gui::i18n {

// 查表取译文并转 QString（Qt 侧接口一律收 UTF-16，源值恒为 UTF-8 字节串）。
// 缺译文 → 回退中文源值，永不返回空串。
inline QString qs(const std::string_view key, const std::string_view fallbackUtf8) {
    const std::string text = core::i18n::translate(key, fallbackUtf8);
    return QString::fromUtf8(text.c_str());
}

// 「键 + 中文源值」的成对引用，供**清单表**使用：下拉条目、工具按钮一类的文案是
// 先装进 constexpr 数组（std::array<std::pair<枚举, const char*>, N> 形态）、再在循环体里
// addItem 的——转换点与常量名隔着一次遍历，IDC_GTR 就地套不进去。让表本身带上键即可：
// 表内存 IDC_GT(...)（零 Qt、仍是字面类型、constexpr 不破），循环体调 qs(ref)。
// 只有 string_view 两个指针，运行期不额外分配；表内容仍与常量层同源（宏生成，不手抄）。
struct TextRef {
    std::string_view key;
    std::string_view text;
};

inline QString qs(const TextRef &ref) { return qs(ref.key, ref.text); }

// 登记本层全部内置语言的译文表（一期仅英文）。必须在 setupFromPreference 之前调用。
// 定义在表文件 src/i18n/lang_en.cpp 内——译文表归本层所有，不经 image_discropper_core 传播。
void installCatalogs();

// 按用户偏好决议并切换语言。pref 取 texts::ident 的三个值之一：
//   · kLangAuto（含空串）→ 跟随系统 locale（QLocale::system()），中文系统即源语言；
//   · 其余（"zh-CN" / "en"）→ 直接采用；未知写法由 core::i18n::setLanguage 归一后回退源语言。
// 启动装配：本函数在 app/main.cpp 于顶层窗口构造之前调用一次，决议初始语言。
// 热切换：运行期在外观对话框（模态）内选定新语言时即调本函数切全局语言，随后**原地**重放各窗文案
//（MainWindow::retranslateUi，经 AppearanceDialog::languageApplied 触发），不销毁重建窗口——Document /
// AnnotationBridge / DocHistory 随窗口存活，UI 与业务态全程无损。全 GUI 无脱离主线程的并发读者，
// 单线程内切换安全（见 app/main.cpp、app/appearance_dialog.cpp）。
void setupFromPreference(const QString& pref);

// 解析偏好「应为」的语言码（**不改变**进程语言状态）：auto/空 → 系统 locale，其余直接用；
// 认不出的写法回退源语言。供外观对话框比对「已存偏好」与「当前生效语言」，判定是否需热切换。
std::string resolveForPreference(const QString& pref);

} // namespace idc::gui::i18n

// ---------------------------------------------------------------------------
// GUI 层门面（两条，键一律由常量名在预处理器里拼出）：
//   · IDC_GTR(分组, 常量名) → QString：就地取值的使用点，形态由
//     `QString::fromUtf8(texts::label::kXxx)` 改写而来，语义等价（zh-CN 下逐字节相同），
//     差别只在「先查译文表，缺则回退中文源值」。
//   · IDC_GT(分组, 常量名) → TextRef：**清单表位**用（存起来稍后转），见上方 TextRef 说明。
// 两条共用同一套分组名与常量名，故对账测试只需认 `分组::常量名` 一种键形态。
// ---------------------------------------------------------------------------
#define IDC_GTR(group, name) \
    ::idc::gui::i18n::qs(#group "::" #name, ::idc::gui::texts::group::name)

#define IDC_GT(group, name) \
    ::idc::gui::i18n::TextRef{#group "::" #name, ::idc::gui::texts::group::name}
