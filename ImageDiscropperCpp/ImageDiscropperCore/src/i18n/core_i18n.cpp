// ============================================================================
// 文件：src/i18n/core_i18n.cpp
// 作用：core_i18n.h 的实现——进程级语言状态（已登记语言清单 + 各语言目录 + 当前语言）与查表。
// 分块依据：全部状态收在本 TU 的匿名命名空间内一个 State 里，对外只经六个函数访问；
//           头文件不放任何状态（门面宏与表类型是无状态的），故下游 include 零负担。
// 说明：
//   · 目录容器选 `std::map<std::string_view, std::string_view, std::less<>>` 而非 unordered_map：
//     C++17 的 unordered_map 没有透明查找（find(string_view) 会先构造一个临时 std::string），
//     千余条目下「每次查表一次堆分配」比 O(log n) 的十次短串比较贵得多；std::map 的
//     std::less<> 是透明比较器，find 直接吃 string_view，零分配。
//   · 键与值都是静态字面量的 view，不持有所有权；表数组是 namespace 作用域的 constexpr 对象，
//     生命周期随进程，故 string_view 永挂空。
//   · active 指向 catalogs 里的某个节点：unordered_map 与 std::map 均为节点式容器，
//     增删其它元素不会使已取得的指针失效（只 erase 该节点才会，而本实现从不 erase）。
//   · 不加锁：见头文件「生命周期与线程」——登记与切换只在启动期单线程发生，其后只读。
//   · formatPattern 按**编号**取值而非顺序消耗占位符：译文里 %2 可以出现在 %1 之前（英语语序如此），
//     同一占位符也可重复；孤立 '%' 与越界编号都原样保留，故本函数不吞字、不崩，也不需要异常。
// ============================================================================
#include "i18n/core_i18n.h"

#include <algorithm>
#include <cctype>
#include <map>
#include <string>
#include <unordered_map>

namespace idc::core::i18n {
namespace {

using Catalog = std::map<std::string_view, std::string_view, std::less<>>;

// 进程级语言状态（全工程唯一一份：三层都链接本静态库）。
struct State {
    std::vector<std::string> langs{std::string(core::kSourceLanguage)};   // 可用清单，源语言恒在首
    std::unordered_map<std::string, Catalog> catalogs;                   // 语言码 → 目录（可累加装配）
    const Catalog* active = nullptr;                                     // 当前语言的目录；源语言下为空
    std::string lang{core::kSourceLanguage};                             // 当前语言码
};

// Meyers 单例：C++11 起函数局部静态量初始化线程安全，且规避静态初始化顺序问题
// （各层表文件的 install 调用发生在 main 内，必然晚于此处首次访问）。
State& state() {
    static State s;
    return s;
}

// 转小写以做大小写不敏感的语言码比对（"EN" ≡ "en"，"zh_CN" 的基标签 ≡ "zh"）。
std::string toLower(std::string_view text) {
    std::string out(text);
    std::transform(out.begin(), out.end(), out.begin(),
                   [](const unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return out;
}

} // namespace

void installLanguage(const std::string_view lang, const I18nTable table) {
    if (lang.empty()) return;
    State& s = state();
    const std::string code(lang);
    if (code == core::kSourceLanguage) return;   // 源语言不建表：它的译文就是常量层本身

    // 同语言多次 install（一层的表拆成多文件 / 多层各自装配同一语言）→ 落到同一目录累加。
    const auto [it, inserted] = s.catalogs.try_emplace(code);
    if (inserted) s.langs.push_back(code);
    for (std::size_t i = 0; i < table.count; ++i) {
        const I18nEntry& e = table.entries[i];
        if (e.key == nullptr || e.text == nullptr) continue;   // 表写坏时的兜底：跳过坏行而非崩
        it->second.emplace(e.key, e.text);                     // 重复键保留先登记的一条
    }
    // 若当前语言正是这张表，新增条目即时可见（active 指向同一节点，无需重建）。
}

std::string resolveLanguage(const std::string_view requested) {
    if (requested.empty()) return {};
    const std::string exact(requested);
    if (exact == core::kSourceLanguage) return exact;

    State& s = state();
    if (s.catalogs.find(exact) != s.catalogs.end()) return exact;

    // 大小写不敏感的整码匹配（"EN" ≡ "en"，"ZH-cn" ≡ 源语言）：语言码不区分大小写是 BCP-47 惯例。
    const std::string lower = toLower(exact);
    if (lower == toLower(core::kSourceLanguage)) return core::kSourceLanguage;
    for (const std::string& lang : s.langs) {
        if (toLower(lang) == lower) return lang;
    }

    // 基础子标签匹配：系统 locale 与终端环境变量常写作 "en_US" / "en-US.UTF-8" / "zh_CN"，
    // 而登记表用的是短码。取首个分隔符之前的段再比一次。
    const std::size_t sep = exact.find_first_of("-_");
    if (sep == std::string::npos || sep == 0) return {};
    const std::string base = lower.substr(0, sep);
    if (base.empty()) return {};
    // 中文系（zh / zh-Hans / zh_CN）一律归到源语言，不要求为它建表。
    if (base == "zh") return core::kSourceLanguage;
    for (const std::string& lang : s.langs) {
        const std::string candidate = toLower(lang);
        if (candidate == base || candidate.rfind(base + "-", 0) == 0) return lang;   // 登记表写作 "en-GB" 时也能命中
    }
    return {};
}

void setLanguage(const std::string_view lang) {
    State& s = state();
    const std::string resolved = resolveLanguage(lang);
    if (resolved.empty() || resolved == core::kSourceLanguage) {
        // 未登记 / 未知 / 源语言：目录置空，一切文案走调用点的中文源值。
        s.active = nullptr;
        s.lang = core::kSourceLanguage;
        return;
    }
    const auto it = s.catalogs.find(resolved);
    if (it == s.catalogs.end()) {   // 理论不可达（resolveLanguage 只返回已登记码），留兜底
        s.active = nullptr;
        s.lang = core::kSourceLanguage;
        return;
    }
    s.active = &it->second;
    s.lang = resolved;
}

std::string translate(const std::string_view key, const std::string_view fallbackUtf8) {
    if (const Catalog* catalog = state().active) {
        if (const auto it = catalog->find(key); it != catalog->end()) return std::string(it->second);
    }
    return std::string(fallbackUtf8);
}

std::string formatPattern(std::string text, const std::initializer_list<std::string> args) {
    // 逐字符扫：命中「% + 1..9」则按编号取 args，否则原样搬运。整句只扫一遍，无需正则与堆分配（除结果串本身）。
    std::string out;
    out.reserve(text.size() + 16);
    const std::string* slots = args.begin();   // initializer_list 背后是一段定长临时数组，本次调用内一直有效
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '%' && i + 1 < text.size() && text[i + 1] >= '1' && text[i + 1] <= '9' &&
            static_cast<std::size_t>(text[i + 1] - '1') < args.size()) {
            out += slots[static_cast<std::size_t>(text[i + 1] - '1')];
            ++i;                     // 跳过已消费的编号位
            continue;
        }
        out += text[i];
    }
    return out;
}

std::string language() { return state().lang; }

std::vector<std::string> availableLanguages() { return state().langs; }

} // namespace idc::core::i18n
