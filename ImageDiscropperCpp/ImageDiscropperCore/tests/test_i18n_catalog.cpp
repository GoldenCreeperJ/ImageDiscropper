// ============================================================================
// 文件：tests/test_i18n_catalog.cpp
// 作用：三层「常量层 ↔ 英文译文表」的目录对账——孤儿键 / 重复键 / 跨层撞键 / 契约组越界 /
//       译文与源值 %N 占位符集合不一致 五类静默失效模式在此变成 CI 红灯，并打印各组覆盖率
//       （未译不 fail，允许逐批补全）。
// 架构定位：属 Core 的测试段（零 Qt，只读源码文本，不链接任何界面库）。之所以放在 Core 侧，
//       是因为对账要同时看 GUI / CLI / Core 三层，而 unit_tests 是唯一三者都可见的目标。
// 消费方：test_main.cpp 的 main()（在 testI18nCore 之前调度；本段不改进程级语言态）。
// 边界：路径由 CMake 注入的 IDC_TEXTS_SOURCE_ROOT 拼出，指向**源码树**而非构建产物——
//       若某天目录改名，本用例会以「组缺失」失败而非静默通过（每组都要求非空）。
//       解析器只认本工程三个头文件的既定排版（namespace 单行开启、} 单行关闭、
//       常量一行一名、R"IDCH(...)" 多行块跳过），排版变化须同步本解析器。
//       占位符比对只扫描常量声明行 / 表条目行上的 %N（带占位符的格式串均为单行字面量，多行 R 块内的
//       %N 不参与——现状无此用法，若将来出现须扩展本解析器）。
// ============================================================================
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <optional>
#include <ranges>
#include <set>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "test_harness.h"

#ifndef IDC_TEXTS_SOURCE_ROOT
#error "test_i18n_catalog.cpp 需要 CMake 注入 IDC_TEXTS_SOURCE_ROOT（三层常量层与译文表所在源码根）"
#endif

namespace {

// 一层的清单：显示名 + 常量层头（可多个半文件）+ 译文表（可多个半 TU）+ 允许进表的组（其余组是跨版本契约，永不翻译）。
// 常量层与译文表均已按「可译 / 契约 + 分组」拆为两半，故逐层持多文件：解析时按序遍历、结果合并。
struct LayerSpec {
    std::string name;
    std::vector<std::string> layerHeaders;   // 相对 IDC_TEXTS_SOURCE_ROOT
    std::vector<std::string> tableFiles;     // 相对 IDC_TEXTS_SOURCE_ROOT
    std::vector<std::string> translatableGroups;
};

const std::vector<LayerSpec>& layers() {
    static const std::vector<LayerSpec> kLayers = {
        {"gui",
         {"ImageDiscropperGui/include/i18n/gui_texts_base.h",
          "ImageDiscropperGui/include/i18n/gui_texts_detail.h"},
         {"ImageDiscropperGui/src/i18n/lang_en.cpp",
          "ImageDiscropperGui/src/i18n/lang_en_detail.cpp"},
         {"menu", "label", "tooltip", "status", "msg"}},
        {"cli",
         {"ImageDiscropperCli/include/i18n/cli_texts_base.h",
          "ImageDiscropperCli/include/i18n/cli_texts_help.h"},
         {"ImageDiscropperCli/src/i18n/lang_en.cpp",
          "ImageDiscropperCli/src/i18n/lang_en_help.cpp"},
         {"msg", "help"}},
        {"core",
         {"ImageDiscropperCore/include/i18n/core_texts.h"},
         {"ImageDiscropperCore/src/i18n/lang_en.cpp"},
         {"msg"}},
    };
    return kLayers;
}

// 去掉行尾注释与首尾空白；只用于判定行首形态，不追求 C++ 词法完备。
std::string stripLine(const std::string& line) {
    const std::size_t comment = line.find("//");
    const std::string code = comment == std::string::npos ? line : line.substr(0, comment);
    const auto isSpace = [](const char c) { return std::isspace(static_cast<unsigned char>(c)) != 0; };
    std::size_t b = 0;
    std::size_t e = code.size();
    while (b < e && isSpace(code[b])) ++b;
    while (e > b && isSpace(code[e - 1])) --e;
    return code.substr(b, e - b);
}

// 行若开启一个命名空间，返回其叶子名（`namespace idc::core::texts::msg {` → "msg"；匿名 → 空串）。
std::optional<std::string> openedNamespace(const std::string& code) {
    if (code.rfind("namespace", 0) != 0) return std::nullopt;
    std::string rest = code.substr(std::string("namespace").size());
    const std::size_t brace = rest.find('{');
    if (brace == std::string::npos) return std::nullopt;   // 具名命名空间声明（无 {）不管
    rest = rest.substr(0, brace);
    std::erase_if(rest,
                  [](const char c) { return std::isspace(static_cast<unsigned char>(c)) != 0; });
    if (rest.rfind("inline", 0) == 0) rest = rest.substr(std::string("inline").size());
    const std::size_t sep = rest.rfind("::");
    return sep == std::string::npos ? rest : rest.substr(sep + 2);
}

// 行是否为命名空间的关闭（`}` 或 `} // namespace xxx`）。
// 不认 `};` 与 `} else {`：它们属于数组 / 函数体，当成作用域关闭会让命名空间栈错位。
bool closedScope(const std::string& code) {
    return code == "}" || code.rfind("} //", 0) == 0;
}

// 行若声明一个常量，返回其名（首个「k 开头且后随 [ 或 = 」的标识符）。
std::optional<std::string> declaredConstant(const std::string& code) {
    if (code.find("constexpr") == std::string::npos) return std::nullopt;
    std::size_t i = 0;
    while (i < code.size()) {
        const char c = code[i];
        const bool identChar = c == '_' || std::isalnum(static_cast<unsigned char>(c)) != 0;
        if (c == 'k') {
            std::size_t j = i;
            while (j < code.size()) {
                if (const char d = code[j]; !(d == '_' || std::isalnum(static_cast<unsigned char>(d)) != 0)) break;
                ++j;
            }
            std::size_t k = j;
            while (k < code.size() && std::isspace(static_cast<unsigned char>(code[k])) != 0) ++k;
            if (k < code.size() && (code[k] == '[' || code[k] == '=')) {
                if (i == 0 || !(code[i - 1] == '_' || std::isalnum(static_cast<unsigned char>(code[i - 1])) != 0)) {
                    return code.substr(i, j - i);
                }
            }
            i = j;
            continue;
        }
        if (!identChar) { ++i; continue; }
        std::size_t j = i;
        while (j < code.size() &&
               (code[j] == '_' || std::isalnum(static_cast<unsigned char>(code[j])) != 0)) ++j;
        i = j;
    }
    return std::nullopt;
}

std::string readWholeFile(const std::filesystem::path& path, bool& ok) {
    const std::ifstream in(path, std::ios::binary);
    if (!in) { ok = false; return {}; }
    std::ostringstream buf;
    buf << in.rdbuf();
    ok = true;
    return buf.str();
}

// 抽取一段文本里出现的 %N（N=1..9）占位符集合（与 i18n::formatPattern 口径一致：只认 %1..%9）。
// 用于对账「译文与源值的占位符集合是否一致」——漏写/多写会让运行时字面显示 "%1"（越界保留、不崩），
// 属静默失效，故在 CI 拦下。返回如 {"%1","%2"}。
std::set<std::string> placeholdersOf(const std::string& text) {
    std::set<std::string> out;
    for (std::size_t i = 0; i + 1 < text.size(); ++i) {
        if (text[i] == '%' && text[i + 1] >= '1' && text[i + 1] <= '9')
            out.insert(std::string(1, '%') + text[i + 1]);
    }
    return out;
}

// 常量层 → 「组::常量名」→ 该行 %N 集合。逐行推进一个命名空间栈，栈顶（非匿名）即该常量所属组。
std::map<std::string, std::set<std::string>> parseLayerKeys(const std::filesystem::path& path, bool& ok) {
    ok = false;
    std::map<std::string, std::set<std::string>> keys;
    bool readFile = false;
    const std::string text = readWholeFile(path, readFile);
    if (!readFile) return keys;
    ok = true;

    std::istringstream in(text);
    std::string line;
    std::vector<std::string> stack;   // 命名空间栈（空串＝匿名作用域）
    bool inRawBlock = false;          // R"IDCH(...)" 多行块内：内容行不参与排版判定
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (inRawBlock) {
            if (line.find(")IDCH\"") != std::string::npos) inRawBlock = false;
            continue;
        }
        const std::string code = stripLine(line);
        if (const auto leaf = openedNamespace(code)) {
            stack.push_back(*leaf);
            continue;
        }
        if (closedScope(code)) {
            if (!stack.empty()) stack.pop_back();
            continue;
        }
        const auto name = declaredConstant(code);
        if (!name) continue;
        std::string group;
        for (auto it = stack.rbegin(); it != stack.rend(); ++it) {
            if (!it->empty()) { group = *it; break; }
        }
        if (group.empty()) continue;   // 无归属组：不是可寻址常量
        keys[group + "::" + *name] = placeholdersOf(code);
        if (code.find("R\"IDCH(") != std::string::npos && code.find(")IDCH\"") == std::string::npos) {
            inRawBlock = true;
        }
    }
    return keys;
}

// 译文表 → 条目序列（键 + 该条文本的 %N 集合）。保留文件顺序，故重复键可被定位。
std::vector<std::pair<std::string, std::set<std::string>>> parseTableEntries(
    const std::filesystem::path& path, bool& ok) {
    ok = false;
    std::vector<std::pair<std::string, std::set<std::string>>> entries;
    bool readFile = false;
    const std::string text = readWholeFile(path, readFile);
    if (!readFile) return entries;
    ok = true;

    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        const std::string code = stripLine(line);
        if (code.rfind("{\"", 0) != 0) continue;
        constexpr std::size_t begin = 2;
        const std::size_t end = code.find('"', begin);
        if (end == std::string::npos) continue;
        entries.emplace_back(code.substr(begin, end - begin), placeholdersOf(code));
    }
    return entries;
}

std::string joinKeys(const std::vector<std::string>& keys, const std::size_t limit) {
    std::ostringstream os;
    for (std::size_t i = 0; i < keys.size() && i < limit; ++i) {
        if (i != 0) os << ", ";
        os << keys[i];
    }
    if (keys.size() > limit) os << " ...(truncated)";
    return os.str();
}

} // namespace

void testI18nCatalog() {
    const std::filesystem::path root(IDC_TEXTS_SOURCE_ROOT);

    // —— 纯文本纪律：CLI 帮助 / 各层消息是纯文本输出，字符串常量内不得出现 markdown 加粗（**），
    //    也不得出现「idc <command> --help」这类与位置语义矛盾的失效指引（命令后 --help 已被拒绝）。
    //    逐行扫描各层常量层与译文表，凡双引号字符串内含违禁子串即失败（注释不在此列）。——
    {
        const std::vector<std::string> forbidden = {"**", "§", "idc <command> --help", "idc <命令> --help"};
        std::vector<std::string> offenders;
        for (const auto&[name, layerHeaders, tableFiles, translatableGroups] : layers()) {
            for (const std::string& rel : layerHeaders) {
                bool ok = false;
                const std::string text = readWholeFile(root / rel, ok);
                if (!ok) continue;
                std::size_t lineNo = 0;
                std::string line;
                std::istringstream ss(text);
                while (std::getline(ss, line)) {
                    ++lineNo;
                    const std::size_t q1 = line.find('"');
                    const std::size_t q2 = line.find_last_of('"');
                    if (q1 == std::string::npos || q2 <= q1) continue;
                    for (const std::string& bad : forbidden)
                        if (line.find(bad, q1) != std::string::npos && line.find(bad) < q2) {
                            offenders.push_back(rel + ":" + std::to_string(lineNo) + " 含「" + bad + "」");
                            break;
                        }
                }
            }
            for (const std::string& rel : tableFiles) {
                bool ok = false;
                const std::string text = readWholeFile(root / rel, ok);
                if (!ok) continue;
                std::size_t lineNo = 0;
                std::string line;
                std::istringstream ss(text);
                while (std::getline(ss, line)) {
                    ++lineNo;
                    const std::size_t q1 = line.find('"');
                    const std::size_t q2 = line.find_last_of('"');
                    if (q1 == std::string::npos || q2 <= q1) continue;
                    for (const std::string& bad : forbidden)
                        if (line.find(bad, q1) != std::string::npos && line.find(bad) < q2) {
                            offenders.push_back(rel + ":" + std::to_string(lineNo) + " 含「" + bad + "」");
                            break;
                        }
                }
            }
        }
        CHECK_MSG(offenders.empty(),
                  std::string("文本常量含违禁子串（纯文本纪律）：\n  - ") +
                      [&offenders] { std::string s; for (const auto& o : offenders) s += o + "\n  - "; return s; }());
    }

    // —— 全局键归属：三层共用一张进程表，同键跨层出现＝后装者永远命中不了（静默失效）。——
    std::map<std::string, std::string> ownerOf;   // 键 → 首个登记它的一层
    std::vector<std::string> crossLayerDupes;

    for (const auto&[name, layerHeaders, tableFiles, translatableGroups] : layers()) {
        // 常量层可能拆成多个半文件：逐个解析后合并为一张「组::名 → %N」表。
        std::map<std::string, std::set<std::string>> declared;
        for (const std::string& headerRel : layerHeaders) {
            const std::filesystem::path layerPath = root / headerRel;
            bool layerOk = false;
            const auto part = parseLayerKeys(layerPath, layerOk);
            CHECK_MSG(layerOk && !part.empty(),
                      std::string("常量层读不到或解析为空: ") + layerPath.string());
            for (const auto&[fst, snd] : part) declared.emplace(fst, snd);
        }
        CHECK_MSG(!declared.empty(), std::string(name) + " 常量层解析为空");

        // 译文表可能拆成多个半 TU：按序解析后拼成条目序列（跨文件的重复键仍会被下方 seen 检出来）。
        std::vector<std::pair<std::string, std::set<std::string>>> entries;
        for (const std::string& tableRel : tableFiles) {
            const std::filesystem::path tablePath = root / tableRel;
            bool tableOk = false;
            const auto part = parseTableEntries(tablePath, tableOk);
            CHECK_MSG(tableOk, std::string("译文表读不到: ") + tablePath.string());
            entries.insert(entries.end(), part.begin(), part.end());
        }
        std::vector<std::string> keys;
        keys.reserve(entries.size());
        for (const auto &key: entries | std::views::keys) keys.push_back(key);

        // —— 契约组越界：不可译组（ident/opt/cmd/env/jsonkey/defaults/token）永不进表。——
        std::vector<std::string> forbidden;
        for (const std::string& key : keys) {
            if (const std::string group = key.substr(0, key.find("::")); std::ranges::find(translatableGroups, group)
                                                                         == translatableGroups.end()) forbidden.push_back(key);
        }
        CHECK_MSG(forbidden.empty(),
                  std::string(name) + " 表内含不可译组键: " + joinKeys(forbidden, 8));

        // —— 孤儿键：常量层里查无此 (组,名) → 该键永不命中，界面静默留在中文，必须 fail。——
        std::vector<std::string> orphans;
        for (const std::string& key : keys) {
            if (!declared.contains(key)) orphans.push_back(key);
        }
        CHECK_MSG(orphans.empty(),
                  std::string(name) + " 表内含孤儿键（常量层无此定义）: " + joinKeys(orphans, 8));

        // —— 层内重复键：installLanguage 只认先登记的一条，重复行等于白写。——
        std::set<std::string> seen;
        std::vector<std::string> dupes;
        for (const std::string& key : keys) {
            if (!seen.insert(key).second) dupes.push_back(key);
        }
        CHECK_MSG(dupes.empty(), std::string(name) + " 表内有重复键: " + joinKeys(dupes, 8));

        // —— 占位符集合一致：某键已译时，译文与源值必须携带同一组 %N（N=1..9）。
        // 漏写 / 多写会让 formatPattern 越界保留字面 "%1"（设计如此、不崩），运行时静默显示占位符——
        // 这条把该静默失效形态变红灯。孤儿键不参与（上面已拦截）；未译键（无表条目）天然不参与。——
        std::vector<std::string> phMismatch;
        for (const auto& [key, tablePh] : entries) {
            const auto it = declared.find(key);
            if (it == declared.end()) continue;
            if (it->second != tablePh) phMismatch.push_back(key);
        }
        CHECK_MSG(phMismatch.empty(),
                  std::string(name) + " 译文与源值 %N 占位符集合不一致: " + joinKeys(phMismatch, 8));

        // —— 跨层撞键：同一进程表内先到先得，后装那层的译文永不可见。——
        for (const std::string& key : keys) {
            if (const auto it = ownerOf.find(key); it != ownerOf.end()) {
                crossLayerDupes.push_back(key + "(" + it->second + "|" + name + ")");
            } else {
                ownerOf.emplace(key, name);
            }
        }

        // —— 覆盖率：逐可译组打印「已译/总数」，未译不 fail（允许渐进补全）。——
        std::cout << "  [" << name << "]";
        std::size_t totalAll = 0;
        std::size_t doneAll = 0;
        for (const std::string& group : translatableGroups) {
            std::size_t total = 0;
            std::size_t done = 0;
            for (const auto &key: declared | std::views::keys) {
                if (key.rfind(group + "::", 0) != 0) continue;
                ++total;
                if (seen.contains(key)) ++done;
            }
            // 组整体缺失说明解析器或排版脱节（而非译文没写），必须 fail。
            CHECK_MSG(total != 0, std::string(name) + " 常量层解析不到组: " + group);
            totalAll += total;
            doneAll += done;
            std::cout << " " << group << " " << done << "/" << total;
        }
        std::cout << "  => " << doneAll << "/" << totalAll
                  << " (" << (totalAll == 0 ? 0 : doneAll * 100 / totalAll) << "%)\n";
    }

    CHECK_MSG(crossLayerDupes.empty(), "跨层重复键（后装者永不可见）: " + joinKeys(crossLayerDupes, 8));

    std::cout << "[i18n-catalog] OK\n";
}
