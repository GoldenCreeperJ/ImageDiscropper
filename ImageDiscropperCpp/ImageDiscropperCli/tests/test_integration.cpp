// ============================================================================
// 文件：tests/test_integration.cpp
// 作用：集成测试 IT-1~IT-53——进程内直接调用 cliMain，端到端验证
//       三层命令 + config + 全局选项的「退出码 / 输出尺寸 / 像素落位 / 错误文本」。
//       不启动子进程（链接 idc_cli_lib），用 rdbuf 重定向捕获 stdout/stderr。
// 分块依据：一测试关注点一文件（严禁上帝文件）；复用共享 CHECK 宏（cli_test_harness.h）。
// 说明：测试图为程序化生成的 200×150 四象限纯色图（每象限 100×75，恰好对齐 --grid
//       0,0,100,75 的 2×2 网格），写入系统临时目录，结束时清理——不污染仓库、不依赖网络。
//       象限↔单元映射：cell(0,0)=index0=红、(0,1)=1=绿、(1,0)=2=蓝、(1,1)=3=黄。
// ============================================================================
#include "cli_test_harness.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <streambuf>
#include <string>
#include <system_error>
#include <vector>

#include "core/color.h"
#include "core/image.h"
#include "engine/composition.h"       // EmitMode / MergeLayout / ExportFormat
#include "engine/engine.h"            // EngineConfig / Tier / CutGenerator / Polarity / GridParams
#include "engine/engine_config_json.h" // saveEngineConfig
#include "engine/grid.h"              // RemainderPolicy
#include "io/image_io.h"              // readImageFile / writeImageFile
#include "engine/region.h"            // RectRegion
#include "canvas/layer.h"             // ImageLayer / composeCanvas（layers 段 IT）
#include "generate/pattern.h"         // PatternParams（配方层 recipe）
#include "preprocess/preprocess_config.h" // 底图色彩预处理步类型（IT-51 用 preprocess::InvertOp）

#include "commands.h" // cliMain

namespace {

namespace fs = std::filesystem;
using idc::core::Color;
using idc::core::Image;

// runCli 的捕获结果：退出码 + stdout + stderr。
struct Captured {
    int code{0};
    std::string out;
    std::string err;
};

// 进程内调用 cliMain，重定向 std::cout / std::cerr 捕获输出，调用后恢复原缓冲。
Captured runCli(const std::vector<std::string>& args) {
    const std::ostringstream outBuf;
    const std::ostringstream errBuf;
    std::streambuf* oldOut = std::cout.rdbuf(outBuf.rdbuf());
    std::streambuf* oldErr = std::cerr.rdbuf(errBuf.rdbuf());
    const int code = idc::cli::cliMain(args);
    std::cout.rdbuf(oldOut); // 恢复（务必在断言前恢复，避免 CHECK 的输出被吞）。
    std::cerr.rdbuf(oldErr);
    return Captured{code, outBuf.str(), errBuf.str()};
}

// 生成测试图：200×150，四象限纯色（左上红 / 右上绿 / 左下蓝 / 右下黄），每象限 100×75。
Image makeTestImage() {
    Image img(200, 150, idc::core::ImageFormat::Rgba);
    constexpr Color yellow(255, 255, 0, 255);
    for (int y = 0; y < 150; ++y) {
        for (int x = 0; x < 200; ++x) {
            Color c;
            if (x < 100 && y < 75) c = idc::core::kRed;
            else if (x >= 100 && y < 75) c = idc::core::kGreen;
            else if (x < 100 && y >= 75) c = idc::core::kBlue;
            else c = yellow;
            img.setPixel(x, y, c);
        }
    }
    return img;
}

// 颜色阈值判定（PNG 编解码后可能有极小误差，用阈值而非精确相等）。
bool isRed(const Color& c) { return c.r > 200 && c.g < 50 && c.b < 50; }
bool isGreen(const Color& c) { return c.g > 200 && c.r < 50 && c.b < 50; }
bool isBlue(const Color& c) { return c.b > 200 && c.r < 50 && c.g < 50; }
bool isYellow(const Color& c) { return c.r > 200 && c.g > 200 && c.b < 50; }

// 读回图像并输出其尺寸；文件不可读时返回 false。
bool readDims(const std::string& path, int& w, int& h) {
    Image im;
    if (!idc::io::readImageFile(path, im)) return false;
    w = im.width();
    h = im.height();
    return true;
}

} // namespace

// 集成测试段：IT-1~IT-53。
void testIntegration() {
    using idc::engine::EngineConfig;

    // ---- 临时工作目录与测试图（结束时清理，不污染仓库）。----
    std::error_code ec;
    const fs::path root = fs::temp_directory_path() / "idc_cli_integration_test";
    fs::remove_all(root, ec);
    fs::create_directories(root, ec);
    CHECK(fs::is_directory(root));

    const Image img = makeTestImage();
    const std::string in = (root / "input.png").string();
    CHECK(idc::io::writeImageFile(in, img, idc::engine::ExportFormat::Png, 90, idc::core::kTransparent));

    // ---- IT-1：extract --rect 50,40,150,120 → 输出 100×80（保留中心矩形，坍缩单图）。----
    {
        const std::string out = (root / "it1.png").string();
        const Captured r = runCli({"extract", "--input", in, "--rect", "50,40,150,120", "--output", out});
        CHECK(r.code == 0);
        int w = 0, h = 0;
        CHECK(readDims(out, w, h));
        CHECK(w == 100 && h == 80);
    }

    // ---- IT-2：extract --hband 40,120 → 200×80（贯穿全宽的水平带）。----
    {
        const std::string out = (root / "it2.png").string();
        const Captured r = runCli({"extract", "--input", in, "--hband", "40,120", "--output", out});
        CHECK(r.code == 0);
        int w = 0, h = 0;
        CHECK(readDims(out, w, h));
        CHECK(w == 200 && h == 80);
    }

    // ---- IT-3：extract --vband 50,150 → 100×150（贯穿全高的垂直带）。----
    {
        const std::string out = (root / "it3.png").string();
        const Captured r = runCli({"extract", "--input", in, "--vband", "50,150", "--output", out});
        CHECK(r.code == 0);
        int w = 0, h = 0;
        CHECK(readDims(out, w, h));
        CHECK(w == 100 && h == 150);
    }

    // ---- IT-4：erase --rect 50,40,150,120 --output-dir → 分离导出四角共 4 个 PNG。----
    // 四角尺寸：上两角 50×40、下两角 50×30（十字切割保留 CORNERS）。
    {
        const fs::path dir = root / "it4";
        const Captured r = runCli({"erase", "--input", in, "--rect", "50,40,150,120",
                                   "--output-dir", dir.string()});
        CHECK(r.code == 0);
        CHECK(fs::is_directory(dir));
        int count = 0, n5040 = 0, n5030 = 0;
        for (const auto& e : fs::directory_iterator(dir)) {
            if (!e.is_regular_file()) continue;
            ++count;
            if (Image sub; idc::io::readImageFile(e.path().string(), sub)) {
                if (sub.width() == 50 && sub.height() == 40) ++n5040;
                else if (sub.width() == 50 && sub.height() == 30) ++n5030;
            }
        }
        CHECK(count == 4);
        CHECK(n5040 == 2 && n5030 == 2);
    }

    // ---- IT-5：erase --rect --merge collapse → 100×70（(W−Δx)×(H−Δy)=100×70）。----
    {
        const std::string out = (root / "it5.png").string();
        const Captured r = runCli({"erase", "--input", in, "--rect", "50,40,150,120",
                                   "--merge", "collapse", "--output", out});
        CHECK(r.code == 0);
        int w = 0, h = 0;
        CHECK(readDims(out, w, h));
        CHECK(w == 100 && h == 70);
    }

    // ---- IT-6：erase --hband 40,120 --merge collapse → 200×70（删除整条横带）。----
    {
        const std::string out = (root / "it6.png").string();
        const Captured r = runCli({"erase", "--input", in, "--hband", "40,120",
                                   "--merge", "collapse", "--output", out});
        CHECK(r.code == 0);
        int w = 0, h = 0;
        CHECK(readDims(out, w, h));
        CHECK(w == 200 && h == 70);
    }

    // ---- IT-7：erase --vband 50,150 --merge collapse → 100×150（删除整条竖带）。----
    {
        const std::string out = (root / "it7.png").string();
        const Captured r = runCli({"erase", "--input", in, "--vband", "50,150",
                                   "--merge", "collapse", "--output", out});
        CHECK(r.code == 0);
        int w = 0, h = 0;
        CHECK(readDims(out, w, h));
        CHECK(w == 100 && h == 150);
    }

    // ---- IT-8：erase 多矩形并集 --merge collapse → 130×80。----
    // 切割线并集 xs{0,30,60,120,160,200} ys{0,20,50,90,130,150}；删除整列 c1(30)+c3(40)=70、
    // 整行 r1(30)+r3(40)=70，故 (200−70)×(150−70)=130×80（始终可坍缩）。
    {
        const std::string out = (root / "it8.png").string();
        const Captured r = runCli({"erase", "--input", in,
                                   "--rect", "30,20,60,50", "--rect", "120,90,160,130",
                                   "--merge", "collapse", "--output", out});
        CHECK(r.code == 0);
        int w = 0, h = 0;
        CHECK(readDims(out, w, h));
        CHECK(w == 130 && h == 80);
    }

    // ---- IT-9：grid --keep 0,0 --keep 1,1 --compose --canvas 2x1 → 200×75。----
    // 行主序放置 [cell0 红, cell3 黄]：左半红、右半黄。
    {
        const std::string out = (root / "it9.png").string();
        const Captured r = runCli({"grid", "--input", in, "--grid", "0,0,100,75",
                                   "--keep", "0,0", "--keep", "1,1",
                                   "--compose", "--canvas", "2x1", "--output", out});
        CHECK(r.code == 0);
        Image o;
        CHECK(idc::io::readImageFile(out, o));
        CHECK(o.width() == 200 && o.height() == 75);
        CHECK(isRed(o.getPixel(50, 37)));    // 左格 = cell0（红）
        CHECK(isYellow(o.getPixel(150, 37))); // 右格 = cell3（黄）
    }

    // ---- IT-10：grid --keep 全 4 --sort column-major --compose --canvas 2x2 → 200×150。----
    // 列主序序列 [0,2,1,3]：左上红(cell0)、右上蓝(cell2)、左下绿(cell1)、右下黄(cell3)。
    {
        const std::string out = (root / "it10.png").string();
        const Captured r = runCli({"grid", "--input", in, "--grid", "0,0,100,75",
                                   "--keep", "0,0", "--keep", "0,1", "--keep", "1,0", "--keep", "1,1",
                                   "--sort", "column-major",
                                   "--compose", "--canvas", "2x2", "--output", out});
        CHECK(r.code == 0);
        Image o;
        CHECK(idc::io::readImageFile(out, o));
        CHECK(o.width() == 200 && o.height() == 150);
        CHECK(isRed(o.getPixel(50, 37)));     // slot0 = cell0（红）
        CHECK(isBlue(o.getPixel(150, 37)));   // slot1 = cell2（蓝）
        CHECK(isGreen(o.getPixel(50, 112)));  // slot2 = cell1（绿）
        CHECK(isYellow(o.getPixel(150, 112))); // slot3 = cell3（黄）
    }

    // ---- IT-11：grid --sort custom --order 1,1;0,0 --compose --canvas 2x1 → 200×75。----
    // 自定义序列 [cell3 黄, cell0 红]：左半黄、右半红（与 IT-9 相反）。
    {
        const std::string out = (root / "it11.png").string();
        const Captured r = runCli({"grid", "--input", in, "--grid", "0,0,100,75",
                                   "--sort", "custom", "--order", "1,1;0,0",
                                   "--compose", "--canvas", "2x1", "--output", out});
        CHECK(r.code == 0);
        Image o;
        CHECK(idc::io::readImageFile(out, o));
        CHECK(o.width() == 200 && o.height() == 75);
        CHECK(isYellow(o.getPixel(50, 37))); // 左格 = cell3（黄）
        CHECK(isRed(o.getPixel(150, 37)));   // 右格 = cell0（红）
    }

    // ---- IT-12：config --load 执行（配置等价于 IT-5）→ 100×70，参数全部生效。----
    {
        EngineConfig cfg;
        cfg.cut.tier = idc::engine::Tier::Erase;
        cfg.cut.generator = idc::engine::CutGenerator::Rect;
        cfg.cut.polarity = idc::engine::Polarity::Remove;
        cfg.cut.rect = idc::engine::RectRegion(50, 40, 150, 120);
        cfg.emitParams.mode = idc::engine::EmitMode::Merged;
        cfg.emitParams.layout = idc::engine::MergeLayout::Collapse;
        cfg.source.width = 200;
        cfg.source.height = 150;
        const std::string cfgPath = (root / "it12.json").string();
        CHECK(idc::engine::saveEngineConfig(cfgPath, cfg));

        const std::string out = (root / "it12.png").string();
        const Captured r = runCli({"config", "--load", cfgPath, "--input", in, "--output", out});
        CHECK(r.code == 0);
        int w = 0, h = 0;
        CHECK(readDims(out, w, h));
        CHECK(w == 100 && h == 70);
    }

    // ---- IT-13：--version → stdout 形如 "idc <ver> (core <ver>)"。----
    {
        const Captured r = runCli({"--version"});
        CHECK(r.code == 0);
        CHECK(r.out.rfind("idc ", 0) == 0);                        // 以 "idc " 开头
        CHECK(r.out.find("(core ") != std::string::npos);          // 含 core 版本
        CHECK(r.out.find(')') != std::string::npos);
    }

    // ---- IT-14：--help → stdout 含全部子命令。----
    {
        const Captured r = runCli({"--help"});
        CHECK(r.code == 0);
        CHECK(r.out.find("extract") != std::string::npos);
        CHECK(r.out.find("erase") != std::string::npos);
        CHECK(r.out.find("grid") != std::string::npos);
        CHECK(r.out.find("new") != std::string::npos);
        CHECK(r.out.find("config") != std::string::npos);
    }

    // ---- IT-15：extract 缺几何参数（--rect/--hband/--vband 三选一未给）→ 退出码 1，stderr 有 `idc: error:` 错误前缀。----
    {
        const std::string out = (root / "it15.png").string();
        const Captured r = runCli({"extract", "--input", in, "--output", out});
        CHECK(r.code == 1);
        CHECK(r.err.find("idc: error:") != std::string::npos);
    }

    // ---- IT-16：输入文件不存在 → 退出码 3。----
    {
        const std::string missing = (root / "no_such_input.png").string();
        const std::string out = (root / "it16.png").string();
        const Captured r = runCli({"extract", "--input", missing, "--rect", "50,40,150,120", "--output", out});
        CHECK(r.code == 3);
    }

    // ---- IT-17：输出父路径被同名普通文件占用（合并单图无法创建父目录）→ 退出码 4。----
    // 说明：合并导出会自动创建父目录，故改用“父路径被普通文件占用”触发 E-8。
    {
        const fs::path blocker = root / "blocker";
        { std::ofstream ofs(blocker.string()); ofs << "x"; } // 制造普通文件占用父路径
        const std::string out = (blocker / "it17.png").string();
        const Captured r = runCli({"extract", "--input", in, "--rect", "50,40,150,120", "--output", out});
        CHECK(r.code == 4);
        fs::remove(blocker, ec);
    }

    // ---- IT-18：坍缩不可行 → 退出码 2，提示改用 rearrange。----
    // 手写配置：L3 网格 2×2、REMOVE 选中 cell0（孤立单元，非整行/整列）+ MERGED COLLAPSE。
    {
        EngineConfig cfg;
        cfg.cut.tier = idc::engine::Tier::Grid;
        cfg.cut.generator = idc::engine::CutGenerator::Grid;
        cfg.cut.polarity = idc::engine::Polarity::Remove;
        cfg.cut.grid.originX = 0;
        cfg.cut.grid.originY = 0;
        cfg.cut.grid.cellWidth = 100;
        cfg.cut.grid.cellHeight = 75;
        cfg.cut.grid.remainder = idc::engine::RemainderPolicy::Discard;
        cfg.selectedCells.push_back(0); // 仅删 cell0 → 保留 {1,2,3} 不构成整行/整列补集
        cfg.emitParams.mode = idc::engine::EmitMode::Merged;
        cfg.emitParams.layout = idc::engine::MergeLayout::Collapse;
        cfg.source.width = 200;
        cfg.source.height = 150;
        const std::string cfgPath = (root / "it18.json").string();
        CHECK(idc::engine::saveEngineConfig(cfgPath, cfg));

        const std::string out = (root / "it18.png").string();
        const Captured r = runCli({"config", "--load", cfgPath, "--input", in, "--output", out});
        CHECK(r.code == 2);
        CHECK(r.err.find("坍缩") != std::string::npos);
        CHECK(r.err.find("rearrange") != std::string::npos);
    }

    // ---- IT-19：grid --merge-sort column-major → 输出画布按「列优先」填充（与 --sort 选择序正交）。----
    // 选择序默认 row-major：块列表 [cell0 红, cell1 绿, cell2 蓝, cell3 黄]；
    // 列优先填充槽序 [0,2,1,3]：红→TL、绿→BL、蓝→TR、黄→BR。
    {
        const std::string out = (root / "it19.png").string();
        const Captured r = runCli({"grid", "--input", in, "--grid", "0,0,100,75",
                                   "--keep", "0,0", "--keep", "0,1", "--keep", "1,0", "--keep", "1,1",
                                   "--compose", "--canvas", "2x2", "--merge-sort", "column-major",
                                   "--output", out});
        CHECK(r.code == 0);
        Image o;
        CHECK(idc::io::readImageFile(out, o));
        CHECK(o.width() == 200 && o.height() == 150);
        CHECK(isRed(o.getPixel(50, 37)));      // TL = 块0（红）
        CHECK(isBlue(o.getPixel(150, 37)));    // TR = 块2（蓝）
        CHECK(isGreen(o.getPixel(50, 112)));   // BL = 块1（绿）
        CHECK(isYellow(o.getPixel(150, 112))); // BR = 块3（黄）
    }

    // ---- IT-20：grid --merge-decorate reverse → 输出画布填充路径整体逆序。----
    // 块列表 [红,绿,蓝,黄]；row-major + reverse 槽序 [3,2,1,0]：红→BR、绿→BL、蓝→TR、黄→TL。
    {
        const std::string out = (root / "it20.png").string();
        const Captured r = runCli({"grid", "--input", in, "--grid", "0,0,100,75",
                                   "--keep", "0,0", "--keep", "0,1", "--keep", "1,0", "--keep", "1,1",
                                   "--compose", "--canvas", "2x2", "--merge-decorate", "reverse",
                                   "--output", out});
        CHECK(r.code == 0);
        Image o;
        CHECK(idc::io::readImageFile(out, o));
        CHECK(isYellow(o.getPixel(50, 37)));   // TL = 块3（黄）
        CHECK(isBlue(o.getPixel(150, 37)));    // TR = 块2（蓝）
        CHECK(isGreen(o.getPixel(50, 112)));   // BL = 块1（绿）
        CHECK(isRed(o.getPixel(150, 112)));    // BR = 块0（红）
    }

    // ---- IT-21：erase --merge rearrange → 退出码 1（重排为 L3 专属，L2 命令直接拒绝）。----
    {
        const std::string out = (root / "it21.png").string();
        const Captured r = runCli({"erase", "--input", in, "--rect", "50,40,150,120",
                                   "--merge", "rearrange", "--output", out});
        CHECK(r.code == 1);
        CHECK(r.err.find("rearrange") != std::string::npos);
        CHECK(r.err.find("grid") != std::string::npos);
    }

    // ---- IT-22：extract --annotation 内联对象 → 标注烧进工作图（左上红区被绿色填充矩形覆盖）。----
    // 保留整图（--rect 0,0,200,150，合并单图 200×150），在左上红区 (10,10)-(40,40) 烧一个绿色填充矩形；
    // 内部点 (20,20) 应变绿，未被覆盖的 (60,60) 仍为红。
    {
        const std::string out = (root / "it22.png").string();
        const Captured r = runCli({"extract", "--input", in, "--rect", "0,0,200,150",
                                   "--annotation",
                                   R"ANN({"type":"rectangle","x1":10,"y1":10,"x2":40,"y2":40,"color":"#FF00FF00","fillColor":"#FF00FF00","fill":true,"strokeWidth":2})ANN",
                                   "--output", out});
        CHECK(r.code == 0);
        Image o;
        CHECK(idc::io::readImageFile(out, o));
        CHECK(o.width() == 200 && o.height() == 150);
        CHECK(isGreen(o.getPixel(20, 20))); // 标注内部被填充
        CHECK(isRed(o.getPixel(60, 60)));   // 未被标注覆盖，仍为原红
    }

    // ---- IT-23：extract --annotations 文件（根为数组）→ 多条标注一并烧录。----
    {
        const std::string annoFile = (root / "it23.json").string();
        {
            std::ofstream ofs(annoFile);
            ofs << R"ANN([{"type":"rectangle","x1":10,"y1":10,"x2":40,"y2":40,"fillColor":"#FF00FF00","fill":true},{"type":"rectangle","x1":150,"y1":100,"x2":190,"y2":140,"fillColor":"#FF0000FF","fill":true}])ANN";
        }
        const std::string out = (root / "it23.png").string();
        const Captured r = runCli({"extract", "--input", in, "--rect", "0,0,200,150",
                                   "--annotations", annoFile, "--output", out});
        CHECK(r.code == 0);
        Image o;
        CHECK(idc::io::readImageFile(out, o));
        CHECK(isGreen(o.getPixel(20, 20)));   // 第一条：绿
        CHECK(isBlue(o.getPixel(170, 120)));  // 第二条：蓝（右下黄区被覆盖）
    }

    // ---- IT-24：--annotation 非法 JSON → 退出码 1（参数错误，Core 段解析失败）。----
    {
        const std::string out = (root / "it24.png").string();
        const Captured r = runCli({"extract", "--input", in, "--rect", "0,0,200,150",
                                   "--annotation", "{not valid json", "--output", out});
        CHECK(r.code == 1);
        CHECK(r.err.find("标注") != std::string::npos);
    }

    // ---- IT-25：--annotation + --save-config → dry-run 序列化保留 annotations 段。----
    {
        const std::string cfgOut = (root / "it25.json").string();
        const Captured r = runCli({"--save-config", cfgOut, "extract", "--input", in,
                                   "--rect", "0,0,200,150",
                                   "--annotation",
                                   R"ANN({"type":"text","x":50,"y":50,"text":"hi","fontSize":24,"color":"#FFFFFFFF"})ANN",
                                   "--output", (root / "it25.png").string()}); // --save-config 为 dry-run：--output 仅满足参数校验，不产出图像
        CHECK(r.code == 0);
        std::ifstream ifs(cfgOut);
        const std::string text((std::istreambuf_iterator(ifs)), std::istreambuf_iterator<char>());
        CHECK(text.find("\"annotations\"") != std::string::npos); // 标注段透写不丢
        CHECK(text.find("\"text\"") != std::string::npos);        // 文字内容保留
    }

    // ---- IT-26：new --size 64x48 --pattern solid --bg "#FFFF0000" → 生成 64×48 全红图。----
    // #AARRGGBB：alpha=FF、r=FF → 不透红；回读尺寸与像素色。
    {
        const std::string out = (root / "it26.png").string();
        const Captured r = runCli({"new", "--size", "64x48", "--pattern", "solid",
                                   "--bg", "#FFFF0000", "--output", out});
        CHECK(r.code == 0);
        Image o;
        CHECK(idc::io::readImageFile(out, o));
        CHECK(o.width() == 64 && o.height() == 48);
        CHECK(isRed(o.getPixel(10, 10)));
        CHECK(isRed(o.getPixel(63, 47)));
    }

    // ---- IT-27：new 底色 alpha=00 → 全透明画布（回读 alpha==0）。----
    {
        const std::string out = (root / "it27.png").string();
        const Captured r = runCli({"new", "--size", "8x8", "--bg", "#00FFFFFF", "--output", out});
        CHECK(r.code == 0);
        Image o;
        CHECK(idc::io::readImageFile(out, o));
        CHECK(o.getPixel(4, 4).a == 0);
    }

    // ---- IT-28：生成源切割——extract --size 200x150（solid 红）--rect 50,40,150,120 → 100×80 全红。----
    // --size 替代 --input 合成画布后走既有 L1 提取路径。
    {
        const std::string out = (root / "it28.png").string();
        const Captured r = runCli({"extract", "--size", "200x150", "--pattern", "solid",
                                   "--bg", "#FFFF0000", "--rect", "50,40,150,120", "--output", out});
        CHECK(r.code == 0);
        int w = 0, h = 0;
        CHECK(readDims(out, w, h));
        CHECK(w == 100 && h == 80);
        Image o;
        CHECK(idc::io::readImageFile(out, o));
        CHECK(isRed(o.getPixel(50, 40)));
    }

    // ---- IT-29：--input 与 --size 互斥 → 退出码 1。----
    {
        const std::string out = (root / "it29.png").string();
        const Captured r = runCli({"extract", "--input", in, "--size", "200x150",
                                   "--rect", "50,40,150,120", "--output", out});
        CHECK(r.code == 1);
        CHECK(r.err.find("--size") != std::string::npos);
    }

    // ---- IT-30：new --pattern 非法名 → 退出码 1。----
    {
        const std::string out = (root / "it30.png").string();
        const Captured r = runCli({"new", "--size", "16x16", "--pattern", "bogus", "--output", out});
        CHECK(r.code == 1);
        CHECK(r.err.find("图案") != std::string::npos);
    }

    // ---- IT-31：new 缺 --output → 退出码 1；new 带 --input → 退出码 1（不接受）。----
    {
        const Captured r1 = runCli({"new", "--size", "16x16"});
        CHECK(r1.code == 1);
        const std::string out = (root / "it31.png").string();
        const Captured r2 = runCli({"new", "--size", "16x16", "--input", in, "--output", out});
        CHECK(r2.code == 1);
    }

    // ---- IT-32：new --quality 透传——合法 JPEG 质量值 code 0；越界/非整数 → code 1（错含“质量”）。----
    {
        const std::string ok = (root / "it32.jpg").string();
        const Captured r1 = runCli({"new", "--size", "16x16", "--pattern", "solid",
                                    "--bg", "#FFFF0000", "--quality", "80", "--output", ok});
        CHECK(r1.code == 0);
        const std::string bad = (root / "it32b.png").string();
        const Captured r2 = runCli({"new", "--size", "16x16", "--quality", "150", "--output", bad});
        CHECK(r2.code == 1);
        CHECK(r2.err.find("质量") != std::string::npos);
        const Captured r3 = runCli({"new", "--size", "16x16", "--quality", "abc", "--output", bad});
        CHECK(r3.code == 1);
    }

    // ---- IT-33：extract --quality 非法→退出码 1（导出选项经 applyExportOptions 统一校验）。----
    {
        const std::string out = (root / "it33.png").string();
        const Captured r = runCli({"extract", "--input", in, "--rect", "50,40,150,120",
                                   "--quality", "200", "--output", out});
        CHECK(r.code == 1);
        CHECK(r.err.find("质量") != std::string::npos);
    }

    // ---- IT-34：new --pad-color——透明画布 + JPEG 用 pad-color 压平（而非静默黑底）。----
    // 底色全透明白 #00FFFFFF，--pad-color 不透明蓝 #FF0000FF → JPEG 落盘后像素应为蓝。
    {
        const std::string out = (root / "it34.jpg").string();
        const Captured r = runCli({"new", "--size", "16x16", "--bg", "#00FFFFFF",
                                   "--format", "jpeg", "--pad-color", "#FF0000FF", "--output", out});
        CHECK(r.code == 0);
        Image o;
        CHECK(idc::io::readImageFile(out, o));
        CHECK(isBlue(o.getPixel(8, 8))); // alpha=0 全透明 → 完全落到 padColor（蓝）。
    }

    // ---- IT-35：new --save-config dry-run——不落像素，把生成配方（source+generate 段）序列化。----
    // 无 --output：--save-config 路径不要求输出图；配置应含 generate 段（kind/period/背景色）。
    {
        const std::string cfg = (root / "it35.json").string();
        const Captured r = runCli({"new", "--size", "32x24", "--pattern", "checkerboard",
                                   "--bg", "#FFFF0000", "--fg", "#FF00FF00", "--period", "8",
                                   "--save-config", cfg});
        CHECK(r.code == 0);
        std::ifstream ifs(cfg);
        const std::string text((std::istreambuf_iterator(ifs)), std::istreambuf_iterator<char>());
        CHECK(text.find("\"generate\"") != std::string::npos);
        CHECK(text.find("checkerboard") != std::string::npos);
        CHECK(text.find("\"period\": 8") != std::string::npos || text.find("\"period\":8") != std::string::npos);
    }

    // ---- IT-36：config --load → --save-config 往返——generate 段读入后再写出保持。----
    {
        const std::string cfgIn = (root / "it35.json").string();
        const std::string cfgOut = (root / "it36.json").string();
        const Captured r = runCli({"--save-config", cfgOut, "config", "--load", cfgIn});
        CHECK(r.code == 0);
        std::ifstream ifs(cfgOut);
        const std::string text((std::istreambuf_iterator(ifs)), std::istreambuf_iterator<char>());
        CHECK(text.find("\"generate\"") != std::string::npos);
        CHECK(text.find("checkerboard") != std::string::npos); // 反序列化→序列化往返不丢 kind
    }

    // ---- IT-37：config 据 generate 段生成源画布切割——无 --input/--size，由配置 generate 合成。----
    // 手写配置：source 200x150 + generate solid 红 + L1 keep rect(50,40,150,120) → 100x80 全红。
    {
        const std::string cfg = (root / "it37.json").string();
        {
            std::ofstream ofs(cfg);
            ofs << R"({
  "source": {"width": 200, "height": 150},
  "generate": {"kind": "solid", "background": "#FFFF0000", "foreground": "#FF000000"},
  "cut": {"tier": "extract", "generator": "rect", "polarity": "keep", "params": {"x1": 50, "y1": 40, "x2": 150, "y2": 120}},
  "emit": {"mode": "merged", "merge": {"layout": "collapse"}}
})";
        }
        const std::string out = (root / "it37.png").string();
        const Captured r = runCli({"config", "--load", cfg, "--output", out});
        CHECK(r.code == 0);
        int w = 0, h = 0;
        CHECK(readDims(out, w, h));
        CHECK(w == 100 && h == 80);
        Image o;
        CHECK(idc::io::readImageFile(out, o));
        CHECK(isRed(o.getPixel(50, 40)));
    }

    // ---- IT-38：总像素护栏——new --size 65535x65535（单边合法但总像素超限）→ 退出码 1。----
    {
        const std::string out = (root / "it38.png").string();
        const Captured r = runCli({"new", "--size", "65535x65535", "--pattern", "solid", "--output", out});
        CHECK(r.code == 1);
        CHECK(r.err.find("总像素") != std::string::npos);
    }

    // ---- IT-39：图案 new 冒烟——stripes/concentric/hue-gradient/noise 各产合法非空图（斜向经 stripes --orientation、方环经 concentric --orientation 表达）。----
    {
        for (const std::vector<std::string> kinds = {"stripes", "concentric", "hue-gradient", "noise"}; const std::string& k : kinds) {
            const std::string out = (root / ("it39_" + k + ".png")).string();
            const Captured r = runCli({"new", "--size", "64x48", "--pattern", k,
                                       "--bg", "#FFFFFFFF", "--fg", "#FF000000", "--seed", "7", "--output", out});
            CHECK(r.code == 0);
            int w = 0, h = 0;
            CHECK(readDims(out, w, h));
            CHECK(w == 64 && h == 48);
        }
    }

    // ---- IT-40：噪声确定性——同 seed 两次落盘字节完全一致；异 seed 字节不同。----
    {
        const auto readBytes = [](const std::string& p) {
            std::ifstream f(p, std::ios::binary);
            return std::string(std::istreambuf_iterator(f), std::istreambuf_iterator<char>());
        };
        const std::string a = (root / "it40_a.png").string();
        const std::string b = (root / "it40_b.png").string();
        const std::string c = (root / "it40_c.png").string();
        CHECK(runCli({"new", "--size", "64x64", "--pattern", "noise", "--noise", "full-color", "--seed", "1234", "--output", a}).code == 0);
        CHECK(runCli({"new", "--size", "64x64", "--pattern", "noise", "--noise", "full-color", "--seed", "1234", "--output", b}).code == 0);
        CHECK(runCli({"new", "--size", "64x64", "--pattern", "noise", "--noise", "full-color", "--seed", "9999", "--output", c}).code == 0);
        const std::string ba = readBytes(a), bb = readBytes(b), bc = readBytes(c);
        CHECK(!ba.empty());
        CHECK(ba == bb);   // 同 seed → 逐字节一致（确定性）
        CHECK(ba != bc);   // 异 seed → 内容不同
    }

    // ---- IT-41：--seed 非法（负数）/ --noise 非法名 → 退出码 1。----
    {
        const std::string out = (root / "it41.png").string();
        CHECK(runCli({"new", "--size", "16x16", "--pattern", "noise", "--seed", "-1", "--output", out}).code == 1);
        const Captured r = runCli({"new", "--size", "16x16", "--pattern", "noise", "--noise", "bogus", "--output", out});
        CHECK(r.code == 1);
        CHECK(r.err.find("噪声模式") != std::string::npos);
    }

    // ---- IT-42：图片不是图案类型——--pattern image 一律拒绝（退出码 1）；
    //      位图只能作为 config 的 layers 段图层传入（见 IT-44/45）。----
    {
        const std::string out = (root / "it42.png").string();
        const Captured r = runCli({"new", "--size", "16x16", "--pattern", "image", "--output", out});
        CHECK(r.code == 1);
        CHECK(r.err.find("不支持的图案类型") != std::string::npos);
        // --pattern-image / --image-anchor / --image-scale 亦不受理（搭配 --pattern image 仍退出码 1）。
        CHECK(runCli({"new", "--size", "16x16", "--pattern", "image", "--pattern-image", out,
                      "--image-anchor", "center", "--image-scale", "original", "--output", out}).code == 1);
    }

    // ---- IT-43：无效图案 token 拒绝 + 斜向/方环经 --orientation 表达。----
    {
        const std::string out = (root / "it43.png").string();
        // diagonal-stripes / rect-rings 非有效图案 token → 退出码 1。
        CHECK(runCli({"new", "--size", "16x16", "--pattern", "diagonal-stripes", "--output", out}).code == 1);
        CHECK(runCli({"new", "--size", "16x16", "--pattern", "rect-rings", "--output", out}).code == 1);
        // 斜向经 stripes --orientation 表达：diagonal-down/diagonal-up 合法、产非空图。
        CHECK(runCli({"new", "--size", "64x48", "--pattern", "stripes", "--orientation", "diagonal-down",
                      "--fg", "#FF000000", "--output", out}).code == 0);
        CHECK(runCli({"new", "--size", "64x48", "--pattern", "stripes", "--orientation", "diagonal-up",
                      "--fg", "#FF000000", "--output", out}).code == 0);
        // 同心方框经 concentric --orientation vertical 表达（方环）。
        CHECK(runCli({"new", "--size", "64x48", "--pattern", "concentric", "--orientation", "vertical",
                      "--fg", "#FF000000", "--output", out}).code == 0);
    }

    // ---- IT-44：多层画布 layers 段端到端（配方层）——透明底 + 整幅红色配方层，config --load 合成后切割。----
    {
        EngineConfig cfg;
        cfg.source = idc::engine::SourceInfo{40, 40};
        std::vector<idc::canvas::ImageLayer> st;
        idc::canvas::ImageLayer l;
        l.sourceKind = idc::canvas::LayerSourceKind::Recipe;
        idc::generate::PatternParams pp;
        pp.kind = idc::generate::PatternKind::Solid;
        pp.background = Color(255, 0, 0, 255);
        l.recipe = pp;
        l.transform.width = 40; l.transform.height = 40; // 铺满画布
        l.zOrder = 0;
        st = {l};
        cfg.layers = st;
        cfg.cut = idc::engine::CutConfig{};
        cfg.cut.tier = idc::engine::Tier::Standard;
        cfg.cut.generator = idc::engine::CutGenerator::Rect;
        cfg.cut.polarity = idc::engine::Polarity::Keep;
        cfg.cut.rect = idc::engine::RectRegion(0, 0, 40, 40);
        cfg.emitParams.mode = idc::engine::EmitMode::Merged;
        cfg.emitParams.layout = idc::engine::MergeLayout::Collapse;
        const std::string cfgPath = (root / "it44.json").string();
        CHECK(idc::engine::saveEngineConfig(cfgPath, cfg, nullptr));
        const std::string out = (root / "it44.png").string();
        const Captured r = runCli({"config", "--load", cfgPath, "--output", out});
        CHECK(r.code == 0);
        int w = 0, h = 0;
        CHECK(readDims(out, w, h));
        CHECK(w == 40 && h == 40);
        Image o;
        CHECK(idc::io::readImageFile(out, o));
        CHECK(isRed(o.getPixel(20, 20)));
    }

    // ---- IT-45：File 图层解码 + --bundle-assets 复制——源图写盘后由 config 目录相对名解析；打包进 <配置名>.assets/。----
    {
        fs::create_directories(root / "sub", ec);
        Image a(20, 20, idc::core::ImageFormat::Rgba);
        a.fill(Color(0, 0, 255, 255));
        const std::string asset = (root / "sub" / "a.png").string();
        CHECK(idc::io::writeImageFile(asset, a, idc::engine::ExportFormat::Png, 90, idc::core::kTransparent));

        EngineConfig cfg;
        cfg.source = idc::engine::SourceInfo{40, 40};
        std::vector<idc::canvas::ImageLayer> st;
        idc::canvas::ImageLayer l;
        l.sourceKind = idc::canvas::LayerSourceKind::File;
        l.fileName = "sub/a.png"; // 相对配置目录
        l.zOrder = 0;             // 原生尺寸 20×20，居中
        st = {l};
        cfg.layers = st;
        cfg.cut = idc::engine::CutConfig{};
        cfg.cut.tier = idc::engine::Tier::Standard;
        cfg.cut.generator = idc::engine::CutGenerator::Rect;
        cfg.cut.polarity = idc::engine::Polarity::Keep;
        cfg.cut.rect = idc::engine::RectRegion(0, 0, 40, 40);
        cfg.emitParams.mode = idc::engine::EmitMode::Merged;
        cfg.emitParams.layout = idc::engine::MergeLayout::Collapse;
        const std::string cfgPath = (root / "it45.json").string();
        CHECK(idc::engine::saveEngineConfig(cfgPath, cfg, nullptr));

        // 执行：File 层 20×20 居中落位 → 中心 (20,20) 蓝、角上 (0,0) 透明。不撑大。
        const std::string out = (root / "it45.png").string();
        const Captured r = runCli({"config", "--load", cfgPath, "--output", out});
        CHECK(r.code == 0);
        int w = 0, h = 0;
        CHECK(readDims(out, w, h));
        CHECK(w == 40 && h == 40);
        Image o;
        CHECK(idc::io::readImageFile(out, o));
        CHECK(isBlue(o.getPixel(20, 20)));
        CHECK(o.getPixel(0, 0).a == 0);

        // --bundle-assets：把 File 层源图按相对名复制进 <配置名>.assets/。
        const std::string bundleCfg = (root / "it45_bundle.json").string();
        const Captured rb = runCli({"--save-config", bundleCfg, "--bundle-assets", "config", "--load", cfgPath});
        CHECK(rb.code == 0);
        CHECK(fs::exists(root / "it45_bundle.assets" / "sub" / "a.png", ec));
    }

    // ---- IT-46：File 层越界路径（含 ..）→ config 执行时 InputError(3)。----
    {
        EngineConfig cfg;
        cfg.source = idc::engine::SourceInfo{40, 40};
        std::vector<idc::canvas::ImageLayer> st;
        idc::canvas::ImageLayer l;
        l.sourceKind = idc::canvas::LayerSourceKind::File;
        l.fileName = "../evil.png";
        l.zOrder = 0;
        st = {l};
        cfg.layers = st;
        cfg.cut = idc::engine::CutConfig{};
        cfg.cut.tier = idc::engine::Tier::Standard;
        cfg.cut.generator = idc::engine::CutGenerator::Rect;
        cfg.cut.polarity = idc::engine::Polarity::Keep;
        cfg.cut.rect = idc::engine::RectRegion(0, 0, 40, 40);
        cfg.emitParams.mode = idc::engine::EmitMode::Merged;
        cfg.emitParams.layout = idc::engine::MergeLayout::Collapse;
        const std::string cfgPath = (root / "it46.json").string();
        CHECK(idc::engine::saveEngineConfig(cfgPath, cfg, nullptr));
        const std::string out = (root / "it46.png").string();
        const Captured r = runCli({"config", "--load", cfgPath, "--output", out});
        CHECK(r.code == 3);
        CHECK(r.err.find("越界") != std::string::npos);
    }

    // ---- IT-47：图层大于画布→不撑大、合成时自然裁剪（输出尺寸恒等于 source）。----
    {
        EngineConfig cfg;
        cfg.source = idc::engine::SourceInfo{20, 20};
        std::vector<idc::canvas::ImageLayer> st;
        idc::canvas::ImageLayer l;
        l.sourceKind = idc::canvas::LayerSourceKind::Recipe;
        idc::generate::PatternParams pp;
        pp.kind = idc::generate::PatternKind::Solid;
        pp.background = Color(0, 255, 0, 255);
        l.recipe = pp;
        l.transform.width = 40; l.transform.height = 40; // 图层大于 20×20 画布
        l.zOrder = 0;
        st = {l};
        cfg.layers = st;
        cfg.cut = idc::engine::CutConfig{};
        cfg.cut.tier = idc::engine::Tier::Standard;
        cfg.cut.generator = idc::engine::CutGenerator::Rect;
        cfg.cut.polarity = idc::engine::Polarity::Keep;
        cfg.cut.rect = idc::engine::RectRegion(0, 0, 20, 20);
        cfg.emitParams.mode = idc::engine::EmitMode::Merged;
        cfg.emitParams.layout = idc::engine::MergeLayout::Collapse;
        const std::string cfgPath = (root / "it47.json").string();
        CHECK(idc::engine::saveEngineConfig(cfgPath, cfg, nullptr));
        const std::string out = (root / "it47.png").string();
        CHECK(runCli({"config", "--load", cfgPath, "--output", out}).code == 0);
        int w = 0, h = 0;
        CHECK(readDims(out, w, h));
        CHECK(w == 20 && h == 20); // 不撑大
        Image o;
        CHECK(idc::io::readImageFile(out, o));
        CHECK(isGreen(o.getPixel(10, 10)));
    }

    // ---- IT-48：层不透明度（transform.opacity 合成期 alpha 乘子）——PNG 保留 alpha。----
    {
        EngineConfig cfg;
        cfg.source = idc::engine::SourceInfo{40, 40};
        std::vector<idc::canvas::ImageLayer> st;
        idc::canvas::ImageLayer l;
        l.sourceKind = idc::canvas::LayerSourceKind::Recipe;
        idc::generate::PatternParams pp;
        pp.kind = idc::generate::PatternKind::Solid;
        pp.background = Color(255, 0, 0, 255);
        l.recipe = pp;
        l.transform.width = 40; l.transform.height = 40;
        l.transform.opacity = 90;   // 层不透明度（合成期 alpha 乘子）：Solid 红 → alpha (255·90+127)/255 = 90
        st = {l};
        cfg.layers = st;
        cfg.cut = idc::engine::CutConfig{};
        cfg.cut.tier = idc::engine::Tier::Standard;
        cfg.cut.generator = idc::engine::CutGenerator::Rect;
        cfg.cut.polarity = idc::engine::Polarity::Keep;
        cfg.cut.rect = idc::engine::RectRegion(0, 0, 40, 40);
        cfg.emitParams.mode = idc::engine::EmitMode::Merged;
        cfg.emitParams.layout = idc::engine::MergeLayout::Collapse;
        const std::string cfgPath = (root / "it48.json").string();
        CHECK(idc::engine::saveEngineConfig(cfgPath, cfg, nullptr));
        const std::string out = (root / "it48.png").string();
        CHECK(runCli({"config", "--load", cfgPath, "--output", out}).code == 0);
        Image o;
        CHECK(idc::io::readImageFile(out, o));
        CHECK(o.getPixel(20, 20).a == 90); // PNG 留 alpha
    }

    // ---- IT-49：底图同构 base（落位声明 + boxBackdrop）端到端——config 载入 + --input 供位图按落位摆入、--save-config 回写 generate 段仅落位（不存路径）。----
    {
        fs::create_directories(root / "sub49", ec);
        Image src(20, 20, idc::core::ImageFormat::Rgba);
        src.fill(Color(0, 0, 255, 255));   // 蓝色源图（底图所附图片）
        const std::string asset = (root / "sub49" / "base.png").string();
        CHECK(idc::io::writeImageFile(asset, src, idc::engine::ExportFormat::Png, 90, idc::core::kTransparent));

        EngineConfig cfg;
        cfg.source = idc::engine::SourceInfo{40, 40};
        idc::canvas::ImageLayer base;
        base.sourceKind = idc::canvas::LayerSourceKind::File;
        base.fileName = "sub49/base.png";   // 相对配置目录（位图不入配置）
        base.hasImagePlacement = true;
        base.boxWidth = 40; base.boxHeight = 40;
        base.imageFit = idc::canvas::ImageFit::Crop;
        base.imageAnchor = idc::generate::ImageAnchor::TopLeft;
        base.imagePivot = idc::generate::ImageAnchor::TopLeft;
        idc::generate::PatternParams bd;
        bd.kind = idc::generate::PatternKind::Solid;
        bd.background = Color(0, 255, 0, 255);   // 图层框底色＝绿（越源部分露出）
        base.boxBackdrop = bd;
        cfg.base = base;
        cfg.cut = idc::engine::CutConfig{};
        cfg.cut.tier = idc::engine::Tier::Standard;
        cfg.cut.generator = idc::engine::CutGenerator::Rect;
        cfg.cut.polarity = idc::engine::Polarity::Keep;
        cfg.cut.rect = idc::engine::RectRegion(0, 0, 40, 40);
        cfg.emitParams.mode = idc::engine::EmitMode::Merged;
        cfg.emitParams.layout = idc::engine::MergeLayout::Collapse;
        const std::string cfgPath = (root / "it49.json").string();
        CHECK(idc::engine::saveEngineConfig(cfgPath, cfg, nullptr));

        // 载入执行：底图为落位声明（不存路径），需 --input 供位图；蓝源 20×20 左上落位、绿底铺其余 → (5,5) 蓝、(30,30) 绿。
        const std::string out = (root / "it49.png").string();
        const Captured r = runCli({"config", "--load", cfgPath, "--input", asset, "--output", out});
        CHECK(r.code == 0);
        int w = 0, h = 0;
        CHECK(readDims(out, w, h));
        CHECK(w == 40 && h == 40);
        Image o;
        CHECK(idc::io::readImageFile(out, o));
        CHECK(isBlue(o.getPixel(5, 5)));     // 源图左上区
        CHECK(isGreen(o.getPixel(30, 30)));  // 越源区露出图层框底色

        // --save-config 回写：generate 段仅带落位键 + backdrop（底图同构序列化），**不写文件路径**（配置＝可跨输入复用的纯配方）。
        const std::string cfgOut = (root / "it49_out.json").string();
        CHECK(runCli({"--save-config", cfgOut, "config", "--load", cfgPath}).code == 0);
        std::ifstream ifs(cfgOut);
        const std::string text((std::istreambuf_iterator(ifs)), std::istreambuf_iterator<char>());
        CHECK(text.find("\"generate\"") != std::string::npos);
        CHECK(text.find("sub49/base.png") == std::string::npos);   // 文件路径不入配置
        CHECK(text.find("\"file\"") == std::string::npos);
        CHECK(text.find("imageAnchor") != std::string::npos);
        CHECK(text.find("backdrop") != std::string::npos);
        CHECK(text.find("opacity") != std::string::npos);   // generate 段携带底图整体不透明度键（缺省 255）
    }

    // ---- IT-50：底图整体不透明度端到端——Recipe 实色底设 opacity=64 → 载入现算底图（composeBase 不烘焙）+ 边界 applyAlpha
    //     淡出 → 导出像素 alpha 命中 255·64/255≈64（单一应用点、非双淡），颜色不变。----
    {
        EngineConfig cfg;
        cfg.source = idc::engine::SourceInfo{40, 40};
        idc::canvas::ImageLayer base;
        base.sourceKind = idc::canvas::LayerSourceKind::Recipe;
        idc::generate::PatternParams p;
        p.kind = idc::generate::PatternKind::Solid;
        p.background = Color(0, 0, 255, 255);   // 不透明蓝底
        base.recipe = p;
        base.transform.opacity = 64;             // 底图整体不透明度（合成期 alpha 乘子）
        cfg.base = base;
        cfg.cut = idc::engine::CutConfig{};
        cfg.cut.tier = idc::engine::Tier::Standard;
        cfg.cut.generator = idc::engine::CutGenerator::Rect;
        cfg.cut.polarity = idc::engine::Polarity::Keep;
        cfg.cut.rect = idc::engine::RectRegion(0, 0, 40, 40);
        cfg.emitParams.mode = idc::engine::EmitMode::Merged;
        cfg.emitParams.layout = idc::engine::MergeLayout::Collapse;
        const std::string cfgPath = (root / "it50.json").string();
        CHECK(idc::engine::saveEngineConfig(cfgPath, cfg, nullptr));
        const std::string out = (root / "it50.png").string();
        CHECK(runCli({"config", "--load", cfgPath, "--output", out}).code == 0);
        Image o;
        CHECK(idc::io::readImageFile(out, o));
        CHECK(isBlue(o.getPixel(20, 20)));        // 颜色不变（淡出只乘 alpha）
        CHECK(o.getPixel(20, 20).a == 64);        // 255·64/255 四舍五入≈64（单一应用点，非双淡）
    }

    // ---- IT-51：三元目标端到端——Recipe 底 + 底图色彩预处理(invert) + 底图 opacity + 画布 opacity。
    //     验证：底图色彩预处理生效（红→青、色相变）、两个边界透明度各施加一次（非任一边界双淡）、alpha 命中导出。
    //     注：色彩预处理选 invert 而非 gray——gray 转灰度会丢 alpha 通道，applyAlpha 对非 RGBA 恒等早退，无法与透明度共存验证。----
    {
        EngineConfig cfg;
        cfg.source = idc::engine::SourceInfo{40, 40};
        idc::canvas::ImageLayer base;
        base.sourceKind = idc::canvas::LayerSourceKind::Recipe;
        idc::generate::PatternParams p;
        p.kind = idc::generate::PatternKind::Solid;
        p.background = Color(255, 0, 0, 255);   // 不透明红底
        base.recipe = p;
        base.preprocess.add(idc::preprocess::InvertOp{std::string("111")});   // 底图色彩预处理：红→青（alpha 保留）
        base.transform.opacity = 180;            // 底图整体不透明度
        cfg.base = base;
        cfg.canvasOpacity = 200;                 // 画布级整体不透明度
        cfg.cut = idc::engine::CutConfig{};
        cfg.cut.tier = idc::engine::Tier::Standard;
        cfg.cut.generator = idc::engine::CutGenerator::Rect;
        cfg.cut.polarity = idc::engine::Polarity::Keep;
        cfg.cut.rect = idc::engine::RectRegion(0, 0, 40, 40);
        cfg.emitParams.mode = idc::engine::EmitMode::Merged;
        cfg.emitParams.layout = idc::engine::MergeLayout::Collapse;
        const std::string cfgPath = (root / "it51.json").string();
        CHECK(idc::engine::saveEngineConfig(cfgPath, cfg, nullptr));
        // 回写检查：底图色彩预处理与顶层 / generate 段 opacity 均入档。
        {
            std::ifstream ifs(cfgPath);
            const std::string text((std::istreambuf_iterator(ifs)), std::istreambuf_iterator<char>());
            CHECK(text.find("\"invert\"") != std::string::npos);   // generate 段 preprocess 色彩步写出
            CHECK(text.find("\"opacity\"") != std::string::npos);  // 顶层 + generate 段 opacity 键
        }
        const std::string out = (root / "it51.png").string();
        CHECK(runCli({"config", "--load", cfgPath, "--output", out}).code == 0);
        Image o;
        CHECK(idc::io::readImageFile(out, o));
        const Color px = o.getPixel(20, 20);
        CHECK(px.r < 50 && px.g > 200 && px.b > 200);   // invert 生效：红→青（色相变）
        // 两边界各乘一次：255→(·180)→180→(·200)→(180·200+127)/255=141（单一应用点、非任一边界双淡）。
        CHECK(px.a == 141);
    }

    // ---- IT-52：--lang 全局选项——已登记码放行、未知码退出码 1 并报因；
    //      其值不被误当子命令名（splitCommand 须跳过取值选项的值）。----
    {
        const auto [code, out, err] = runCli({"--lang", "fr", "--help"});
        CHECK(code == 1);
        CHECK(out.empty());                                   // 未进入帮助输出
        CHECK(err.find("无法识别的语言代码") != std::string::npos);   // 报因用切换前的源语言（此时尚未切）
        CHECK(err.find("fr") != std::string::npos);

        const Captured ok = runCli({"--lang=en", "--help"});
        CHECK(ok.code == 0);
        CHECK(ok.out.find("--lang <code>") != std::string::npos);   // 用法行恒在（选项名与占位符不译）
        // 注：CLI 英文表已全量覆盖 msg / help 两组（孤儿/重复/覆盖率由 test_i18n_catalog 断言兜住）；
        //   本用例仍只断言不可译契约串——英文散文属非契约文本，改译不该掀翻测试。

        const Captured cmd = runCli({"--lang", "en", "bogus"});
        CHECK(cmd.code == 1);
        CHECK(cmd.err.find("bogus") != std::string::npos);
        CHECK(cmd.err.find("en'") == std::string::npos);           // 不是把 "en" 当成了命令名

        // 复位：显式源语言把进程态改回中文（本用例会改全局语言态，勿给后续留英文态）。
        CHECK(runCli({"--lang", "zh-CN", "bogus"}).code == 1);
    }

    // ---- IT-53：config --load 标注段非法——详情逐条透传 Core 报因（不压成通用 schema 提示），----
    //      且与“JSON 本身语法错误”分支区分开（后者仍给通用 schema 详情与提示）。
    {
        const std::string cfg = (root / "it53.json").string();
        {
            std::ofstream ofs(cfg);
            ofs << R"({
  "source": {"width": 200, "height": 150},
  "cut": {"tier": "extract", "generator": "rect", "polarity": "keep", "params": {"x1": 10, "y1": 10, "x2": 90, "y2": 60}},
  "emit": {"mode": "merged", "merge": {"layout": "collapse"}},
  "annotations": [{"x": 10, "y": 10, "text": "no type"}]
})";
        }
        const Captured r = runCli({"config", "--load", cfg, "--output", (root / "it53.png").string()});
        CHECK(r.code == 1);
        CHECK(r.err.find("配置文件解析失败") != std::string::npos);        // brief 不变
        CHECK(r.err.find("annotations 段存在非法条目") != std::string::npos); // detail 首行来自 Core
        CHECK(r.err.find("第 1 个标注元素") != std::string::npos);          // 逐条定位信息未丢
        CHECK(r.err.find("缺少 type 字段") != std::string::npos);
        CHECK(r.err.find("无法按配置 JSON schema 解析") == std::string::npos); // 未退化为通用详情
        CHECK(r.err.find("标注须为 SPEC §7") != std::string::npos);       // 提示取标注语法

        // 对照：纯语法错误（json::parse 直接抛，根本没走到标注段校验）→ annoErr 为空，
        //   仍走通用 schema 详情与提示。
        const std::string bad = (root / "it53_bad.json").string();
        {
            std::ofstream ofs(bad);
            ofs << "{ not valid json";
        }
        const Captured g = runCli({"config", "--load", bad, "--output", (root / "it53b.png").string()});
        CHECK(g.code == 1);
        CHECK(g.err.find("无法按配置 JSON schema 解析") != std::string::npos);
        CHECK(g.err.find("annotations 段存在非法条目") == std::string::npos);
    }

    // ---- 清理临时目录（不污染仓库 / 系统临时区）。----
    fs::remove_all(root, ec);

    std::cout << "[cli-integration] OK\n";
}
