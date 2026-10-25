// ============================================================================
// 文件：tests/test_arg_parser.cpp
// 作用：单元测试 ArgParser 的「语法归类」与 parseGlobalOptions 的「全局选项扫描」
//       （参数解析必须覆盖）。只验证语法层行为，不涉及值语义（见
//       test_value_parser.cpp）与命令编排（见 test_integration.cpp）。
// 分块依据：一测试关注点一文件（严禁上帝文件）；复用共享 CHECK 宏（cli_test_harness.h）。
// 说明：覆盖 --name value / --name=value / flag / 可重复选项 / 缺值 / 位置参数 / "--" 分隔，
//       以及全局选项在任意位置出现、-h/-v 简写、--save-config 取值等。
// ============================================================================
#include "cli_test_harness.h"

#include <iostream>
#include <string>
#include <vector>

#include "arg_parser.h"

// ArgParser + parseGlobalOptions 单元测试段。
void testArgParser() {
    using namespace idc::cli;

    // ---- 取值选项：--name value 与 --name=value 两种写法。----
    {
        ArgParser p;
        p.parse({"--input", "photo.png", "--output=out.png"});
        CHECK(p.has("--input"));
        CHECK(p.get("--input") == "photo.png");
        CHECK(p.count("--input") == 1);
        CHECK(p.get("--output") == "out.png"); // 内联 '=' 形式
    }

    // ---- flag 选项：仅记录出现过（空值占位），不消费后续 token。----
    {
        ArgParser p;
        p.parse({"--compose", "--output", "x.png"});
        CHECK(p.has("--compose"));
        CHECK(p.get("--compose").empty());   // flag 值为空串
        CHECK(p.get("--output") == "x.png"); // --compose 未消费 --output
    }

    // ---- 可重复选项：保留全部值及出现顺序（--rect / --keep / --remove）。----
    {
        ArgParser p;
        p.parse({"--rect", "0,0,1,1", "--rect", "2,2,3,3", "--keep", "0,0"});
        CHECK(p.count("--rect") == 2);
        const std::vector<std::string> rects = p.getAll("--rect");
        CHECK(rects.size() == 2 && rects[0] == "0,0,1,1" && rects[1] == "2,2,3,3");
        CHECK(p.count("--keep") == 1);
    }

    // ---- 缺值：取值选项后紧跟另一个选项（以 '-' 开头）→ 记为空值。----
    {
        ArgParser p;
        p.parse({"--input", "--output", "x.png"});
        CHECK(p.has("--input"));
        CHECK(p.get("--input").empty());    // --input 缺值（下一个是 --output）
        CHECK(p.get("--output") == "x.png");
    }

    // ---- 负数值：取值选项的值以 '-' 开头但为数字（如负网格基准点）→ 正常消费。----
    {
        ArgParser p;
        p.parse({"--grid", "-5,-7,10,20", "--input", "p.png"});
        CHECK(p.get("--grid") == "-5,-7,10,20");
        CHECK(p.get("--input") == "p.png");   // 负数值之后仍正常解析选项
    }

    // ---- fallback：未出现的选项 get 返回给定默认；getAll 返回空；count 为 0。----
    {
        ArgParser p;
        p.parse({"--input", "a.png"});
        CHECK(p.get("--missing", "def") == "def");
        CHECK(p.get("--missing").empty()); // 默认 fallback 为空串
        CHECK(p.getAll("--missing").empty());
        CHECK(p.count("--missing") == 0);
        CHECK(!p.has("--missing"));
    }

    // ---- 位置参数 与 "--" 分隔符：其后一律视为位置参数（不再解析为选项）。----
    {
        ArgParser p;
        p.parse({"stray", "--input", "a.png", "--", "--notoption", "tail"});
        const std::vector<std::string>& pos = p.positional();
        CHECK(pos.size() == 3);           // "stray" + "--notoption" + "tail"
        CHECK(pos[0] == "stray");
        CHECK(pos[1] == "--notoption");   // "--" 之后不再当选项
        CHECK(pos[2] == "tail");
        CHECK(p.get("--input") == "a.png");
        CHECK(!p.has("--notoption"));     // 未被当作选项
    }

    // ---- optionsNotIn：不在给定白名单内的选项名（去重、字典序）；白名单内不报。----
    {
        ArgParser p;
        p.parse({"--input", "a.png", "--strct-assets", "--foo=1", "--foo", "--rect", "0,0,1,1"});
        const std::set<std::string> accepted{"--input", "--rect"};   // 假定本命令只接受 --input/--rect
        const std::vector<std::string> unknown = p.optionsNotIn(accepted);
        CHECK(unknown.size() == 2);                                   // --foo（两次归一）+ --strct-assets；--input/--rect 已接受
        CHECK(unknown[0] == "--foo" && unknown[1] == "--strct-assets"); // 字典序
    }

    // ---- duplicatedValueOptions：出现次数 >1 且不在「可重复」集内的选项（去重、字典序）。----
    {
        ArgParser p;
        p.parse({"--input", "a.png", "--input", "b.png", "--rect", "0,0,1,1", "--rect", "2,2,3,3"});
        const std::set<std::string> allowedRepeat{"--rect"};         // --rect 可重复，--input 不可
        const std::vector<std::string> dups = p.duplicatedValueOptions(allowedRepeat);
        CHECK(dups.size() == 1);                                      // 仅 --input 重复
        CHECK(dups[0] == "--input");
    }

    // ---- parseGlobalOptions：flag 全局选项（含 -h / -v 简写）。----
    {
        CHECK(parseGlobalOptions({"--help"}).help);
        CHECK(parseGlobalOptions({"-h"}).help);
        CHECK(parseGlobalOptions({"--version"}).version);
        CHECK(parseGlobalOptions({"-V"}).version);
        CHECK(parseGlobalOptions({"-hv"}).help && parseGlobalOptions({"-hv"}).verbose);   // 短选项簇 -abc ≡ -a -b -c
        CHECK(parseGlobalOptions({"--verbose"}).verbose);
        CHECK(parseGlobalOptions({"-v"}).verbose);               // 短写 = verbose（惯例）
        CHECK(parseGlobalOptions({"--strict"}).strict);                 // 伞形严格 flag：仅出现即置真
    }

    // ---- ArgParser 层：--save-config / --assets-dir 属作业级通用选项（写在子命令之后，由各命令读取）。----
    {
        ArgParser a;
        a.parse({"--save-config", "out.json", "--assets-dir=custom", "--assets-dir"});
        CHECK(a.get("--save-config") == "out.json");
        CHECK(a.has("--assets-dir"));
        CHECK(a.getAll("--assets-dir").front() == "custom");   // =path 记路径；flag 记空串（保序）
    }

    // ---- parseGlobalOptions：--lang 取原值不校验（语言码可用性属 cli_i18n，不在语法层）。----
    {
        CHECK(parseGlobalOptions({"--lang", "en"}).lang == "en");
        CHECK(parseGlobalOptions({"--lang=fr_FR"}).lang == "fr_FR");
        CHECK(parseGlobalOptions({"grid", "--input", "a.png", "--lang"}).lang.empty());   // 缺值 → 当作未指定
        CHECK(parseGlobalOptions({"--lang", "--verbose"}).lang.empty());   // 下一 token 是选项 → 不消费（不吞选项）
    }

    // ---- help/version 位置语义：仅命令名前（或无命令）生效；命令后不置位（交命令层拒绝）。----
    {
        CHECK(parseGlobalOptions({"--help"}).help);
        CHECK(parseGlobalOptions({"-h", "extract"}).help);
        CHECK(!parseGlobalOptions({"extract", "-h"}).help);
        CHECK(!parseGlobalOptions({"extract", "--help"}).help);
        CHECK(parseGlobalOptions({"-V"}).version);
        CHECK(parseGlobalOptions({"-hv"}).help && parseGlobalOptions({"-hv"}).verbose);   // 短选项簇 -abc ≡ -a -b -c
        CHECK(!parseGlobalOptions({"extract", "-v"}).version);
    }

    // ---- 运行修饰可出现在子命令之后的任意位置；空 argv 时全为默认。----
    {
        const GlobalOptions a = parseGlobalOptions({"extract", "--input", "a.png", "--verbose"});
        CHECK(a.verbose);
        const auto [help, version, verbose, strict, lang] = parseGlobalOptions({});
        CHECK(!help && !version && !verbose && !strict);
        CHECK(lang.empty());
    }

    std::cout << "[cli-arg-parser] OK\n";
}
