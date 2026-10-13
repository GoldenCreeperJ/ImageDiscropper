// ============================================================================
// 文件：tests/test_i18n_core.cpp
// 作用：i18n 机制内核（i18n/core_i18n.h）的回归断言——表登记、语言码归一、回退语义、源语言不建表、占位符格式化。
// 分块依据：只测**机制**不测译文内容（内容与常量层的对账在 tests/test_i18n_catalog.cpp），
//           故本用例自带一张假表，不依赖 GUI / CLI 的表文件（那两个目标也不在 unit_tests 里）。
// 说明：
//   · 表必须是 namespace 作用域的 constexpr 数组——内核只登记 string_view，不拷贝表本体，
//     函数内的局部数组会当场悬空（这条纪律由本用例的形状共同守住）。
//   · 本用例会改动进程级语言状态，故在 test_main 的调度里放**最后一段**，且收尾复位到源语言。
//   · formatPattern 为纯函数（不读语言状态），故与语言切换断言同段不相互干扰。
// ============================================================================
#include <string>
#include <vector>

#include "test_harness.h"

#include "i18n/core_i18n.h"

namespace {

constexpr idc::core::I18nEntry kFakeEn[] = {
    {"label::kAlpha", "Alpha"},
    {"label::kBeta", "Beta"},
};

// 与 kFakeEn 有重复键的第二段表：验证「同语言多段累加」且「先登记者优先」。
constexpr idc::core::I18nEntry kFakeEnAgain[] = {
    {"label::kAlpha", "Alpha-second"},
    {"label::kGamma", "Gamma"},
};

} // namespace

void testI18nCore() {
    using namespace idc::core;

    // —— 初始态：源语言、无任何表 → 一切走回退（这正是 zh-CN 路径零行为变化的根据）。——
    CHECK(i18n::language() == kSourceLanguage);
    CHECK(i18n::translate("label::kAlpha", "字母") == "字母");

    // —— 登记：同语言多段累加，当前语言未切时表不生效。——
    i18n::installLanguage(kLangCodeEn, makeI18nTable(kFakeEn));
    i18n::installLanguage(kLangCodeEn, makeI18nTable(kFakeEnAgain));
    CHECK(i18n::translate("label::kAlpha", "字母") == "字母");

    // —— 归一：精确 / 大小写 / 基础子标签 / 中文系 / 未知 / 空。——
    CHECK(i18n::resolveLanguage("en") == "en");
    CHECK(i18n::resolveLanguage("EN") == "en");
    CHECK(i18n::resolveLanguage("en_US") == "en");
    CHECK(i18n::resolveLanguage("en-US.UTF-8") == "en");
    CHECK(i18n::resolveLanguage("zh_CN") == kSourceLanguage);
    CHECK(i18n::resolveLanguage("ZH-cn") == kSourceLanguage);
    CHECK(i18n::resolveLanguage("fr").empty());
    CHECK(i18n::resolveLanguage("C").empty());
    CHECK(i18n::resolveLanguage("").empty());

    // —— 切换后：命中译文、缺键回退、语言码已归一。——
    i18n::setLanguage("en_US");
    CHECK(i18n::language() == "en");
    CHECK(i18n::translate("label::kAlpha", "字母") == "Alpha");        // 重复键取先登记的一条
    CHECK(i18n::translate("label::kGamma", " gamma") == "Gamma");      // 第二段表可见
    CHECK(i18n::translate("label::kMissing", "缺项") == "缺项");         // 缺键回退，不崩不空

    // —— 未知语言：setLanguage 不报错，静默回退源语言（GUI「跟随系统」走到这里）。——
    i18n::setLanguage("fr");
    CHECK(i18n::language() == kSourceLanguage);
    CHECK(i18n::translate("label::kAlpha", "字母") == "字母");

    // —— 源语言不建表：给它装表也被忽略，避免与常量层重复存一份中文。——
    i18n::installLanguage(kSourceLanguage, makeI18nTable(kFakeEn));
    i18n::setLanguage("zh-CN");
    CHECK(i18n::translate("label::kAlpha", "字母") == "字母");

    // —— 清单：源语言恒在首，其余按登记先后；不认识的码不混进来。——
    const std::vector<std::string> langs = i18n::availableLanguages();
    CHECK(langs.size() == 2);
    CHECK(langs.front() == kSourceLanguage);
    CHECK(langs[1] == "en");

    // —— 占位符：按**编号**取值（译文可自由调语序），越界与孤立 % 原样保留。——
    CHECK(i18n::formatPattern("无占位符", {"a", "b"}) == "无占位符");
    CHECK(i18n::formatPattern("第 %1 个元素：%2", {"3", "bad"}) == "第 3 个元素：bad");
    CHECK(i18n::formatPattern("%2 before %1", {"a", "b"}) == "b before a");   // 编号乱序仍按编号取
    CHECK(i18n::formatPattern("%1-%1-%1", {"x"}) == "x-x-x");                // 同位可重复
    CHECK(i18n::formatPattern("a %3 b", {"x", "y"}) == "a %3 b");             // 越界保留
    CHECK(i18n::formatPattern("50% / 100%", {}) == "50% / 100%");             // 孤立 % 与空参
    CHECK(i18n::formatPattern("%0 %a %", {"x"}) == "%0 %a %");               // 只认 %1..%9
    CHECK(i18n::formatPattern("", {"x"}).empty());

    std::cout << "[i18n-core] OK\n";
}
