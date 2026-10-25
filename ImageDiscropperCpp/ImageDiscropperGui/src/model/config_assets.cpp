// ============================================================================
// 文件：model/config_assets.cpp
// 作用：实现 config_assets.h——图层资产查找链解析、加载解码注入、保存打包写盘（零 Qt）。
// 分块依据：与 CLI 的 resolveAssetPath / bundleLayerAssets 同规则（本文件注释互指）：
//       查找链 = 显式目录（若有）→ config.assetsDir（若声明）→ <配置名>.assets/ → 配置所在目录，
//       assets 优先于配置目录（打包快照是权威，见 SPEC §7 图层栅格资源例外）；越界拒绝沿用同一契约。
// ============================================================================
#include "model/config_assets.h"

#include <system_error>
#include <vector>

#include "canvas/layer.h"      // canvas::ImageLayer（file 层 decodedPixels 注入 / 打包源）
#include "core/path_utf8.h"    // core::pathFromUtf8 / pathToUtf8（路径按 UTF-8 ↔ 宽字符）
#include "io/image_io.h"       // readImageFile / writeImageFile（Core 解码 / 落盘，与 CLI 同一 I/O 通道）

namespace idc::gui {

namespace {

// 配置所在目录（无父目录时退化为当前目录）；configPath 为空 → 当前工作目录。
std::filesystem::path configDirOf(const std::string& configPath) {
    if (configPath.empty()) return std::filesystem::path(".");
    const std::filesystem::path p = core::pathFromUtf8(configPath);
    const std::filesystem::path parent = p.parent_path();
    return parent.empty() ? std::filesystem::path(".") : parent;
}

// 候选目录中首个存在者；不存在返回空 path。
std::filesystem::path firstExisting(const std::filesystem::path& dir, const std::string& name) {
    if (dir.empty()) return {};
    std::error_code ec;
    const std::filesystem::path cand = dir / core::pathFromUtf8(name);
    return std::filesystem::exists(cand, ec) ? cand : std::filesystem::path();
}

} // namespace

bool isSafeLayerAsset(const std::string& assetName) {
    const std::filesystem::path p = core::pathFromUtf8(assetName);
    if (p.empty() || p.is_absolute() || p.has_root_name()) return false;
    for (const auto& comp : p)
        if (comp == "..") return false;
    return true;
}

std::filesystem::path resolveLayerAsset(const std::string& configPath,
                                        const std::string& assetsDirDeclared,
                                        const std::string& assetsDirOverride,
                                        const std::string& assetName) {
    const std::filesystem::path baseDir = configDirOf(configPath);

    // ① 显式「从哪里导入」（对话框资产来源）——最优先；相对路径按配置所在目录解析（绝对路径原样）。
    if (!assetsDirOverride.empty()) {
        const std::filesystem::path hit = firstExisting(baseDir / core::pathFromUtf8(assetsDirOverride), assetName);
        if (!hit.empty()) return hit;
    }
    // ② config.assetsDir 声明（相对配置目录；若与显式来源相同已查过，跳过）。
    if (!assetsDirDeclared.empty() && assetsDirDeclared != assetsDirOverride) {
        const std::filesystem::path hit = firstExisting(baseDir / core::pathFromUtf8(assetsDirDeclared), assetName);
        if (!hit.empty()) return hit;
    }
    // ③ <配置名>.assets/（约定目录，仅当有配置文件）。
    if (!configPath.empty()) {
        std::filesystem::path assetsName = core::pathFromUtf8(configPath).stem();
        assetsName += std::filesystem::path(L".assets");
        const std::filesystem::path hit = firstExisting(baseDir / assetsName, assetName);
        if (!hit.empty()) return hit;
    }
    // ④ 配置所在目录（兜底）。
    return firstExisting(baseDir, assetName);
}

bool decodeFileLayers(engine::EngineConfig& config, const std::string& configPath,
                      const std::string& assetsDirOverride, int& missing, std::string& err) {
    if (!config.layers.has_value()) return true;
    missing = 0;
    for (canvas::ImageLayer& layer : *config.layers) {
        if (layer.fileName.empty() || layer.recipe.has_value()) continue;
        if (!isSafeLayerAsset(layer.fileName)) {
            err = layer.fileName;   // 越界拒绝：调用方据此拼报因（拒绝优先于静默）。
            return false;
        }
        const std::filesystem::path src = resolveLayerAsset(configPath, config.assetsDir,
                                                            assetsDirOverride, layer.fileName);
        if (src.empty()) { ++missing; continue; }   // 缺文件 → 保持空 decodedPixels（降级占位）。
        core::Image img;
        if (!io::readImageFile(core::pathToUtf8(src), img)) { ++missing; continue; }
        layer.decodedPixels = std::move(img);
    }
    return true;
}

bool bundleFileLayers(const engine::EngineConfig& config, const std::filesystem::path& targetDir,
                      std::string& err) {
    if (!config.layers.has_value()) return true;
    std::error_code ec;
    for (const canvas::ImageLayer& layer : *config.layers) {
        if (layer.fileName.empty() || layer.recipe.has_value()) continue;
        if (!isSafeLayerAsset(layer.fileName)) { err = layer.fileName; return false; }
        if (layer.decodedPixels.empty()) { err = layer.fileName; return false; }   // 缺像素不静默。
        const std::filesystem::path dst = targetDir / core::pathFromUtf8(layer.fileName);
        if (dst.parent_path() != targetDir) {
            std::filesystem::create_directories(dst.parent_path(), ec);   // 已存在时返回 false 但 ec 不置——只认 ec。
            if (ec) { err = core::pathToUtf8(dst.parent_path()); return false; }
        }
        if (const auto r = io::writeImageFile(core::pathToUtf8(dst), layer.decodedPixels,
                                              engine::ExportFormat::Png, 90, core::kTransparent); !r) {
            err = core::pathToUtf8(dst);
            return false;
        }
    }
    return true;
}

} // namespace idc::gui
