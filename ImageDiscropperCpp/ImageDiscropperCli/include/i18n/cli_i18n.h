// ============================================================================
// 文件：include/i18n/cli_i18n.h
// 作用：CLI 层的 i18n 门面——IDC_CTR 宏（查表取 std::string）+ 本层译文表装配 + 语言决议链。
// 架构定位：CLI 顶层基建（与 cli_texts.h / cli_nums.h 同族）。语言状态与查表实现在 Core
//       （三层共用一份进程表），本头只负责「键的拼法」与「CLI 特有的语言来源优先级」。
// 与 Core 的分工：本层可译分组（msg / help）的英文表住 src/i18n/lang_en.cpp，由 installCatalogs() 登记；
//       **opt / cmd 组不可译**（选项名与子命令名是脚本契约），§7 JSON 键名与 token 同样不可译，
//       这些一律保持裸常量引用（`texts::opt::kInput` 等），不经门面。
// 语言来源优先级（applyLanguage）：`--lang <code>` > 环境变量 IDC_LANG > LC_ALL > LANG > 源语言。
//       显式写了却认不出 → 返回 false，由 cli_app 报因并取退出码 1（与其余参数错误同形）；
//       来自环境变量的认不出 → 静默回退源语言（环境变量常是 "C" / "posix"，不该阻断作业）。
// ============================================================================
#pragma once

#include <string>
#include <string_view>

#include "i18n/core_i18n.h"
#include "i18n/cli_texts.h"

namespace idc::cli::i18n {

// 查表取译文；未装 / 缺键 → 回退中文源值。永不返回空。
inline std::string tr(const std::string_view key, const std::string_view fallbackUtf8) {
    return core::i18n::translate(key, fallbackUtf8);
}

// 登记本层全部内置语言的译文表（当前仅英文一表）。必须在 applyLanguage 之前调用。
// 定义在表文件 src/i18n/lang_en.cpp——译文表归本层所有，不经 image_discropper_core 传播。
void installCatalogs();

// 决议并切换语言。requested 取 GlobalOptions::lang（缺省／空串＝未显式指定，转走环境变量链）。
// 返回 false 仅当**命令行显式给了 --lang 且该码认不出**；其余情形一律成功（内部回退源语言）。
bool applyLanguage(const std::string& requested = {});

// 供报因文本枚举可用语言码（形如 "zh-CN / en"），由 --lang 的错误提示复用。
std::string availableLanguageCodes();

} // namespace idc::cli::i18n

// ---------------------------------------------------------------------------
// CLI 层门面：IDC_CTR(分组, 常量名) → std::string（可译分组只有 msg / help 两个）。
// 使用点形态：`msgs::kXxx` / `std::string(msgs::kXxx)` → `IDC_CTR(msg, kXxx)`；
//       `hp::kTextXxx` / `hp::kDescXxx` / `hp::kLeadXxx` → `IDC_CTR(help, kXxx)`（多行
//       R"IDCH(...)" 散文块整段算一个键，条目少而长）。
// 不进目录：`help::kUse*`（选项名 + 占位符，与 texts::opt 逐字配对的脚本契约）与
//       `texts::opt / cmd / env`、§7 JSON 键名 / token——这些一律保持裸常量引用。
// zh-CN 下本层表为空 → 逐字节回退常量层原文，故接线不改变中文输出。
// ---------------------------------------------------------------------------
#define IDC_CTR(group, name) \
    ::idc::cli::i18n::tr(#group "::" #name, ::idc::cli::texts::group::name)

// 带占位符的整句：先取译文，再按编号填 %N（复用 Core i18n::formatPattern，与 IDC_KTRF 同构）。
// 用于句中夹运行期值的句子（尺寸 / 计数 / 量词），避免拆成 lead/mid/tail 多段使译者无法重组语序。
// 用法：IDC_CTRF(msg, kLogBurnIn, std::to_string(n))；源语言下替换结果与直接字符串拼接逐字节相同。
#define IDC_CTRF(group, name, ...) \
    ::idc::core::i18n::formatPattern(IDC_CTR(group, name), {__VA_ARGS__})
