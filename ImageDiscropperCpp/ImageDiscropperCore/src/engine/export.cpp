// ============================================================================
// 文件：src/engine/export.cpp
// 作用：实现导出阶段——exportImage 把合成结果 Composition 落盘（终稿 §5.1/§5.2/§5.5）。
//       分离模式（SEPARATE）把每个保留片段裁剪编码后，直接写入目标文件夹（不压缩打包）；
//       合并模式（MERGED）构建画布、blit 各片段后写单图。像素搬运在此发生（split 只产区域描述）。
// 分块依据：导出是唯一接触“像素裁剪 + 编码 I/O”的阶段，独立于合成计算（composition.cpp）；
//       编码委托 image_io（stb），本文件只做编排、命名与目录管理。
// 说明：SEPARATE 时 outputPath 为目标文件夹——不存在则自动创建（含多级父目录），仅在创建
//       失败（如被同名文件占用 / 权限不足）时返回 false（E-8）；MERGED 时 outputPath 为单图
//       文件路径，其父目录不存在时同样自动创建，格式优先由扩展名推断，无有效扩展名时回退到
//       Composition::format。
// ============================================================================
#include "engine/export.h"

#include "engine/image_io.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

namespace idc::engine {
namespace {

namespace fs = std::filesystem;

// 命名索引零填充宽度（对应模板 {index:03d}）。
constexpr int kIndexWidth = 3;

// 转小写（用于扩展名比较，ASCII 足够）。
std::string toLower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](const unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

// 取路径扩展名（含点，小写）；无扩展名返回空串。
std::string extensionOf(const std::string& path) {
    const std::size_t dot = path.find_last_of('.');
    const std::size_t sep = path.find_last_of("/\\");
    if (dot == std::string::npos) return "";
    if (sep != std::string::npos && dot < sep) return ""; // 点在目录名里，非扩展名。
    return toLower(path.substr(dot));
}

// 由扩展名推断导出格式；known 返回是否为受支持的图像格式。
ExportFormat formatFromExt(const std::string& ext, bool& known) {
    if (ext == ".png") { known = true; return ExportFormat::PNG; }
    if (ext == ".jpg" || ext == ".jpeg") { known = true; return ExportFormat::JPEG; }
    if (ext == ".bmp") { known = true; return ExportFormat::BMP; }
    if (ext == ".webp") { known = true; return ExportFormat::WEBP; } // 由 libwebp 编码。
    known = false;
    return ExportFormat::PNG;
}

// 格式对应的扩展名（供分离导出命名文件）。
const char* extForFormat(const ExportFormat fmt) {
    switch (fmt) {
        case ExportFormat::PNG:  return ".png";
        case ExportFormat::JPEG: return ".jpg";
        case ExportFormat::BMP:  return ".bmp";
        case ExportFormat::WEBP: return ".webp";
    }
    return ".png";
}

// 整数零填充到固定宽度（命名模板 {index:03d}）。
std::string zeroPad(const int v, const int width) {
    std::string s = std::to_string(v < 0 ? 0 : v);
    while (static_cast<int>(s.size()) < width) s.insert(s.begin(), '0');
    return s;
}

// 就地替换所有出现的子串 from → to。
void replaceAll(std::string& s, const std::string& from, const std::string& to) {
    if (from.empty()) return;
    std::size_t pos = 0;
    while ((pos = s.find(from, pos)) != std::string::npos) {
        s.replace(pos, from.size(), to);
        pos += to.size();
    }
}

// 应用命名模板：{name} → 文件夹名；{index:03d} → 零填充序号；{index} → 普通序号；
// {row}/{col} → 源网格行/列号（非网格来源即 -1 时按 0 处理）。
std::string applyNaming(const std::string& tpl, const std::string& name, const int index,
                        const int row, const int col) {
    std::string out = tpl.empty() ? std::string("{name}_{index:03d}") : tpl;
    replaceAll(out, "{name}", name);
    replaceAll(out, "{index:03d}", zeroPad(index, kIndexWidth));
    replaceAll(out, "{index}", std::to_string(index));
    replaceAll(out, "{row}", std::to_string(row < 0 ? 0 : row));
    replaceAll(out, "{col}", std::to_string(col < 0 ? 0 : col));
    return out;
}

// 确保 dir 是一个已存在的目录：本就是目录则直接成功；不存在则创建（含多级父目录）。
// 仅在创建失败（被同名文件占用 / 权限不足等）时返回 false（E-8）——即“先尝试创建，失败再报错”。
bool ensureDirectory(const fs::path& dir) {
    std::error_code ec;
    if (fs::is_directory(dir, ec)) return true; // 已是目录，无需创建。
    fs::create_directories(dir, ec);            // 尝试创建；失败时置 ec。
    if (ec) return false;                       // 创建出错。
    return fs::is_directory(dir, ec);           // 兜底再确认确为目录。
}

// 分离模式导出：把每个保留片段裁剪编码后写入目标文件夹（不压缩打包）。
// outputDir 不存在则自动创建（含多级父目录）；创建失败返回 false（E-8）。
bool exportSeparate(const Composition& composition, const core::Image& source,
                    const std::string& outputDir) {
    const fs::path dir(outputDir);
    if (!ensureDirectory(dir)) return false; // E-8：文件夹不存在且无法创建。

    // 命名模板 {name} 取文件夹名（无法取得时回退 "region"）。
    std::string name = dir.filename().string();
    if (name.empty() || name == "." || name == "..") name = "region";

    const ExportFormat fmt = composition.format;
    const std::string ext = extForFormat(fmt);

    bool allOk = true;
    int i = 0;
    for (const Placement& p : composition.placements) {
        const core::Image sub =
            source.crop(p.source.left, p.source.top, p.source.right, p.source.bottom);
        const int idx = p.index >= 0 ? p.index : i; // 优先用单元序号命名。
        if (const fs::path file = dir / (applyNaming(composition.naming, name, idx, p.row, p.col) + ext); !writeImageFile(file.string(), sub, fmt, composition.quality, core::kTransparent))
            allOk = false;
        ++i;
    }
    return allOk;
}

// 合并模式导出：构建画布、以 padColor 填充、逐片段 blit，写单图。
// 父目录不存在时自动创建（与分离导出行为一致，修复“合并导出不创建父目录”）。
bool exportMerged(const Composition& composition, const core::Image& source,
                  const std::string& outputPath) {
    const fs::path outPath(outputPath);
    if (const fs::path parent = outPath.parent_path(); !parent.empty() && !ensureDirectory(parent)) return false; // E-8：父目录不可创建。

    bool known = false;
    ExportFormat fmt = formatFromExt(extensionOf(outputPath), known);
    if (!known) fmt = composition.format; // 扩展名不明时以配置格式为准。

    // RGBA 画布，以透传的 padColor 填充（重排空位 / PAD 余量在源区域裁剪后由画布底色补齐）。
    core::Image canvas(composition.canvasWidth, composition.canvasHeight, core::ImageFormat::RGBA);
    canvas.fill(composition.padColor);
    for (const Placement& p : composition.placements) {
        const core::Image sub =
            source.crop(p.source.left, p.source.top, p.source.right, p.source.bottom);
        canvas.blit(sub, p.dest.left, p.dest.top);
    }
    return writeImageFile(outputPath, canvas, fmt, composition.quality, composition.padColor);
}

} // namespace

// 把合成结果落盘（分离多图 / 合并单图）；成功返回 true。
// 模式判定：compose 对 SEPARATE 置画布 0×0（outputPath 为目标文件夹，不存在则自动创建），
//           对 MERGED 置真实画布尺寸（outputPath 为单图文件路径）。
bool exportImage(const Composition& composition, const core::Image& source,
                 const std::string& outputPath) {
    if (composition.placements.empty()) return false; // E-7：保留集为空，禁止导出。
    if (outputPath.empty()) return false;             // E-8：路径为空。
    if (source.empty()) return false;                 // 无源图像，无法裁剪。

    const bool separate = composition.canvasWidth <= 0 || composition.canvasHeight <= 0;
    return separate ? exportSeparate(composition, source, outputPath)
                    : exportMerged(composition, source, outputPath);
}

} // namespace idc::engine
