// ============================================================================
// 文件：tests/test_arg_parser.cpp
// 作用：单元测试 ArgParser 的「语法归类」与 parseGlobalOptions 的「全局选项扫描」
//       （guideline §9.1：参数解析必须覆盖）。只验证语法层行为，不涉及值语义（见
//       test_value_parser.cpp）与命令编排（见 test_integration.cpp）。
// 分块依据：一测试关注点一文件（严禁上帝文件）；复用共享 CHECK 宏（cli_test_harness.h）。
// 说明：覆盖 --name value / --name=value / flag / 可重复选项 / 缺值 / 位置参数 / "--" 分隔，
//       以及全局选项在任意位置出现、-h/-v 简写、--config/--save-config 取值等。
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

    // ---- parseGlobalOptions：flag 全局选项（含 -h / -v 简写）。----
    {
        CHECK(parseGlobalOptions({"--help"}).help);
        CHECK(parseGlobalOptions({"-h"}).help);
        CHECK(parseGlobalOptions({"--version"}).version);
        CHECK(parseGlobalOptions({"-v"}).version);
        const GlobalOptions d = parseGlobalOptions({"--verbose", "--quiet"});
        CHECK(d.verbose && d.quiet);
    }

    // ---- parseGlobalOptions：--config / --save-config 取值（空格 与 '=' 两种）。----
    {
        CHECK(parseGlobalOptions({"--config", "my.json"}).config == "my.json");
        CHECK(parseGlobalOptions({"--config=eq.json"}).config == "eq.json");
        CHECK(parseGlobalOptions({"--save-config", "out.json"}).saveConfig == "out.json");
        CHECK(parseGlobalOptions({"--save-config=so.json"}).saveConfig == "so.json");
    }

    // ---- 全局选项可出现在子命令之后的任意位置；空 argv 时全为默认。----
    {
        const GlobalOptions a = parseGlobalOptions({"extract", "--input", "a.png", "--verbose"});
        CHECK(a.verbose && a.config.empty());
        const auto [help, version, verbose, quiet, config, saveConfig] = parseGlobalOptions({});
        CHECK(!help && !version && !verbose && !quiet);
        CHECK(config.empty() && saveConfig.empty());
    }

    std::cout << "[cli-arg-parser] OK\n";
}
