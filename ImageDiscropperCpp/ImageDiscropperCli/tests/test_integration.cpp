// ============================================================================
// 文件：tests/test_integration.cpp
// 作用：集成测试 IT-1~IT-21（guideline §9.2）——进程内直接调用 cliMain，端到端验证
//       三层命令 + config + 全局选项的「退出码 / 输出尺寸 / 像素落位 / 错误文本」。
//       不启动子进程（链接 idc_cli_lib），用 rdbuf 重定向捕获 stdout/stderr。
// 分块依据：一测试关注点一文件（严禁上帝文件）；复用共享 CHECK 宏（cli_test_harness.h）。
// 说明：测试图为程序化生成的 200×150 四象限纯色图（每象限 100×75，恰好对齐 --grid
//       0,0,100,75 的 2×2 网格），写入系统临时目录，结束时清理——不污染仓库、不依赖网络（§9.3）。
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
#include "engine/image_io.h"          // readImageFile / writeImageFile
#include "engine/region.h"            // RectRegion

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
    std::ostringstream outBuf;
    std::ostringstream errBuf;
    std::streambuf* oldOut = std::cout.rdbuf(outBuf.rdbuf());
    std::streambuf* oldErr = std::cerr.rdbuf(errBuf.rdbuf());
    const int code = idc::cli::cliMain(args);
    std::cout.rdbuf(oldOut); // 恢复（务必在断言前恢复，避免 CHECK 的输出被吞）。
    std::cerr.rdbuf(oldErr);
    return Captured{code, outBuf.str(), errBuf.str()};
}

// 生成测试图：200×150，四象限纯色（左上红 / 右上绿 / 左下蓝 / 右下黄），每象限 100×75。
Image makeTestImage() {
    Image img(200, 150, idc::core::ImageFormat::RGBA);
    const Color yellow(255, 255, 0, 255);
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
    if (!idc::engine::readImageFile(path, im)) return false;
    w = im.width();
    h = im.height();
    return true;
}

} // namespace

// 集成测试段：IT-1~IT-21。
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
    CHECK(idc::engine::writeImageFile(in, img, idc::engine::ExportFormat::PNG, 90, idc::core::kTransparent));

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
            Image sub;
            if (idc::engine::readImageFile(e.path().string(), sub)) {
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
        CHECK(idc::engine::readImageFile(out, o));
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
        CHECK(idc::engine::readImageFile(out, o));
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
        CHECK(idc::engine::readImageFile(out, o));
        CHECK(o.width() == 200 && o.height() == 75);
        CHECK(isYellow(o.getPixel(50, 37))); // 左格 = cell3（黄）
        CHECK(isRed(o.getPixel(150, 37)));   // 右格 = cell0（红）
    }

    // ---- IT-12：config --load 执行（配置等价于 IT-5）→ 100×70，参数全部生效。----
    {
        EngineConfig cfg;
        cfg.cut.tier = idc::engine::Tier::L2;
        cfg.cut.generator = idc::engine::CutGenerator::RECT;
        cfg.cut.polarity = idc::engine::Polarity::REMOVE;
        cfg.cut.rect = idc::engine::RectRegion(50, 40, 150, 120);
        cfg.emitParams.mode = idc::engine::EmitMode::MERGED;
        cfg.emitParams.layout = idc::engine::MergeLayout::COLLAPSE;
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
        CHECK(r.out.find("config") != std::string::npos);
    }

    // ---- IT-15：缺少必填（extract 无几何参数）→ 退出码 1，stderr 有 §5.2 错误前缀。----
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
    // 说明：合并导出现已自动创建父目录（Core 修复），故改用“父路径是文件”触发 E-8。
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
        cfg.cut.tier = idc::engine::Tier::L3;
        cfg.cut.generator = idc::engine::CutGenerator::GRID;
        cfg.cut.polarity = idc::engine::Polarity::REMOVE;
        cfg.cut.grid.originX = 0;
        cfg.cut.grid.originY = 0;
        cfg.cut.grid.cellWidth = 100;
        cfg.cut.grid.cellHeight = 75;
        cfg.cut.grid.remainder = idc::engine::RemainderPolicy::DISCARD;
        cfg.selectedCells.push_back(0); // 仅删 cell0 → 保留 {1,2,3} 不构成整行/整列补集
        cfg.emitParams.mode = idc::engine::EmitMode::MERGED;
        cfg.emitParams.layout = idc::engine::MergeLayout::COLLAPSE;
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
        CHECK(idc::engine::readImageFile(out, o));
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
        CHECK(idc::engine::readImageFile(out, o));
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
        CHECK(r.err.find("L3") != std::string::npos);
    }

    // ---- 清理临时目录（不污染仓库 / 系统临时区）。----
    fs::remove_all(root, ec);

    std::cout << "[cli-integration] OK\n";
}
