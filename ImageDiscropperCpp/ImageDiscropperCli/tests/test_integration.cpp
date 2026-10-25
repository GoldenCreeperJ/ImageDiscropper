// ============================================================================
// 文件：tests/test_integration.cpp
// 作用：集成测试 IT-1~IT-60——进程内直接调用 cliMain，端到端验证
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
#include "engine/composition.h"       // ExportMode / MergeLayout / ExportFormat
#include "engine/engine.h"            // EngineConfig / Tier / CutGenerator / Polarity / GridParams
#include "engine/engine_config_json.h" // saveEngineConfig
#include "engine/grid.h"              // RemainderPolicy
#include "io/image_io.h"              // readImageFile / writeImageFile
#include "engine/region.h"            // RectRegion
#include "annotation/annotation_set.h" // AnnotationSet（--save-config 往返段 IT）
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

// 集成测试段：IT-1~IT-60。
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
        cfg.exportParams.mode = idc::engine::ExportMode::Merged;
        cfg.exportParams.layout = idc::engine::MergeLayout::Collapse;
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

    // ---- IT-14.5：--help 目标分派——命令 → 命令帮助；主题 → 主题页；未知目标退出码 1。----
    {
        const Captured a1 = runCli({"--help", "annotations"});
        CHECK(a1.code == 0 && a1.out.find("--annotation") != std::string::npos);
        const Captured a2 = runCli({"--help", "layers"});
        CHECK(a2.code == 0 && a2.out.find("--layer") != std::string::npos);
        const Captured a3 = runCli({"--help", "generate"});
        CHECK(a3.code == 0 && a3.out.find("--size") != std::string::npos);   // 生成源页保留 --size 语义（图案参数与矩阵指向 new 页）
        const Captured cmd = runCli({"--help", "extract"});
        CHECK(cmd.code == 0 && cmd.out.find("--rect") != std::string::npos);   // 命令名 → 命令帮助
        const Captured bad = runCli({"--help", "nope"});
        CHECK(bad.code == 1 && bad.err.find("annotations") != std::string::npos);
        const Captured after = runCli({"extract", "--help"});   // 命令后 -h/--help → 本命令不接受（1）
        CHECK(after.code == 1);
        const Captured before = runCli({"--help", "extract"});  // 命令前 → 命令帮助
        CHECK(before.code == 0 && before.out.find("--rect") != std::string::npos);
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
        cfg.exportParams.mode = idc::engine::ExportMode::Merged;
        cfg.exportParams.layout = idc::engine::MergeLayout::Collapse;
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
        const Captured r = runCli({"extract", "--input", in, "--save-config", cfgOut,
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

    // ---- IT-29：--input 与 --size 同给 → 画布尺寸由 --size 定，位图居中铺入（原生尺寸；越界裁、不足露底）。----
    {
        const std::string out = (root / "it29.png").string();
        const Captured r = runCli({"extract", "--input", in, "--size", "300x200",
                                   "--rect", "0,0,300,200", "--output", out});
        CHECK(r.code == 0);
        Image o;
        CHECK(idc::io::readImageFile(out, o));
        CHECK(o.width() == 300 && o.height() == 200);
        CHECK(o.getPixel(10, 10) == Color(255, 255, 255, 255));   // 画布左上留默认白底
        CHECK(isRed(o.getPixel(60, 35)));                         // 位图内容居中（偏移 (50,25)：画布 (60,35) → 位图 (10,10) 红）
        CHECK(o.getPixel(250, 180) == Color(255, 255, 255, 255)); // 右下超出位图 → 露默认白底
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
        const Captured r = runCli({"config", "--load", cfgIn, "--save-config", cfgOut});
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
  "export": {"mode": "merged", "merge": {"layout": "collapse"}}
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
        // --pattern-image / --image-anchor / --image-scale 不存在：按未知选项拒绝（退出码 1）。
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
        cfg.exportParams.mode = idc::engine::ExportMode::Merged;
        cfg.exportParams.layout = idc::engine::MergeLayout::Collapse;
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

    // ---- IT-45：File 图层解码 + --assets-dir 打包——源图写盘后由 config 目录相对名解析；打包进 <配置名>.assets/。----
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
        l.fileName = "sub/a.png"; // 相对配置目录
        l.zOrder = 0;             // 原生尺寸 20×20，居中
        st = {l};
        cfg.layers = st;
        cfg.cut = idc::engine::CutConfig{};
        cfg.cut.tier = idc::engine::Tier::Standard;
        cfg.cut.generator = idc::engine::CutGenerator::Rect;
        cfg.cut.polarity = idc::engine::Polarity::Keep;
        cfg.cut.rect = idc::engine::RectRegion(0, 0, 40, 40);
        cfg.exportParams.mode = idc::engine::ExportMode::Merged;
        cfg.exportParams.layout = idc::engine::MergeLayout::Collapse;
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

        // --assets-dir（flag）：把 File 层源图按相对名复制进 <配置名>.assets/。
        const std::string bundleCfg = (root / "it45_bundle.json").string();
        const Captured rb = runCli({"config", "--load", cfgPath, "--save-config", bundleCfg, "--assets-dir"});
        CHECK(rb.code == 0);
        CHECK(fs::exists(root / "it45_bundle.assets" / "sub" / "a.png", ec));
    }

    // ---- IT-45.5：--assets-dir 三态——=path 打包 + 写 assetsDir 字段 + 同路径幂等跳过 + 加载侧 =path 插查找链最前。----
    {
        const fs::path d = root / "it455";
        fs::create_directories(d / "sub", ec);
        fs::create_directories(d / "ov" / "sub", ec);
        Image red(20, 20, idc::core::ImageFormat::Rgba);
        red.fill(Color(255, 0, 0, 255));
        Image green(20, 20, idc::core::ImageFormat::Rgba);
        green.fill(Color(0, 255, 0, 255));
        CHECK(idc::io::writeImageFile((d / "sub" / "a.png").string(), red, idc::engine::ExportFormat::Png, 90, idc::core::kTransparent).has_value());
        CHECK(idc::io::writeImageFile((d / "ov" / "sub" / "a.png").string(), green, idc::engine::ExportFormat::Png, 90, idc::core::kTransparent).has_value());

        EngineConfig cfg;
        cfg.source = idc::engine::SourceInfo{40, 40};
        idc::canvas::ImageLayer l;
        l.fileName = "sub/a.png";
        cfg.layers = std::vector{l};
        cfg.cut = idc::engine::CutConfig{};
        cfg.cut.tier = idc::engine::Tier::Standard;
        cfg.cut.generator = idc::engine::CutGenerator::Rect;
        cfg.cut.polarity = idc::engine::Polarity::Keep;
        cfg.cut.rect = idc::engine::RectRegion(0, 0, 40, 40);
        cfg.exportParams.mode = idc::engine::ExportMode::Merged;
        cfg.exportParams.layout = idc::engine::MergeLayout::Collapse;
        const std::string cfgPath = (d / "it455.json").string();
        CHECK(idc::engine::saveEngineConfig(cfgPath, cfg, nullptr));

        // =path 打包：输出到 custom/，保存配置目录之内 → 写 assetsDir 字段。
        const std::string outCfg = (d / "out.json").string();
        const Captured r1 = runCli({"config", "--load", cfgPath, "--save-config", outCfg, "--assets-dir=custom"});
        CHECK(r1.code == 0);
        CHECK(fs::exists(d / "custom" / "sub" / "a.png", ec));
        EngineConfig back;
        CHECK(idc::engine::loadEngineConfig(outCfg, back).has_value());
        CHECK(back.assetsDir == "custom");

        // 同路径幂等：再打包一次 src==dst 跳过（不报自复制错误）。
        const Captured r2 = runCli({"config", "--load", cfgPath, "--save-config", outCfg, "--assets-dir=custom"});
        CHECK(r2.code == 0);

        // 加载侧 =path 插最前：ov/sub/a.png（绿）盖过 custom/sub/a.png（红）。
        const std::string png = (d / "it455.png").string();
        const Captured r3 = runCli({"config", "--load", outCfg, "--assets-dir=ov", "--output", png});
        CHECK(r3.code == 0);
        Image o;
        CHECK(idc::io::readImageFile(png, o));
        CHECK(isGreen(o.getPixel(20, 20)));
    }

    // ---- IT-46.5：命令行 --layer/--layers 追加到 layers 段（config 专属；zOrder 未给自增续尾；非法 JSON 退出码 1）----
    {
        EngineConfig cfg;
        cfg.source = idc::engine::SourceInfo{40, 40};
        idc::canvas::ImageLayer own;
        own.recipe = idc::generate::PatternParams{};
        own.recipe->kind = idc::generate::PatternKind::Solid;
        own.recipe->background = Color(255, 0, 0, 255);
        own.zOrder = 5;
        cfg.layers = std::vector{own};
        cfg.cut = idc::engine::CutConfig{};
        cfg.cut.tier = idc::engine::Tier::Standard;
        cfg.cut.generator = idc::engine::CutGenerator::Rect;
        cfg.cut.polarity = idc::engine::Polarity::Keep;
        cfg.cut.rect = idc::engine::RectRegion(0, 0, 40, 40);
        cfg.exportParams.mode = idc::engine::ExportMode::Merged;
        cfg.exportParams.layout = idc::engine::MergeLayout::Collapse;
        const std::string cfgPath = (root / "it465.json").string();
        CHECK(idc::engine::saveEngineConfig(cfgPath, cfg, nullptr));

        // --layers 文件：根为数组的 1 条图层（zOrder 未给）。
        const std::string layersPath = (root / "it465_layers.json").string();
        {
            std::ofstream ofs(layersPath);
            ofs << R"J([{"source":{"recipe":{"kind":"solid","background":"#FF00FF00"}}}])J";
        }
        // 内联 --layer（zOrder 未给）+ 文件 --layers：dry-run 保存后读回，验证追加与 zOrder 自增。
        const std::string outCfg = (root / "it465_out.json").string();
        const Captured r = runCli({"config", "--load", cfgPath,
                                   "--layer", R"J({"source":{"recipe":{"kind":"solid","background":"#FF0000FF"}}})J",
                                   "--layers", layersPath,
                                   "--save-config", outCfg});
        CHECK(r.code == 0);
        EngineConfig back;
        CHECK(idc::engine::loadEngineConfig(outCfg, back).has_value());
        CHECK(back.layers.has_value() && back.layers->size() == 3);
        CHECK((*back.layers)[0].zOrder == 5);
        CHECK((*back.layers)[1].zOrder == 6);   // 内联未给 zOrder → 5+1
        CHECK((*back.layers)[2].zOrder == 7);   // 文件层未给 zOrder → 续尾

        // 非法 --layer JSON → 参数错误（1）。
        const Captured bad = runCli({"config", "--load", cfgPath, "--layer", "not json", "--output",
                                     (root / "it465_x.png").string()});
        CHECK(bad.code == 1);
        CHECK(bad.err.find("图层") != std::string::npos);
    }

    // ---- IT-46.6：extract 命令行 --layer（四命令通用）——recipe 图层铺满生成画布，整图保留导出即图层色。----
    {
        const std::string out = (root / "it466.png").string();
        const Captured r = runCli({"extract", "--size", "20x20", "--rect", "0,0,20,20",
                                   "--layer", R"J({"source":{"recipe":{"kind":"solid","background":"#FF00FF00"}},"transform":{"width":20,"height":20}})J",
                                   "--output", out});
        CHECK(r.code == 0);
        Image o;
        CHECK(idc::io::readImageFile(out, o));
        CHECK(isGreen(o.getPixel(10, 10)));   // 图层色覆盖生成画布
    }

    // ---- IT-46.7：new --annotation 烧入生成画布（坐标基准＝画布尺寸）。----
    {
        const std::string out = (root / "it467.png").string();
        const Captured r = runCli({"new", "--size", "40x40", "--bg", "#FFFF0000",
                                   "--annotation",
                                   R"ANN({"type":"rectangle","x1":10,"y1":10,"x2":30,"y2":30,"color":"#FF00FF00","fillColor":"#FF00FF00","fill":true,"strokeWidth":2})ANN",
                                   "--output", out});
        CHECK(r.code == 0);
        Image o;
        CHECK(idc::io::readImageFile(out, o));
        CHECK(o.width() == 40 && o.height() == 40);
        CHECK(isGreen(o.getPixel(20, 20))); // 标注内部被填充
        CHECK(isRed(o.getPixel(35, 35)));   // 未被标注覆盖，仍为底色红
    }

    // ---- IT-46.8：new --layer（recipe 铺满）→ 图层色覆盖生成画布。----
    {
        const std::string out = (root / "it468.png").string();
        const Captured r = runCli({"new", "--size", "20x20", "--bg", "#FFFF0000",
                                   "--layer", R"J({"source":{"recipe":{"kind":"solid","background":"#FF00FF00"}},"transform":{"width":20,"height":20}})J",
                                   "--output", out});
        CHECK(r.code == 0);
        Image o;
        CHECK(idc::io::readImageFile(out, o));
        CHECK(isGreen(o.getPixel(10, 10)));   // 图层色覆盖生成画布
    }

    // ---- IT-46.9：new --save-config 往返——annotations / layers 段与 assetsDir 字段透写。----
    {
        const std::string cfgPath = (root / "it469.json").string();
        const Captured r = runCli({"new", "--size", "30x30",
                                   "--annotation", R"ANN({"type":"line","points":[[0,0],[10,10]],"color":"#FF000000"})ANN",
                                   "--layer", R"J({"source":{"recipe":{"kind":"solid","background":"#FF0000FF"}}})J",
                                   "--assets-dir=custom", "--save-config", cfgPath});
        CHECK(r.code == 0);
        EngineConfig back;
        idc::annotation::AnnotationSet backAnnos;
        std::string annoErr;
        const auto loaded = idc::engine::loadEngineConfig(cfgPath, back, &backAnnos, &annoErr);
        CHECK(loaded.has_value());
        CHECK(back.base.has_value() && back.base->recipe.has_value()); // generate 段
        CHECK(back.layers.has_value() && back.layers->size() == 1);    // layers 段
        CHECK(backAnnos.annotations().size() == 1);                    // annotations 段
        CHECK(back.assetsDir == "custom");                             // =path 相对化写字段
    }

    // ---- IT-46.10：extract --naming → 未知选项退出码 1（输出恒单图，命名模板无消费点）。----
    {
        const Captured r = runCli({"extract", "--input", in, "--rect", "0,0,200,150",
                                   "--naming", "x", "--output", (root / "it4610.png").string()});
        CHECK(r.code == 1);
        CHECK(r.err.find("--naming") != std::string::npos);
    }

    // ---- IT-46.11：--keep/--remove 分号列表（单值 r,c;r,c）≡ 多个可重复旗标；非法项 / 空列表退出码 1。----
    {
        // 单值列表 ≡ 两个旗标：分离导出 2 张。
        const std::string dirA = (root / "it4611a").string();
        const Captured r1 = runCli({"grid", "--input", in, "--grid", "0,0,100,75",
                                    "--keep", "0,0;0,1", "--output-dir", dirA});
        CHECK(r1.code == 0);
        int n = 0;
        for (const auto& e : fs::directory_iterator(dirA))
            if (e.is_regular_file()) ++n;
        CHECK(n == 2);
        // 尾随分号容忍；--remove 同形。
        const Captured r2 = runCli({"grid", "--input", in, "--grid", "0,0,100,75",
                                    "--remove", "1,0;", "--output-dir", (root / "it4611b").string()});
        CHECK(r2.code == 0);
        // 列表内非法项 → 1。
        const Captured bad1 = runCli({"grid", "--input", in, "--grid", "0,0,100,75",
                                      "--keep", "0,0;bad", "--output-dir", (root / "it4611c").string()});
        CHECK(bad1.code == 1);
        // 全空列表 → 1。
        const Captured bad2 = runCli({"grid", "--input", in, "--grid", "0,0,100,75",
                                      "--keep", ";", "--output-dir", (root / "it4611d").string()});
        CHECK(bad2.code == 1);
    }

    // ---- IT-46.12：--layer 严格校验（与标注同级）——锚点拼错 / 浮点 / opacity 越界 / 图案键拼错 → 退出码 1，报因定位到字段。----
    {
        const auto badLayer = [&](const char* layer) {
            return runCli({"extract", "--size", "20x20", "--rect", "0,0,20,20",
                           "--layer", layer, "--output", (root / "it4612.png").string()});
        };
        const Captured r1 = badLayer(R"J({"transform":{"inCanvasAnchor":"bogus"}})J");
        CHECK(r1.code == 1);
        CHECK(r1.err.find("inCanvasAnchor") != std::string::npos);
        const Captured r2 = badLayer(R"J({"zOrder":1.5})J");
        CHECK(r2.code == 1);
        CHECK(r2.err.find("zOrder") != std::string::npos);
        const Captured r3 = badLayer(R"J({"transform":{"opacity":999}})J");
        CHECK(r3.code == 1);
        const Captured r4 = badLayer(R"J({"source":{"recipe":{"kind":"bogus"}}})J");
        CHECK(r4.code == 1);
        // 合法形态不受影响：空 file（占位层）+ 负 zOrder（最底层）→ 正常执行。
        const Captured ok = badLayer(R"J({"source":{"file":""},"zOrder":-2})J");
        CHECK(ok.code == 0);
    }

    // ---- IT-46.13：--assets-dir 打包顶层 File 层——目标 .assets/ 目录须被创建（顶层资产无子目录分支）。----
    {
        Image a(16, 16, idc::core::ImageFormat::Rgba);
        a.fill(Color(255, 0, 0, 255));
        const std::string asset = (root / "topbadge.png").string();
        CHECK(idc::io::writeImageFile(asset, a, idc::engine::ExportFormat::Png, 90, idc::core::kTransparent));

        EngineConfig cfg;
        cfg.source = idc::engine::SourceInfo{40, 40};
        idc::canvas::ImageLayer l;
        l.fileName = "topbadge.png";   // 顶层资产：打包须直接落 <保存配置名>.assets/
        l.zOrder = 0;
        cfg.layers = std::vector{l};
        cfg.cut = idc::engine::CutConfig{};
        cfg.cut.tier = idc::engine::Tier::Standard;
        cfg.cut.generator = idc::engine::CutGenerator::Rect;
        cfg.cut.polarity = idc::engine::Polarity::Keep;
        cfg.cut.rect = idc::engine::RectRegion(0, 0, 40, 40);
        cfg.exportParams.mode = idc::engine::ExportMode::Merged;
        cfg.exportParams.layout = idc::engine::MergeLayout::Collapse;
        const std::string cfgPath = (root / "it4613.json").string();
        CHECK(idc::engine::saveEngineConfig(cfgPath, cfg, nullptr));

        const std::string outCfg = (root / "it4613_bundle.json").string();
        const Captured r = runCli({"config", "--load", cfgPath, "--save-config", outCfg, "--assets-dir"});
        CHECK(r.code == 0);
        CHECK(fs::exists(root / "it4613_bundle.assets" / "topbadge.png", ec));   // 顶层资产落包
    }

    // ---- IT-46.14：输出目标与 --save-config 至少其一——三命令 dry-run 可省 --output / --output-dir。----
    {
        const Captured r1 = runCli({"extract", "--input", in, "--rect", "0,0,100,75",
                                    "--save-config", (root / "it4614a.json").string()});
        CHECK(r1.code == 0);
        CHECK(fs::exists(root / "it4614a.json", ec));
        const Captured r2 = runCli({"erase", "--input", in, "--rect", "0,0,100,75", "--merge", "collapse",
                                    "--save-config", (root / "it4614b.json").string()});
        CHECK(r2.code == 0);
        const Captured r3 = runCli({"grid", "--input", in, "--grid", "0,0,100,75", "--keep", "0,0",
                                    "--compose", "--canvas", "1x1", "--save-config", (root / "it4614c.json").string()});
        CHECK(r3.code == 0);
        const Captured r4 = runCli({"grid", "--input", in, "--grid", "0,0,100,75", "--keep", "0,0",
                                    "--save-config", (root / "it4614d.json").string()});
        CHECK(r4.code == 0);
        // 两者皆无 → 1。
        const Captured bad = runCli({"extract", "--input", in, "--rect", "0,0,100,75"});
        CHECK(bad.code == 1);
    }

    // ---- IT-46.15：new --save-config 含全幅 keep cut 段 → config --load 直接复现画布。----
    {
        const std::string cfg = (root / "it4615.json").string();
        const Captured r1 = runCli({"new", "--size", "20x20", "--bg", "#FFFF0000", "--save-config", cfg});
        CHECK(r1.code == 0);
        std::ifstream ifs(cfg);
        std::ostringstream ss;
        ss << ifs.rdbuf();
        CHECK(ss.str().find("\"polarity\": \"keep\"") != std::string::npos);   // cut 段全幅保留
        const std::string out = (root / "it4615.png").string();
        const Captured r2 = runCli({"config", "--load", cfg, "--output", out});
        CHECK(r2.code == 0);
        Image o;
        CHECK(idc::io::readImageFile(out, o));
        CHECK(o.width() == 20 && o.height() == 20);
        CHECK(isRed(o.getPixel(5, 5)));   // 复现纯色画布
    }

    // ---- IT-46.16：--replace-annotations / --replace-layers 替换语义（含清空 / 移除）。----
    {
        const std::string cfg = (root / "it4616.json").string();
        const Captured r0 = runCli({"extract", "--input", in, "--rect", "0,0,100,75",
                                    "--annotation", R"ANN({"type":"line","points":[[0,0],[10,10]],"color":"#FF000000"})ANN",
                                    "--layer", R"J({"source":{"recipe":{"kind":"solid","background":"#FF0000FF"}}})J",
                                    "--save-config", cfg});
        CHECK(r0.code == 0);
        EngineConfig back;
        idc::annotation::AnnotationSet backAnnos;
        std::string annoErr;
        CHECK(idc::engine::loadEngineConfig(cfg, back, &backAnnos, &annoErr).has_value());
        CHECK(backAnnos.annotations().size() == 1);
        CHECK(back.layers.has_value() && back.layers->size() == 1);

        // 替换：各自给新条目 → 只留新条目。
        const std::string out1 = (root / "it4616b.json").string();
        const Captured r1 = runCli({"config", "--load", cfg, "--replace-annotations", "--replace-layers",
                                    "--annotation", R"ANN({"type":"circle","x1":10,"y1":10,"x2":10,"y2":15,"color":"#FF00FF00"})ANN",
                                    "--layer", R"J({"source":{"recipe":{"kind":"solid","background":"#FF00FF00"}}})J",
                                    "--save-config", out1});
        CHECK(r1.code == 0);
        EngineConfig back1;
        idc::annotation::AnnotationSet backAnnos1;
        CHECK(idc::engine::loadEngineConfig(out1, back1, &backAnnos1, &annoErr).has_value());
        CHECK(backAnnos1.annotations().size() == 1);
        CHECK(back1.layers.has_value() && back1.layers->size() == 1);

        // 替换且无命令行输入 → 标注清空、layers 段移除。
        const std::string out2 = (root / "it4616c.json").string();
        const Captured r2 = runCli({"config", "--load", cfg, "--replace-annotations", "--replace-layers",
                                    "--save-config", out2});
        CHECK(r2.code == 0);
        EngineConfig back2;
        idc::annotation::AnnotationSet backAnnos2;
        CHECK(idc::engine::loadEngineConfig(out2, back2, &backAnnos2, &annoErr).has_value());
        CHECK(backAnnos2.annotations().empty());
        CHECK(!back2.layers.has_value());

        // 默认追加不受影响：+1 条标注 → 2 条。
        const std::string out3 = (root / "it4616d.json").string();
        const Captured r3 = runCli({"config", "--load", cfg,
                                    "--annotation", R"ANN({"type":"line","points":[[0,0],[5,5]],"color":"#FF000000"})ANN",
                                    "--save-config", out3});
        CHECK(r3.code == 0);
        EngineConfig back3;
        idc::annotation::AnnotationSet backAnnos3;
        CHECK(idc::engine::loadEngineConfig(out3, back3, &backAnnos3, &annoErr).has_value());
        CHECK(backAnnos3.annotations().size() == 2);

        // flag 不吞后续裸值（isFlagOption 三处登记回归）：裸值按游离位置参数拒绝。
        const Captured stray = runCli({"config", "--load", cfg, "--replace-annotations", "stray.json",
                                       "--save-config", (root / "it4616e.json").string()});
        CHECK(stray.code == 1);
        CHECK(stray.err.find("多余参数") != std::string::npos);
    }

    // ---- IT-46.17：--input 与 --size 同给——画布尺寸由 --size 定，位图左上无位移铺入（越界裁、不足露底）。----
    {
        const std::string out = (root / "it4617.png").string();
        const Captured r = runCli({"extract", "--input", in, "--size", "300x200", "--bg", "#FFFFFFFF",
                                   "--rect", "0,0,300,200", "--output", out});
        CHECK(r.code == 0);
        Image o;
        CHECK(idc::io::readImageFile(out, o));
        CHECK(o.width() == 300 && o.height() == 200);
        CHECK(o.getPixel(10, 10) == Color(255, 255, 255, 255));   // 画布左上留 --bg 白底
        CHECK(isRed(o.getPixel(60, 35)));                         // 位图内容居中（偏移 (50,25)：(60,35) → 位图 (10,10) 红）
        CHECK(isYellow(o.getPixel(240, 165)));                    // (240,165) → 位图 (190,140) 黄
        CHECK(o.getPixel(250, 180) == Color(255, 255, 255, 255)); // 画布右下超出位图 → 露 --bg 白底
        // erase / grid 同给冒烟。
        const Captured r2 = runCli({"erase", "--input", in, "--size", "300x200", "--bg", "#FFFFFFFF",
                                    "--rect", "10,10,100,100", "--merge", "collapse", "--output", (root / "it4617b.png").string()});
        CHECK(r2.code == 0);
        const Captured r3 = runCli({"grid", "--input", in, "--size", "300x200", "--bg", "#FFFFFFFF",
                                    "--grid", "0,0,100,100", "--keep", "0,0", "--output-dir", (root / "it4617c").string()});
        CHECK(r3.code == 0);
        // 至少其一守卫保留：皆无 → 1。
        const Captured bad = runCli({"extract", "--rect", "0,0,10,10", "--output", (root / "it4617d.png").string()});
        CHECK(bad.code == 1);
        // 图案参数无 --size → 1（杜绝静默忽略）。
        const Captured bad2 = runCli({"extract", "--input", in, "--pattern", "checkerboard",
                                      "--rect", "0,0,10,10", "--output", (root / "it4617e.png").string()});
        CHECK(bad2.code == 1);
        CHECK(bad2.err.find("--size") != std::string::npos);
    }

    // ---- IT-46.18：config 字段覆盖族——CLI 值覆盖配置对应字段，--save-config 落盘与执行同样生效。----
    {
        const std::string cfg = (root / "it4618.json").string();
        const Captured r0 = runCli({"extract", "--input", in, "--rect", "0,0,100,75", "--save-config", cfg});
        CHECK(r0.code == 0);

        const std::string outCfg = (root / "it4618b.json").string();
        const Captured r1 = runCli({"config", "--load", cfg,
                                    "--tier", "erase", "--polarity", "remove",
                                    "--rect", "10,10,60,60",
                                    "--export-mode", "separate", "--format", "jpeg",
                                    "--quality", "70", "--naming", "ov_{index}",
                                    "--save-config", outCfg});
        CHECK(r1.code == 0);
        EngineConfig back;
        CHECK(idc::engine::loadEngineConfig(outCfg, back).has_value());
        CHECK(back.cut.tier == idc::engine::Tier::Erase);
        CHECK(back.cut.polarity == idc::engine::Polarity::Remove);
        CHECK(back.cut.generator == idc::engine::CutGenerator::Rect);
        CHECK(back.cut.rect.left == 10 && back.cut.rect.top == 10 && back.cut.rect.right == 60 && back.cut.rect.bottom == 60);
        CHECK(back.exportParams.mode == idc::engine::ExportMode::Separate);
        CHECK(back.exportParams.format == idc::engine::ExportFormat::Jpeg);
        CHECK(back.exportParams.quality == 70);
        CHECK(back.exportParams.naming == "ov_{index}");

        // 非法枚举值 → 1。
        const Captured bad = runCli({"config", "--load", cfg, "--tier", "bogus",
                                     "--save-config", (root / "it4618c.json").string()});
        CHECK(bad.code == 1);
        // band 覆盖与 --save-config 同用 → 1。
        const Captured bad2 = runCli({"config", "--load", cfg, "--hband", "10,20",
                                      "--save-config", (root / "it4618d.json").string()});
        CHECK(bad2.code == 1);
        // band 覆盖执行路径：横带保留 → 全幅宽 200、高 10。
        const std::string out = (root / "it4618e.png").string();
        const Captured r2 = runCli({"config", "--load", cfg, "--input", in, "--hband", "10,20", "--output", out});
        CHECK(r2.code == 0);
        Image o;
        CHECK(idc::io::readImageFile(out, o));
        CHECK(o.width() == 200 && o.height() == 10);
    }

    // ---- IT-46.19：--fit 位图→画布适应——stretch 拉伸铺满；未与 --input+--size 同给 → 1；非法值 → 1。----
    {
        const std::string outS = (root / "it4619s.png").string();
        const Captured rs = runCli({"extract", "--input", in, "--size", "200x200", "--bg", "#FFFFFFFF",
                                    "--fit", "stretch", "--rect", "0,0,200,200", "--output", outS});
        CHECK(rs.code == 0);
        Image os;
        CHECK(idc::io::readImageFile(outS, os));
        CHECK(os.width() == 200 && os.height() == 200);
        CHECK(isYellow(os.getPixel(199, 199)));   // 位图右下（黄）拉伸到画布右下
        CHECK(isRed(os.getPixel(5, 5)));          // 位图左上（红）拉伸到画布左上
        // --fit 只与 --input 同给（无 --size）→ 1。
        const Captured bad = runCli({"extract", "--input", in, "--fit", "stretch",
                                     "--rect", "0,0,100,75", "--output", (root / "it4619b.png").string()});
        CHECK(bad.code == 1);
        // 非法 fit 值 → 1。
        const Captured bad2 = runCli({"extract", "--input", in, "--size", "200x200", "--fit", "bogus",
                                      "--rect", "0,0,200,200", "--output", (root / "it4619c.png").string()});
        CHECK(bad2.code == 1);
    }

    // ---- IT-46.20：百分比回写与编辑判定——未编辑原样回写；覆盖 / 替换后不得因值巧合错写百分比。----
    {
        const std::string cfg = (root / "it4620.json").string();
        const Captured r0 = runCli({"extract", "--input", in, "--rect", "0,0,100,75", "--save-config", cfg});
        CHECK(r0.code == 0);
        // 把 x1 改成 "10%"（图宽 200 → 20px）。
        {
            std::ifstream ifs(cfg);
            std::ostringstream ss;
            ss << ifs.rdbuf();
            std::string text = ss.str();
            const std::string needle = "\"x1\": 0";
            const auto pos = text.find(needle);
            CHECK(pos != std::string::npos);
            text.replace(pos + needle.size() - 1, 1, "\"10%\"");
            std::ofstream ofs(cfg);
            ofs << text;
        }
        // 未编辑往返 → 原样回写 "10%"。
        const std::string rt1 = (root / "it4620b.json").string();
        const Captured r1 = runCli({"config", "--load", cfg, "--save-config", rt1});
        CHECK(r1.code == 0);
        std::string t1;
        {
            std::ifstream ifs(rt1);
            std::ostringstream ss;
            ss << ifs.rdbuf();
            t1 = ss.str();
        }
        CHECK(t1.find("\"x1\": \"10%\"") != std::string::npos);
        // --rect 覆盖为同值 20 → 必须固化整数（不得错写百分比）。
        const std::string rt2 = (root / "it4620c.json").string();
        const Captured r2 = runCli({"config", "--load", cfg, "--rect", "20,0,100,75", "--save-config", rt2});
        CHECK(r2.code == 0);
        std::string t2;
        {
            std::ifstream ifs(rt2);
            std::ostringstream ss;
            ss << ifs.rdbuf();
            t2 = ss.str();
        }
        CHECK(t2.find("\"x1\": 20") != std::string::npos);
        CHECK(t2.find("\"x1\": \"10%\"") == std::string::npos);
    }

    // ---- IT-46：File 层越界路径（含 ..）→ config 执行时 InputError(3)。----
    {
        EngineConfig cfg;
        cfg.source = idc::engine::SourceInfo{40, 40};
        std::vector<idc::canvas::ImageLayer> st;
        idc::canvas::ImageLayer l;
        l.fileName = "../evil.png";
        l.zOrder = 0;
        st = {l};
        cfg.layers = st;
        cfg.cut = idc::engine::CutConfig{};
        cfg.cut.tier = idc::engine::Tier::Standard;
        cfg.cut.generator = idc::engine::CutGenerator::Rect;
        cfg.cut.polarity = idc::engine::Polarity::Keep;
        cfg.cut.rect = idc::engine::RectRegion(0, 0, 40, 40);
        cfg.exportParams.mode = idc::engine::ExportMode::Merged;
        cfg.exportParams.layout = idc::engine::MergeLayout::Collapse;
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
        cfg.exportParams.mode = idc::engine::ExportMode::Merged;
        cfg.exportParams.layout = idc::engine::MergeLayout::Collapse;
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
        cfg.exportParams.mode = idc::engine::ExportMode::Merged;
        cfg.exportParams.layout = idc::engine::MergeLayout::Collapse;
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
        cfg.exportParams.mode = idc::engine::ExportMode::Merged;
        cfg.exportParams.layout = idc::engine::MergeLayout::Collapse;
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
        CHECK(runCli({"config", "--load", cfgPath, "--save-config", cfgOut}).code == 0);
        std::ifstream ifs(cfgOut);
        const std::string text((std::istreambuf_iterator(ifs)), std::istreambuf_iterator<char>());
        CHECK(text.find("\"generate\"") != std::string::npos);
        CHECK(text.find("sub49/base.png") == std::string::npos);   // 文件路径不入配置
        CHECK(text.find("\"file\"") == std::string::npos);
        CHECK(text.find("imageAnchor") != std::string::npos);
        CHECK(text.find("backdrop") != std::string::npos);
        CHECK(text.find("opacity") != std::string::npos);   // generate 段携带底图整体不透明度键（缺省 255）
        CHECK(text.find("boxWidth") == std::string::npos);   // 底图落位框尺寸不落盘（恒＝source，载入由 source 回填）
    }

    // ---- IT-50：底图整体不透明度端到端——Recipe 实色底设 opacity=64 → 载入现算底图（composeBase 不烘焙）+ 边界 applyAlpha
    //     淡出 → 导出像素 alpha 命中 255·64/255≈64（单一应用点、非双淡），颜色不变。----
    {
        EngineConfig cfg;
        cfg.source = idc::engine::SourceInfo{40, 40};
        idc::canvas::ImageLayer base;
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
        cfg.exportParams.mode = idc::engine::ExportMode::Merged;
        cfg.exportParams.layout = idc::engine::MergeLayout::Collapse;
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
        cfg.exportParams.mode = idc::engine::ExportMode::Merged;
        cfg.exportParams.layout = idc::engine::MergeLayout::Collapse;
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
  "export": {"mode": "merged", "merge": {"layout": "collapse"}},
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
        CHECK(r.err.find("标注须为标注对象 JSON") != std::string::npos);       // 提示取标注语法

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

    // ---- IT-54：File 层缺文件（默认）→ 降级占位、退出码 0，stderr 出一条 WARN 汇总（无人值守可见）。----
    //      含一层实色配方（保证画布非空）+ 一层缺失文件（合法相对名、文件不存在 → 占位分支）。
    {
        EngineConfig cfg;
        cfg.source = idc::engine::SourceInfo{40, 40};
        std::vector<idc::canvas::ImageLayer> st;
        idc::canvas::ImageLayer solid;
        idc::generate::PatternParams pp;
        pp.kind = idc::generate::PatternKind::Solid;
        pp.background = Color(255, 0, 0, 255);
        solid.recipe = pp;
        solid.transform.width = 40; solid.transform.height = 40;
        solid.zOrder = 0;
        idc::canvas::ImageLayer ghost;
        ghost.fileName = "ghost_missing.png";   // 合法相对名，但磁盘上不存在 → 缺文件分支
        ghost.zOrder = 1;
        st = {solid, ghost};
        cfg.layers = st;
        cfg.cut = idc::engine::CutConfig{};
        cfg.cut.tier = idc::engine::Tier::Standard;
        cfg.cut.generator = idc::engine::CutGenerator::Rect;
        cfg.cut.polarity = idc::engine::Polarity::Keep;
        cfg.cut.rect = idc::engine::RectRegion(0, 0, 40, 40);
        cfg.exportParams.mode = idc::engine::ExportMode::Merged;
        cfg.exportParams.layout = idc::engine::MergeLayout::Collapse;
        const std::string cfgPath = (root / "it54.json").string();
        CHECK(idc::engine::saveEngineConfig(cfgPath, cfg, nullptr));
        const std::string out = (root / "it54.png").string();
        const Captured r = runCli({"config", "--load", cfgPath, "--output", out});
        CHECK(r.code == 0);                                     // 缺文件降级占位，仍成功
        CHECK(r.err.find("idc: warn:") != std::string::npos);   // WARN 汇总走 stderr，前缀语言中性
    }

    // ---- IT-55：同一缺文件配置 + --strict → 报错退出码 3（不降级）。----
    {
        const std::string cfgPath = (root / "it54.json").string();   // 复用 IT-54 配置
        const std::string out = (root / "it55.png").string();
        const Captured r = runCli({"--strict", "config", "--load", cfgPath, "--output", out});
        CHECK(r.code == 3);
        CHECK(r.err.find("idc: error:") != std::string::npos);
    }

    // ---- IT-56：拼错的选项名（--strct）被命令层拒绝 → 退出码 1，stderr 报「无法识别的选项」（G1，杜绝静默失效）。----
    {
        const std::string out = (root / "it56.png").string();
        const Captured r = runCli({"extract", "--input", in, "--rect", "50,40,150,120",
                                   "--strct", "--output", out});
        CHECK(r.code == 1);
        CHECK(r.err.find("无法识别的选项") != std::string::npos);
        CHECK(r.err.find("--strct") != std::string::npos);
    }

    // ---- IT-57：config --input 裸图尺寸（200×150）≠ 声明 source（100×100）、无 resize 预处理。----
    //      默认：静默按交集裁切、退出码 0，但 stderr 出一条尺寸错配 WARN；--strict → 输入错误退出码 3（G2）。----
    {
        EngineConfig cfg;
        cfg.source = idc::engine::SourceInfo{100, 100};   // 声明 100×100，实际输入 200×150
        cfg.cut = idc::engine::CutConfig{};
        cfg.cut.tier = idc::engine::Tier::Standard;
        cfg.cut.generator = idc::engine::CutGenerator::Rect;
        cfg.cut.polarity = idc::engine::Polarity::Keep;
        cfg.cut.rect = idc::engine::RectRegion(0, 0, 100, 100);
        cfg.exportParams.mode = idc::engine::ExportMode::Merged;
        cfg.exportParams.layout = idc::engine::MergeLayout::Collapse;
        const std::string cfgPath = (root / "it57.json").string();
        CHECK(idc::engine::saveEngineConfig(cfgPath, cfg, nullptr));

        // 默认：裁切继续、退出码 0，出 WARN（含 source）。WARN 恒写 stderr（与 verbose 无关，与报错对偶）。
        const std::string out = (root / "it57.png").string();
        const Captured w = runCli({"config", "--load", cfgPath, "--input", in, "--output", out});
        CHECK(w.code == 0);
        CHECK(w.err.find("idc: warn:") != std::string::npos);
        CHECK(w.err.find("source") != std::string::npos);

        // --strict：尺寸错配升级为报错，退出码 3（不产出）。
        const std::string outS = (root / "it57s.png").string();
        const Captured s = runCli({"--strict", "config", "--load", cfgPath, "--input", in, "--output", outS});
        CHECK(s.code == 3);
        CHECK(s.err.find("idc: error:") != std::string::npos);
    }

    // ---- IT-58：跳命令误用选项——extract 传 grid 专属 --keep（合法选项但不属本命令）→ 退出码 1（A）。----
    {
        const std::string out = (root / "it58.png").string();
        const Captured r = runCli({"extract", "--input", in, "--rect", "50,40,150,120",
                                   "--keep", "0,0", "--output", out});
        CHECK(r.code == 1);
        CHECK(r.err.find("无法识别的选项") != std::string::npos);
        CHECK(r.err.find("--keep") != std::string::npos);
    }

    // ---- IT-59：重复给出的单值选项——--input 两次（第二个不被静默吞掉）→ 退出码 1（B）。----
    {
        const std::string out = (root / "it59.png").string();
        const Captured r = runCli({"extract", "--input", in, "--input", in, "--rect", "50,40,150,120",
                                   "--output", out});
        CHECK(r.code == 1);
        CHECK(r.err.find("重复给出的取值选项") != std::string::npos);
        CHECK(r.err.find("--input") != std::string::npos);
    }

    // ---- IT-60：游离位置参数——命令后多余裸 token（无命令读它）→ 退出码 1（C）。----
    {
        const std::string out = (root / "it60.png").string();
        const Captured r = runCli({"extract", "--input", in, "--rect", "50,40,150,120",
                                   "--output", out, "stray"});
        CHECK(r.code == 1);
        CHECK(r.err.find("无法接受的多余参数") != std::string::npos);
        CHECK(r.err.find("stray") != std::string::npos);
    }

    // ---- IT-61：grid 分离模式（无 --compose）却给出合并专属 --canvas → 退出码 1（模式级对称守卫）。----
    {
        const fs::path dir = root / "it61";
        const Captured r = runCli({"grid", "--input", in, "--grid", "0,0,100,75",
                                   "--keep", "0,0", "--canvas", "2x2", "--output-dir", dir.string()});
        CHECK(r.code == 1);
        CHECK(r.err.find("合并专属选项") != std::string::npos);
        CHECK(r.err.find("--compose") != std::string::npos);
    }

    // ---- IT-62：grid 分离模式（无 --compose）却给出合并目标 --output → 退出码 1（模式级对称守卫）。----
    {
        const fs::path dir = root / "it62";
        const Captured r = runCli({"grid", "--input", in, "--grid", "0,0,100,75",
                                   "--keep", "0,0", "--output", (root / "it62.png").string(),
                                   "--output-dir", dir.string()});
        CHECK(r.code == 1);
        CHECK(r.err.find("合并专属选项") != std::string::npos);
        CHECK(r.err.find("--output") != std::string::npos);
    }

    // ---- IT-63：erase 分离模式（无 --merge）却给出合并目标 --output → 退出码 1（模式级对称守卫）。----
    {
        const fs::path dir = root / "it63";
        const Captured r = runCli({"erase", "--input", in, "--rect", "50,40,150,120",
                                   "--output", (root / "it63.png").string(), "--output-dir", dir.string()});
        CHECK(r.code == 1);
        CHECK(r.err.find("--merge") != std::string::npos);
        CHECK(r.err.find("--output") != std::string::npos);
    }

    // ---- IT-64：config 反向输出键——配置为分离导出却同时给 --output → 退出码 1（模式级对称守卫）。----
    {
        EngineConfig cfg;
        cfg.source = idc::engine::SourceInfo{200, 150};
        cfg.cut = idc::engine::CutConfig{};
        cfg.cut.tier = idc::engine::Tier::Standard;
        cfg.cut.generator = idc::engine::CutGenerator::Rect;
        cfg.cut.polarity = idc::engine::Polarity::Keep;
        cfg.cut.rect = idc::engine::RectRegion(0, 0, 100, 75);
        cfg.exportParams.mode = idc::engine::ExportMode::Separate;
        const std::string cfgPath = (root / "it64.json").string();
        CHECK(idc::engine::saveEngineConfig(cfgPath, cfg, nullptr));

        // 分离模式：--output-dir 必填且已给，多余反向 --output 触发模式级对称守卫。
        const fs::path dir = root / "it64";
        const Captured r = runCli({"config", "--load", cfgPath, "--input", in,
                                   "--output-dir", dir.string(), "--output", (root / "it64.png").string()});
        CHECK(r.code == 1);
        CHECK(r.err.find("导出模式不符") != std::string::npos);
    }

    // ---- IT-65：百分比坐标——--rect / --grid 的坐标段可写百分比（按输入图宽 / 高换算）；
    //      非整数也非百分比的段仍退出码 1。----
    {
        // 画布 200x150：0%,0%,50%,50% → (0,0,100,75)。
        const std::string out = (root / "it65.png").string();
        const Captured r = runCli({"extract", "--size", "200x150", "--pattern", "solid",
                                   "--bg", "#FFFF0000", "--rect", "0%,0%,50%,50%", "--output", out});
        CHECK(r.code == 0);
        int w = 0, h = 0;
        CHECK(readDims(out, w, h));
        CHECK(w == 100 && h == 75);

        // 网格：单元 50%×50% → 2×2；保留左上重排为 1×1 → 100×75。
        const std::string gridOut = (root / "it65_grid.png").string();
        const Captured g = runCli({"grid", "--size", "200x150", "--pattern", "solid",
                                   "--bg", "#FFFF0000", "--grid", "0,0,50%,50%", "--keep", "0,0",
                                   "--compose", "--canvas", "1x1", "--output", gridOut});
        CHECK(g.code == 0);
        CHECK(readDims(gridOut, w, h));
        CHECK(w == 100 && h == 75);

        // 形如 "10pt" 的段既非整数也非百分比 → 参数错误。
        const Captured bad = runCli({"extract", "--size", "200x150", "--rect", "10pt,0,100,100",
                                     "--output", out});
        CHECK(bad.code == 1);
        CHECK(bad.err.find("百分比") != std::string::npos);
    }

    // ---- IT-66：命令行标注 / 图层的几何坐标同样接受百分比（段级入口按基准换算；>100% 合法）----
    // 画布 200×150：标注 0%,0%,50%,50% → (0,0)-(100,75) 绿；图层框 50%×50%（=100×75）以左上锚点 + 偏移
    //   (50%,50%)=(100,75) 落位 → (100,75)-(200,150) 蓝（transform.x/y 是锚点落位之上的自由偏移）。
    {
        const std::string out = (root / "it66.png").string();
        const Captured r = runCli({"extract", "--size", "200x150", "--pattern", "solid",
                                   "--bg", "#FFFF0000", "--rect", "0,0,200,150",
                                   "--annotation",
                                   R"ANN({"type":"rectangle","x1":"0%","y1":"0%","x2":"50%","y2":"50%","fillColor":"#FF00FF00","fill":true})ANN",
                                   "--layer",
                                   R"J({"source":{"recipe":{"kind":"solid","background":"#FF0000FF"}},"transform":{"inCanvasAnchor":"top-left","layerPivot":"top-left","x":"50%","y":"50%","width":"50%","height":"50%"}})J",
                                   "--output", out});
        CHECK(r.code == 0);
        Image o;
        CHECK(idc::io::readImageFile(out, o));
        CHECK(o.width() == 200 && o.height() == 150);
        CHECK(isGreen(o.getPixel(40, 30)));    // 标注左上区
        CHECK(isBlue(o.getPixel(120, 90)));    // 图层左上区（不重叠标注）
        CHECK(isBlue(o.getPixel(190, 140)));   // 图层右下区
        CHECK(isRed(o.getPixel(190, 20)));     // 标注右侧、图层上方
        CHECK(isRed(o.getPixel(20, 140)));     // 标注下方、图层左侧

        // 既非整数也非百分比串的几何字段 → 仍为参数错误（1）。
        const Captured bad = runCli({"extract", "--size", "8x8", "--rect", "0,0,8,8",
                                     "--annotation", R"ANN({"type":"line","points":[["wide%","0%"],["1","1"]]})ANN",
                                     "--output", out});
        CHECK(bad.code == 1);
        CHECK(bad.err.find("数值字段") != std::string::npos);
    }

    // ---- IT-67：config 命令行标注的百分比按“解出的画布”换算（而非配置声明的 source）----
    // 配置 source 100×100，实际跑 --input 200×150：标注 0%,0%,50%,50% 应按 200×150 → (0,0)-(100,75)；
    //   若误按配置 source 换算会得 (0,0)-(50,50)，像素 (70,60) 就不会变绿。
    {
        const fs::path cfgPath = root / "it67.json";
        {
            std::ofstream ofs(cfgPath);
            ofs << R"J({"source":{"width":100,"height":100},
                    "cut":{"tier":"extract","generator":"rect","polarity":"keep",
                           "params":{"x1":0,"y1":0,"x2":100,"y2":100}},
                    "export":{"mode":"merged","merge":{"layout":"collapse"},"format":"png"}})J";
        }
        const std::string out = (root / "it67.png").string();
        const Captured r = runCli({"config", "--load", cfgPath.string(), "--input", in,
                                   "--annotation",
                                   R"ANN({"type":"rectangle","x1":"0%","y1":"0%","x2":"50%","y2":"50%","fillColor":"#FF00FF00","fill":true})ANN",
                                   "--output", out});
        CHECK(r.code == 0);
        Image o;
        CHECK(idc::io::readImageFile(out, o));
        CHECK(o.width() == 100 && o.height() == 100);
        CHECK(isGreen(o.getPixel(70, 60)));
    }

    // ---- 清理临时目录（不污染仓库 / 系统临时区）。----
    fs::remove_all(root, ec);

    std::cout << "[cli-integration] OK\n";
}
