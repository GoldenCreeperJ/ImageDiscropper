// ============================================================================
// 文件：src/i18n/cli_i18n.cpp
// 作用：CLI 门面的语言决议实现——环境变量链取值与 --lang 校验，末尾落到 Core 的进程级语言状态。
// 分块依据：门面头只放无状态的查表适配（tr 内联）与声明；本 TU 收「CLI 才有的来源优先级」，
//           与 GUI 的「偏好 + 系统 locale」各自一处，不强行统一（两层的输入面本就不同）。
// 说明：
//   · 必须在 installCatalogs() 之后调用（表未登记时任何码都解析为「未安装」）。
//   · 重启/重跑生效：本函数在 cliMain 顶部走一次，此后语言状态只读；
//     cli_tests 进程内多次调 cliMain，语言在每次调用开头按同样优先级重决议，故互不污染。
//   · 环境变量链：IDC_LANG（本工具专用，可覆盖桌面/终端语言）> LC_ALL > LANG（POSIX 惯例）。
//     三者的值可能是 "C" / "POSIX" / "en_US.UTF-8" 一类的非语言标签，
//     认不出时**静默回退源语言**——环境变量不该让一次正常作业变成报错。
// ============================================================================
#include "i18n/cli_i18n.h"

#include <cstdlib>
#include <string>

#include "i18n/core_i18n.h"

namespace idc::cli::i18n {
namespace {

// 环境变量名一律取 texts::env（不可译契约），按优先级从高到低排列。
const char* const kLangEnvVars[] = {
    texts::env::kIdcLang,
    texts::env::kLcAll,
    texts::env::kLang,
};

// 读环境变量：MSVC 把 getenv 标为 C4996（推 _dupenv_s），此处按平台取用并即时 free，
// 既不动全局警告开关也不泄漏；其余平台 getenv 无此问题。
std::string readEnv(const char* name) {
#ifdef _MSC_VER
    char* value = nullptr;
    std::size_t size = 0;
    if (_dupenv_s(&value, &size, name) != 0 || value == nullptr) return {};
    std::string out(value);
    std::free(value);
    return out;
#else
    if (const char* value = std::getenv(name); value != nullptr) return value;
    return {};
#endif
}

// 取环境变量链上第一个非空值；全空返回空串（＝未指定，交给 setLanguage 落源语言）。
std::string fromEnvironment() {
    for (const char* name : kLangEnvVars) {
        if (std::string value = readEnv(name); !value.empty()) return value;
    }
    return {};
}

} // namespace

bool applyLanguage(const std::string& requested) {
    if (!requested.empty()) {
        // 命令行显式写了：认不出就报错，不静默回退（回退会让拼错的 --lang 看起来「生效了」）。
        if (core::i18n::resolveLanguage(requested).empty()) return false;
        core::i18n::setLanguage(requested);
        return true;
    }
    // 未显式写：环境变量 → 空 → setLanguage 内部回退源语言。
    core::i18n::setLanguage(fromEnvironment());
    return true;
}

std::string availableLanguageCodes() {
    std::string out;
    for (const std::string& code : core::i18n::availableLanguages()) {
        if (!out.empty()) out += " / ";   // 版式碎片，与 kHintKnownCmds 的「a / b / c」同形，不上提常量
        out += code;
    }
    return out;
}

} // namespace idc::cli::i18n
