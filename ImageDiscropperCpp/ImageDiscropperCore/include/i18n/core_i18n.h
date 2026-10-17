// ============================================================================
// 文件：include/i18n/core_i18n.h
// 作用：三层共用的国际化机制内核——语言目录表类型、进程级当前语言状态、查表函数，
//       以及 Core 层门面宏 IDC_KTR。
// 架构定位：Core 层（零 Qt，仅标准库）。GUI / CLI / Core 都链接 image_discropper_core，
//       故「已登记的语言」与「当前语言」在全进程**只有一份**，不存在各层语言不同步；
//       各层的译文表由各自装配：GUI / CLI 的表住各自层（不让无关层为其膨胀），Core 报因的表
//       住 Core 自己（src/i18n/lang_en.cpp），由本头声明的 installCoreCatalogs() 登记；
//       GUI / CLI 的 installCatalogs() 各调用它一次，三层共用同一份，不为同一句话存两遍译文。
// 为什么自研而不用 Qt Linguist（tr() / lupdate / .ts / .qm）：全部文案住在常量层的
//       `inline constexpr char kXxx[]`，lupdate 抽不到常量；.qm 是 Qt6 内部二进制格式，
//       零 Qt 的 Core / CLI 读不了；引 qttools 还会连带拖进整套 qtbase 双份构建。
//       结论：中文常量本身即「源语言文本 + 回退值」，译文表只存「键 → 该语言文本」，
//       编译期内嵌，无需任何构建期工具链与运行时资源文件。
// 键形态：`<分组>::<常量名>`（如 "menu::kMenuFile"）。键由门面宏经预处理器 `#` 字符串化生成，
//       使用点只写两个标识符，故不可能出现拼错的键；跨层 (分组, 常量名) 撞名会**静默串译文**，
//       由 unit_tests 的目录对账用例断言全局唯一（tests/test_i18n_catalog.cpp）。
// 占位符：句式里要嵌入序号 / 路径等运行期值时，写成**单条整句 + %N**（如 "第 %1 个标注元素：%2"），
//       不拆成前后两段碎片——拆开后译者无法重组语序（英语就把序号挪到句尾）。
//       替换由 i18n::formatPattern 完成（配 IDC_KTRF 宏）；源语言下替换结果与直接字符串拼接逐字节相同。
// 语言码：BCP-47 风格短码。源语言 kSourceLanguage 恒可用且**不建表**——它的译文就是常量层本身，
//       再存一份纯属重复；其余语言码由各层表文件登记（当前内置 kLangCodeEn）。
// 回退纪律：语言未装 / 该语言缺该键 → 原样返回调用点传入的中文源值。
//       故「译文缺失」永远是**渐进补全**而非崩溃或空白，zh-CN 路径永远是零行为变化。
// 生命周期与线程：语言表只在**进程启动期单线程**登记（GUI 在 QApplication 构造之后、顶层窗口构造
//       之前；CLI 在 cliMain 顶部），运行期不再增删表项；语言切换（setLanguage）支持运行期热切换，
//       安全由纪律保证：切换只发生在**模态静止点**（外观对话框为模态框，选定语言即在其事件循环里
//       同步改全局语言，此刻主窗手势/刷新全部阻塞；GUI 耗时重计算亦同步跑在主线程）。全 GUI 无脱离
//       主线程的并发读者，表读写恒在同一线程串行发生。若将来引入后台线程读文案，须先补同步。
// ============================================================================
#pragma once

#include <cstddef>
#include <initializer_list>
#include <string>
#include <string_view>
#include <vector>

#include "i18n/core_texts.h"   // IDC_KTR 的中文源值（回退值）所在层

namespace idc::core {

// 源语言码：常量层的中文即该语言的文本，故不为它建表，只在可用语言清单里占首位。
inline constexpr auto kSourceLanguage = "zh-CN";
// 目标语言码：各层表文件的登记码与 GUI 语言下拉 / CLI --lang 的取值共用此名，
// 免得三处各写一份 "en" 而漂移（加一门语言 = 新增一张表 + 一个码常量 + 注册一次，见 README）。
inline constexpr auto kLangCodeEn = "en";

// ---------------------------------------------------------------------------
// I18nEntry：一条译文——键 → 该语言文本。
// 两个指针都指向表文件里的静态字符串字面量（生命周期随进程），故查表结果可安全地
// 以 std::string_view 持有，不必拷贝。
// ---------------------------------------------------------------------------
struct I18nEntry {
    const char* key;
    const char* text;
};

// ---------------------------------------------------------------------------
// I18nTable：一门语言的一段表（指针 + 计数）。
// 计数由 makeI18nTable 从数组维度推导，调用点不写数字——杜绝「加了行忘了改数」。
// ---------------------------------------------------------------------------
struct I18nTable {
    const I18nEntry* entries = nullptr;
    std::size_t count = 0;
};

template <std::size_t N>
constexpr I18nTable makeI18nTable(const I18nEntry (&table)[N]) {
    return {table, N};
}

namespace i18n {

// —— 装配期（只在启动时调用，且必须先 install 后 set）——

// 登记某门语言的一段表。同一语言可多次调用（累加），便于一个层把表拆成多个文件。
// 源语言被忽略其内容（只确保它在清单里）；重复键以**先登记的一条**为准，
// 目录对账用例另有「无重复键」断言兜住。
void installLanguage(std::string_view lang, I18nTable table);

// 登记 Core 层自己的英文目录（表在 src/i18n/lang_en.cpp）。
// 由各层的 installCatalogs() 调用——GUI 与 CLI 都要显示 Core 的报因，故两边都装一次；
// 同语言累加不冲突，且表只有一份，不存在两侧漂移。
// 不自动装配（不靠静态对象自注册）：CLI 侧是静态库，未被引用的翻译单元会被链接器
// 整块丢弃，自注册会随调用点变化而静默失效，故一律显式调用。
void installCoreCatalogs();

// 切换当前语言。先经 resolveLanguage 归一（"en_US" → "en"）；
// 归一后为空（未登记 / 未知语言）→ 回退源语言，即一切文案走常量层原文，绝不报错。
void setLanguage(std::string_view lang);

// —— 使用期（只读）——

// 查表取译文；未装语言或缺键 → 返回 fallbackUtf8（调用点的中文源值）。永不返回空。
std::string translate(std::string_view key, std::string_view fallbackUtf8);

// 把 text 里的 %N（N＝1..9）依次替换为 args 的对应项（args[0] 填 %1）。
// 按**编号**取值而非按出现顺序，故译文可自由调整语序（"Item #%1: %2" 与 "第 %1 个：%2" 共用一份 args）；
// 同一占位符可重复出现；编号越界或 text 里无占位符 → 原样保留该片段，绝不崩、绝不吞字。
// 复数：本机制**不做**单复数变形（只按编号填槽）。如 "Delete all %1 annotations?" 在 count=1 时显示
//       "1 annotations"、英文形态不完美——属已知边界：此类确认框的数量几乎恒 >1。将来要处理时在
//       formatPattern 之上加 plural 变体（按数值选不同模板），勿改本函数的填槽语义。
std::string formatPattern(const std::string &text, std::initializer_list<std::string> args);

// 当前生效语言码（已归一、已回退后的结果，供 UI 回显与日志）。
std::string language();

// 把任意写法归一为**已登记**的语言码：精确命中 → 取基础子标签（首个 '-' / '_' 之前，大小写不敏感）
// 再命中 → 否则返回空串。源语言恒命中（返回 kSourceLanguage）。
// 「显式指定却认不出」的调用方（CLI --lang）据此报错；「跟随系统」的调用方（GUI）据此回退。
std::string resolveLanguage(std::string_view requested);

// 可用语言清单：源语言在首，其余按登记先后。CLI 据此校验 --lang，UI 据此枚举选项。
std::vector<std::string> availableLanguages();

} // namespace i18n
} // namespace idc::core

// ---------------------------------------------------------------------------
// Core 层门面：IDC_KTR(分组, 常量名) → std::string。
// 键与源值同源于一次书写：# 把两个标识符变成字符串，逗号后的 `::name` 取到中文常量本身，
// 因此「键拼错」与「回退值写错」两类笔误都在编译期暴露（未声明的标识符）。
// Core 的可译分组只有 texts::msg（jsonkey / defaults 是不可译契约，永不进目录）。
// msg 里包含图案类型的显示名（patternKindName 的返回值），它同样面向人、可译。
// ---------------------------------------------------------------------------
#define IDC_KTR(group, name) \
    ::idc::core::i18n::translate(#group "::" #name, ::idc::core::texts::group::name)

// 带占位符的整句：先取译文，再按编号填 %N（见 i18n::formatPattern）。
// 用法：IDC_KTRF(msg, kErrAnnoItemPrefix, std::to_string(idx), reason)
#define IDC_KTRF(group, name, ...) \
    ::idc::core::i18n::formatPattern(IDC_KTR(group, name), {__VA_ARGS__})
