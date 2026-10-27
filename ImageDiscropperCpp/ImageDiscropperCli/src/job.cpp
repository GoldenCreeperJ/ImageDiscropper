// ============================================================================
// 文件：src/job.cpp
// 作用：实现 job.h——CLI 与 Core 引擎之间的执行桥。把装配好的 EngineConfig 跑通、落盘，
//       并把 Core 的结果 / 失败精确映射为退出码。
// 分块依据：三层命令与 config 命令共用此执行路径（NFR-0），命令层不重复执行逻辑；
//       本文件只调用 Core 公开 API（readImageFile / runEngine / exportImage /
//       saveEngineConfig），不含任何切割 / 合成 / 编码实现（CONTRIBUTING.md「分层纪律」）。
// 说明：Core 的 runEngine 仅在 L3 遇到「MERGED+COLLAPSE 但不可坍缩」时自动改用 REARRANGE
//       并返回 ok=true、collapsible=false（L1/L2 直接报错，见 SPEC §4.3）；CLI 据
//       opt.explicitCollapse 判定是否应显式报错（用户显式要 collapse → 退出码 2 并提示 rearrange）。
// 常量层：错误 brief / detail / hint 与进度日志一律经门面 IDC_CTR(msg, ...) 取 texts::msg
//       （include/i18n/cli_texts.h），本层不直接写常量名，也不写字面量——键由预处理器拼出，
//       常量改名即未声明标识符，编译期按落点直接报回（zh-CN 下表为空，逐字节回退常量层字面输出）。
//       未提取项：后缀 L".assets"（宽字面量，与 char[] 类型不符；GUI 侧同契约读的是
//       gui_texts.h ident::kAssetsDirSuffix，两处必须同字）、逐分量比较的 ".."（path 分量相等判定）、以及语言中性的
//       "x" / " → " 格式碎片（随语句局部阅读更直观）；尺寸括注的全角「（）」属 msg 组供译。
// ============================================================================
#include "job.h"

#include <algorithm>
#include <filesystem>
#include <iostream>
#include <optional>
#include <string>

#include "engine/engine_config_json.h" // saveEngineConfig
#include "io/image_io.h"              // readImageFile
#include "engine/export.h"             // exportImage（new 命令复用同一落盘通道）
#include "engine/composition.h"        // Composition / Placement / CompositionParams
#include "engine/region.h"             // RectRegion（全画布源/目标区域）
#include "preprocess/preprocess_config.h" // PreprocessConfig / configType / PreprocessType（G2 resize 探测）
#include "core/path_utf8.h"            // pathFromUtf8/pathToUtf8（取代 C++23 已移除的 u8path）
#include "annotation/annotation_set.h" // AnnotationSet::burnIn(base)（配置标注烧录）
#include "canvas/layer.h"              // composeCanvas / ImageLayer（多层画布展平，SPEC §7 layers）

#include "cli_error.h"
#include "i18n/cli_texts.h"
#include "i18n/cli_i18n.h"

namespace idc::cli {

namespace {

// 图层资源文件名的安全校验：仅接受「相对、不越界」的路径——拒绝绝对路径、带根/盘符、
// 以及任何含 `..` 分量的名字（越界限死配置 / assets 目录，SPEC §7 layers 决策）。
bool isSafeRelativeAsset(const std::string& name) {
    if (name.empty()) return false;
    const std::filesystem::path p(name);
    if (p.is_absolute() || !p.root_name().empty() || !p.root_directory().empty()) return false;
    for (const auto& part : p) {
        if (part == "..") return false;
    }
    return true;
}

// 由配置路径推导其所在目录（父目录；无父目录时退化为当前目录）。configPath 按 UTF-8 解析。
std::filesystem::path configDirectory(const std::string& configPath) {
    const std::filesystem::path p = core::pathFromUtf8(configPath);
    std::filesystem::path parent = p.parent_path();
    return parent.empty() ? std::filesystem::path(".") : parent;
}

// 在查找链中定位 File 层源图（与 GUI model/config_assets 同规）：assetsDirOverride（--assets-dir=path，若有）→
//   config.assetsDir 声明（若写且非覆盖本身）→ <配置名>.assets/（约定目录，仅 config 命令有配置锚点）→
//   配置所在目录（configPath 空＝当前工作目录，无约定层级）。命中首个存在者返回其路径；均未命中返回空 path
//   （调用方据此降级占位或报错）。assets 优先于配置目录——打包分发的 .assets 是随配置同行的权威快照
//   （SPEC §7 图层栅格资源例外；原地改散放文件须重新 bundle 才更新）。
std::filesystem::path resolveAssetPath(const std::string& configPath,
                                       const std::string& assetsDirDeclared,
                                       const std::string& assetsDirOverride,
                                       const std::string& assetName) {
    const std::filesystem::path baseDir = configDirectory(configPath);
    const auto hit = [&assetName](const std::filesystem::path& dir) {
        if (dir.empty()) return std::filesystem::path();
        std::error_code ec;
        const std::filesystem::path cand = dir / core::pathFromUtf8(assetName);   // 绝对目录 / 相对名原样，相对目录按 baseDir 解析。
        return std::filesystem::exists(cand, ec) ? cand : std::filesystem::path();
    };
    if (!assetsDirOverride.empty())
        if (const std::filesystem::path p = hit(baseDir / core::pathFromUtf8(assetsDirOverride)); !p.empty()) return p;
    if (!assetsDirDeclared.empty() && assetsDirDeclared != assetsDirOverride)
        if (const std::filesystem::path p = hit(baseDir / core::pathFromUtf8(assetsDirDeclared)); !p.empty()) return p;
    if (!configPath.empty()) {
        std::filesystem::path assetsName = core::pathFromUtf8(configPath).stem();
        assetsName += std::filesystem::path(L".assets");   // <配置名>.assets（保持宽字符，避免窄串码页失配）
        if (const std::filesystem::path p = hit(baseDir / assetsName); !p.empty()) return p;
    }
    return hit(baseDir);
}

} // namespace

// 把 --assets-dir=path 的目录在「保存配置目录之内」时相对化（可写 §7 assetsDir 字段：相对、配置目录内解析）；
//   相对路径原样返回；绝对路径不落保存配置目录之内 → 返回空（字段契约＝配置内声明，目录外不写）。
std::string relativizeAssetsDirForSave(const std::string& saveConfigPath, const std::string& overrideDir) {
    if (overrideDir.empty()) return {};
    const std::filesystem::path ov = core::pathFromUtf8(overrideDir);
    if (ov.is_relative()) return core::pathToUtf8(ov.lexically_normal());
    const std::filesystem::path savePath = core::pathFromUtf8(saveConfigPath);
    const std::filesystem::path saveDir = savePath.parent_path().empty() ? std::filesystem::path(".") : savePath.parent_path();
    std::error_code ec;
    const std::filesystem::path canon = std::filesystem::weakly_canonical(ov, ec);
    if (ec) return {};
    const std::filesystem::path rel = canon.lexically_relative(std::filesystem::weakly_canonical(saveDir));
    if (rel.empty()) return {};
    // 逐分量查越界（path::native() 平台间 wchar_t/char 类型不一，不能写死宽字面量比较）
    for (const auto& part : rel) {
        if (part == "..") return {};
    }
    return core::pathToUtf8(rel);
}

// 读取输入图像；失败按输入文件错误（3）报告。
ExitCode loadInputImage(const std::string& path, core::Image& out, const JobOptions& opt) {
    if (path.empty()) {
        reportError(makeError(ExitCode::ArgError, IDC_CTR(msg, kErrNoInputPath),
                              "", IDC_CTR(msg, kHintUseInputFile)));
        return ExitCode::ArgError;
    }
    // Core 解码：支持 PNG/JPEG/BMP/WebP 等；文件不存在 / 非图像 / 不可读均返回失败（CoreError，operator bool 判假）。
    if (!io::readImageFile(path, out)) {
        reportError(makeError(ExitCode::InputError, IDC_CTR(msg, kErrReadInput),
                              IDC_CTR(msg, kDetPath) + path,
                              IDC_CTR(msg, kHintInputReadable)));
        return ExitCode::InputError;
    }
    if (opt.verbose)
        std::cerr << IDC_CTR(msg, kLogPrefix) << IDC_CTR(msg, kLogInputRead) << path
                  << IDC_CTR(msg, kLogParenOpen) << out.width() << "x" << out.height() << IDC_CTR(msg, kLogParenClose);
    return ExitCode::Ok;
}

// 把已生成图像作为合并单图落盘（new 命令专用）：构造覆盖全画布的单片段 Composition 交 Core exportImage。
ExitCode writeGeneratedImage(const core::Image& image, const std::string& outPath,
                             const engine::CompositionParams& exportParams, const JobOptions& opt) {
    if (image.empty()) {
        reportError(makeError(ExitCode::OutputError, IDC_CTR(msg, kErrEmptyImage), IDC_CTR(msg, kDetTarget) + outPath,
                              IDC_CTR(msg, kHintCheckGenArgs)));
        return ExitCode::OutputError;
    }
    // 单片段：源区域与目标区域均为整幅画布 [0,W)×[0,H)；canvas 即图像尺寸（合并单图）。
    engine::Composition comp;
    comp.canvasWidth = image.width();
    comp.canvasHeight = image.height();
    comp.format = exportParams.format;         // 扩展名不明时的回退格式。
    comp.quality = exportParams.quality;
    comp.padColor = exportParams.padColor;     // JPEG/BMP 压平背景（合并画布空位填充色，全幅覆盖时用不到）。
    engine::Placement whole;
    whole.source = engine::RectRegion(0, 0, image.width(), image.height());
    whole.dest = engine::RectRegion(0, 0, image.width(), image.height());
    whole.index = 0;
    whole.row = 0;
    whole.col = 0;
    comp.placements.push_back(whole);

    if (opt.verbose)
        std::cerr << IDC_CTR(msg, kLogPrefix) << IDC_CTR(msg, kLogWriteGenerated) << outPath << IDC_CTR(msg, kLogParenOpen)
                  << image.width() << "x" << image.height() << IDC_CTR(msg, kLogParenClose);
    if (!engine::exportImage(comp, image, outPath)) {
        reportError(makeError(ExitCode::OutputError, IDC_CTR(msg, kErrExportFailed), IDC_CTR(msg, kDetTarget) + outPath,
                              IDC_CTR(msg, kHintOutputWritable)));
        return ExitCode::OutputError;
    }
    return ExitCode::Ok;
}

// 多层画布合成：逐层解码 File 叠加层源图像（相对 <配置名>.assets / 配置目录，assets 优先）注入 decodedPixels → 以 base 为底 composeCanvas 展平。
ExitCode buildLayerCanvas(const engine::EngineConfig& config, const std::string& configPath,
                          const core::Image& base, core::Image& out, const JobOptions& opt) {
    if (!config.layers.has_value()) {
        reportError(makeError(ExitCode::RuntimeError, IDC_CTR(msg, kErrLayerCompose), IDC_CTR(msg, kDetNoLayers),
                              IDC_CTR(msg, kHintInternalLayersOnly)));
        return ExitCode::RuntimeError;
    }
    // config 为 const（不能改写回），但需把解码出的 File 层像素注入后再合成——取可变副本作工作数组。
    std::vector<canvas::ImageLayer> layers = *config.layers;

    std::size_t missingAssets = 0;   // 降级占位的缺失图层计数（循环后出一条 WARN 汇总）。
    for (auto& layer : layers) {
        if (layer.fileName.empty())
            continue; // 纯图案 / 占位层无需解码；空名视为占位（降级）。
        if (!isSafeRelativeAsset(layer.fileName)) {
            reportError(makeError(ExitCode::InputError, IDC_CTR(msg, kErrAssetPathEscape),
                                  IDC_CTR(msg, kDetLayerFileName) + layer.fileName,
                                  IDC_CTR(msg, kHintAssetRelative)));
            return ExitCode::InputError;
        }
        const std::filesystem::path resolved = resolveAssetPath(configPath, config.assetsDir,
                                                                opt.assetsDir.value_or(""), layer.fileName);
        if (resolved.empty()) {
            // --strict：缺文件直接报输入文件错误（退出码 3），供无人值守脚本据此中断。
            if (opt.strict) {
                reportError(makeError(ExitCode::InputError, IDC_CTR(msg, kErrAssetMissingStrict),
                                      IDC_CTR(msg, kDetMissing) + layer.fileName,
                                      IDC_CTR(msg, kHintAssetMissingStrict)));
                return ExitCode::InputError;
            }
            ++missingAssets;
            if (opt.verbose)
                std::cerr << IDC_CTR(msg, kLogPrefix) << IDC_CTR(msg, kLogAssetMissing) << layer.fileName << "\n";
            continue; // 缺文件 → decodedPixels 保持空，composeCanvas 按占位（空图）处理。
        }
        core::Image px;
        if (!io::readImageFile(core::pathToUtf8(resolved), px)) {
            reportError(makeError(ExitCode::InputError, IDC_CTR(msg, kErrAssetDecode),
                                  IDC_CTR(msg, kDetPath) + core::pathToUtf8(resolved),
                                  IDC_CTR(msg, kHintInputReadable)));
            return ExitCode::InputError;
        }
        layer.decodedPixels = std::move(px);
    }

    // 缺文件已降级占位：恒出一条 WARN 汇总到 stderr（与 verbose 无关）（使无人值守批量运行可见产出降级，对齐报错的不对称性）。
    if (missingAssets > 0)
        std::cerr << IDC_CTR(msg, kWarnHead)
                  << IDC_CTRF(msg, kWarnAssetMissingSummary, std::to_string(missingAssets)) << "\n";

    out = canvas::composeCanvas(layers, config.source.width, config.source.height, base);
    if (out.empty()) {
        reportError(makeError(ExitCode::RuntimeError, IDC_CTR(msg, kErrLayerCanvasEmpty),
                              IDC_CTRF(msg, kDetSourceSize, std::to_string(config.source.width) + "x" + std::to_string(config.source.height)),
                              IDC_CTR(msg, kHintSourceBounds)));
        return ExitCode::RuntimeError;
    }
    if (opt.verbose)
        std::cerr << IDC_CTR(msg, kLogPrefix)
                  << IDC_CTRF(msg, kLogLayerComposed, std::to_string(out.width()), std::to_string(out.height()), std::to_string(layers.size()));
    return ExitCode::Ok;
}

// 底图展平（SPEC §7 generate 段，同构 canvas::ImageLayer）：Recipe 底按 source 尺寸现算 / 落位底图需外部 --input。
ExitCode buildBaseCanvas(const engine::EngineConfig& config, core::Image& out, const JobOptions& opt) {
    if (!config.base.has_value()) {
        reportError(makeError(ExitCode::RuntimeError, IDC_CTR(msg, kErrBaseCompose), IDC_CTR(msg, kDetNoGenerate),
                              IDC_CTR(msg, kHintInternalBaseOnly)));
        return ExitCode::RuntimeError;
    }
    const canvas::ImageLayer& base = *config.base;
    // 底图不外传文件：带内层落位的配置只存落位参数、不存图片路径，其源图须由 --input 运行时供给。
    //   无 --input 走到此处（纯配置展平）无法落位任何位图 → 明确报因。
    if (base.hasImagePlacement) {
        reportError(makeError(ExitCode::InputError, IDC_CTR(msg, kErrBaseNeedsInput),
                              IDC_CTR(msg, kDetBaseNeedsInput),
                              IDC_CTR(msg, kHintProvideBaseInput)));
        return ExitCode::InputError;
    }
    out = canvas::composeBase(base, config.source.width, config.source.height);
    if (out.empty()) {
        reportError(makeError(ExitCode::RuntimeError, IDC_CTR(msg, kErrBaseGenEmpty),
                              IDC_CTRF(msg, kDetSourceSize, std::to_string(config.source.width) + "x" + std::to_string(config.source.height)),
                              IDC_CTR(msg, kHintSourceBounds)));
        return ExitCode::RuntimeError;
    }
    if (opt.verbose)
        std::cerr << IDC_CTR(msg, kLogPrefix) << IDC_CTR(msg, kLogBaseGenerated) << out.width() << "x" << out.height() << "\n";
    return ExitCode::Ok;
}

// 把已解析的外部底图源（--input/--size 产物 io）按 config.base 内层落位摆入 config.source 尺寸画布：
//   运行时以 File 源注入 decodedPixels 后交 composeBase 展平（配置不含 file 路径，图凭运行时供给，保批处理复用）。
ExitCode placeInputIntoBase(const engine::EngineConfig& config, core::Image& io, const JobOptions& opt) {
    if (!config.base.has_value() || !config.base->hasImagePlacement) return ExitCode::Ok;   // 无落位：保持恒等底图
    canvas::ImageLayer layer = *config.base;
    layer.decodedPixels = io;   // 运行时注入外部输入图作位图（无 recipe → 文件源派生）
    const core::Image composed = canvas::composeBase(layer, config.source.width, config.source.height);
    if (composed.empty()) {
        reportError(makeError(ExitCode::RuntimeError, IDC_CTR(msg, kErrBasePlaceEmpty),
                              IDC_CTRF(msg, kDetSourceSize, std::to_string(config.source.width) + "x" + std::to_string(config.source.height)),
                              IDC_CTR(msg, kHintBasePlacement)));
        return ExitCode::RuntimeError;
    }
    io = composed;
    if (opt.verbose)
        std::cerr << IDC_CTR(msg, kLogPrefix) << IDC_CTR(msg, kLogBasePlaced) << io.width() << "x" << io.height() << "\n";
    return ExitCode::Ok;
}

// 资源打包（--assets-dir 三态）：把各 File 层源图按相对名复制进 <saveConfigPath 同名>.assets/ 或自定义目录。
ExitCode bundleLayerAssets(const engine::EngineConfig& config, const std::string& configPath,
                           const std::string& saveConfigPath, const JobOptions& opt) {
    if (!config.layers.has_value()) return ExitCode::Ok; // 无图层可打包，空操作。
    // 输出目录：--assets-dir=path 自定义（相对路径按保存配置所在目录解析）；否则 <保存配置名>.assets/。
    const std::filesystem::path savePath = core::pathFromUtf8(saveConfigPath);
    const std::filesystem::path saveDir = savePath.parent_path().empty() ? std::filesystem::path(".") : savePath.parent_path();
    std::filesystem::path outAssetsDir;
    if (opt.assetsDir.has_value() && !opt.assetsDir->empty())
        outAssetsDir = saveDir / core::pathFromUtf8(*opt.assetsDir);
    else {
        std::filesystem::path outAssetsName = savePath.stem();
        outAssetsName += std::filesystem::path(L".assets");
        outAssetsDir = saveDir / outAssetsName;
    }

    std::error_code ec;
    // 目标目录先建好：顶层 File 层（无子目录）也要落在其中；嵌套层由下方子目录分支再建。
    if (std::filesystem::create_directories(outAssetsDir, ec); ec) {
        reportError(makeError(ExitCode::OutputError, IDC_CTR(msg, kErrMkdirAsset),
                              IDC_CTR(msg, kDetPath) + core::pathToUtf8(outAssetsDir), ec.message()));
        return ExitCode::OutputError;
    }
    for (const auto& layer : *config.layers) {
        if (layer.fileName.empty())
            continue;
        if (!isSafeRelativeAsset(layer.fileName)) {
            reportError(makeError(ExitCode::ArgError, IDC_CTR(msg, kErrAssetEscapeReject),
                                  IDC_CTR(msg, kDetLayerFileName) + layer.fileName,
                                  IDC_CTR(msg, kHintBundleRelative)));
            return ExitCode::ArgError;
        }
        const std::filesystem::path src = resolveAssetPath(configPath, config.assetsDir,
                                                           opt.assetsDir.value_or(""), layer.fileName);
        if (src.empty()) {
            reportError(makeError(ExitCode::InputError, IDC_CTR(msg, kErrAssetMissingBundle),
                                  IDC_CTR(msg, kDetMissing) + layer.fileName,
                                  IDC_CTR(msg, kHintBundleNeedsSource)));
            return ExitCode::InputError;
        }
        const std::filesystem::path dst = outAssetsDir / core::pathFromUtf8(layer.fileName);
        // 原地重打包（src==dst 自复制：标准未定义、平台各异甚至截断为空）→ 幂等跳过＝自覆盖的等价实现。
        if (std::error_code sameEc; src == dst || std::filesystem::equivalent(src, dst, sameEc)) {
            if (opt.verbose)
                std::cerr << IDC_CTR(msg, kLogPrefix) << IDC_CTR(msg, kLogAssetBundled) << layer.fileName << "\n";
            continue;
        }
        if (dst.parent_path() != outAssetsDir) {
            std::filesystem::create_directories(dst.parent_path(), ec);   // 已存在时返回 false 但 ec 不置——只认 ec。
            if (ec) {
                reportError(makeError(ExitCode::OutputError, IDC_CTR(msg, kErrMkdirAsset),
                                      IDC_CTR(msg, kDetPath) + core::pathToUtf8(dst.parent_path()), ec.message()));
                return ExitCode::OutputError;
            }
        }
        std::filesystem::copy_file(src, dst, std::filesystem::copy_options::overwrite_existing, ec);
        if (ec) {
            reportError(makeError(ExitCode::OutputError, IDC_CTR(msg, kErrCopyAsset),
                                  core::pathToUtf8(src) + " → " + core::pathToUtf8(dst), ec.message()));
            return ExitCode::OutputError;
        }
        if (opt.verbose) std::cerr << IDC_CTR(msg, kLogPrefix) << IDC_CTR(msg, kLogAssetBundled) << layer.fileName << "\n";
    }
    return ExitCode::Ok;
}

// 执行作业：校正 source → runEngine → 坍缩校验 → exportImage → 退出码。
ExitCode executeJob(engine::EngineConfig& config, const core::Image& image,
                    const JobOutput& out, const JobOptions& opt,
                    const annotation::AnnotationSet* annotations) {
    // G2：config 声明的 source 尺寸 ≠ 实际输入图像且未用 resize 预处理 → 静默按交集裁窄 / 丢格。
    //   默认出一条 WARN（stderr，与 verbose 无关）；--strict（opt.strict）→ 输入错误退出码 3。
    //   比对在预处理物化前、用原始 image 参数：extract/erase/grid 已把 source 设为图像尺寸（恒相等）；
    //   config 的 base/layers 路径工作图按 source 生成（恒相等）；仅 --input 裸图尺寸 ≠ 声明 source 才触发。
    //   含 resize 预处理时跳过（resize 是归一尺寸的既定 remedy，错配即将被其修正）。
    if (config.source.width > 0 && config.source.height > 0) {
        const bool hasResize = std::ranges::any_of(config.preprocess,
                                                   [](const preprocess::PreprocessConfig& step) {
                                                       return preprocess::configType(step) == preprocess::PreprocessType::Resize;
                                                   });
        if (!hasResize && (config.source.width != image.width() || config.source.height != image.height())) {
            const std::string declared = std::to_string(config.source.width) + "x" + std::to_string(config.source.height);
            const std::string actual = std::to_string(image.width()) + "x" + std::to_string(image.height());
            if (opt.strict) {
                reportError(makeError(ExitCode::InputError, IDC_CTR(msg, kErrSizeMismatchStrict),
                                      IDC_CTRF(msg, kDetSizeMismatch, declared, actual),
                                      IDC_CTR(msg, kHintSizeMismatch)));
                return ExitCode::InputError;
            }
            std::cerr << IDC_CTR(msg, kWarnHead) << IDC_CTRF(msg, kWarnSizeMismatch, declared, actual) << "\n";
        }
    }

    // FR-1：切割前把 preprocess / 标注物化为「工作图 work」。
    //   几何流水线（runEngine）与落盘（exportImage）必须共用同一张 work——runEngine 内部会按预处理
    //   产出尺寸（rotate ±90 宽高互换 / resize 改尺寸）计算裁剪区域，若仍拿未预处理的原图落盘，
    //   裁剪矩形会套在尺寸/像素都不符的原图上，导致裁错内容或越界。物化后清空 config.preprocess，
    //   令 runEngine 不会二次应用（与 GUI「引擎收已预处理图 + 空 preprocess」同一约定）。
    //   NFR-8：按需物化——无 preprocess 且无标注（最常见用法）时 work 直指入参图，**不做整图深拷贝**。
    std::optional<core::Image> buffer;              // 物化产物缓冲（函数期内存活；重新赋值前须确保旧指向已消费完）。
    const core::Image* work = &image;
    const bool hasAnnotations = annotations != nullptr && !annotations->annotations().empty();

    // 先物化画布预处理、后烧标注（标注不受画布预处理影响）——与既有 job 口径一致。
    if (!config.preprocess.empty()) {
        buffer = config.preprocess.apply(image);
        if (buffer->empty()) {
            reportError(makeError(ExitCode::RuntimeError, IDC_CTR(msg, kErrPreprocessFailed),
                                  IDC_CTR(msg, kDetPreprocessEmpty), IDC_CTR(msg, kHintCheckPreprocess)));
            return ExitCode::RuntimeError;
        }
        config.preprocess = preprocess::PreprocessPipeline{}; // 已物化进 buffer，清空防二次应用。
        work = &*buffer;
    }
    if (hasAnnotations) {
        core::Image burned = annotations->burnIn(*work); // Core 逐形状光栅化（含 OBB 变换），CLI 不自实现像素合成。
        buffer = std::move(burned);
        work = &*buffer;
        if (opt.verbose) std::cerr << IDC_CTR(msg, kLogPrefix) << IDC_CTRF(msg, kLogBurnIn, std::to_string(annotations->annotations().size())) << "\n";
    }

    // 以工作图实际尺寸校正 source（预处理可能改变尺寸）。
    config.source.width = work->width();
    config.source.height = work->height();

    if (opt.verbose) std::cerr << IDC_CTR(msg, kLogPrefix) << IDC_CTR(msg, kLogRunEngine) << "\n";
    const auto engineResult = engine::runEngine(*work, config);
    if (!engineResult) {
        // Core 业务错误（E-1/E-5/E-6/E-7 等）统一归为运行时错误（2）；报因文本由 CoreError.message 承载（已在产生点本地化）。
        reportError(makeError(ExitCode::RuntimeError, IDC_CTR(msg, kErrEngineFailed), engineResult.error().message,
                              IDC_CTR(msg, kHintCheckSelection)));
        return ExitCode::RuntimeError;
    }
    const engine::EngineResult& result = *engineResult;

    // G3：引擎成功但零导出单元 → 落 0 文件的假成功。默认 WARN（stderr，与 verbose 无关）；
    //   --strict（opt.strict）→ 运行错误退出码 2（供无人值守脚本据非零码中断）。
    if (result.composition.placements.empty()) {
        if (opt.strict) {
            reportError(makeError(ExitCode::RuntimeError, IDC_CTR(msg, kErrEmptyOutputStrict),
                                  IDC_CTR(msg, kDetEmptyOutput),
                                  IDC_CTR(msg, kHintEmptyOutput)));
            return ExitCode::RuntimeError;
        }
        std::cerr << IDC_CTR(msg, kWarnHead) << IDC_CTR(msg, kWarnEmptyOutput) << "\n";
    }

    // 显式要求坍缩但不可坍缩 → 退出码 2（固定错误文本）。
    if (opt.explicitCollapse && !result.collapsible) {
        reportError(makeError(ExitCode::RuntimeError, IDC_CTR(msg, kErrNotCollapsible),
                              IDC_CTR(msg, kDetNotCollapsible),
                              IDC_CTR(msg, kHintUseRearrange)));
        return ExitCode::RuntimeError;
    }

    // 合并请求被重排准入门限降为分离（SPEC §4.3：保留块尺寸不全等且不可坍缩）：单文件路径承载不了
    // 多图，明确报错让用户改走分离导出，而不是把文件名当文件夹写。
    if (!out.separate && result.composition.canvasWidth <= 0) {
        reportError(makeError(ExitCode::RuntimeError, IDC_CTR(msg, kErrNotMergeable),
                              IDC_CTR(msg, kDetNotMergeable), IDC_CTR(msg, kHintUseSeparateExport)));
        return ExitCode::RuntimeError;
    }

    if (opt.verbose)
        std::cerr << IDC_CTR(msg, kLogPrefix) << IDC_CTR(msg, kLogExportTo) << out.path
                  << (out.separate ? IDC_CTR(msg, kLogSeparateTag) : IDC_CTR(msg, kLogMergedTag)) << "\n";

    // 落盘：分离 → 目标文件夹（Core 自动创建）；合并 → 单图文件路径。裁剪源恒为已预处理 work。
    if (!engine::exportImage(result.composition, *work, out.path)) {
        reportError(makeError(ExitCode::OutputError, IDC_CTR(msg, kErrExportFailed), IDC_CTR(msg, kDetTarget) + out.path,
                              out.separate ? IDC_CTR(msg, kHintOutputDirWritable)
                                           : IDC_CTR(msg, kHintOutputWritable)));
        return ExitCode::OutputError;
    }

    // 正常结果输出到 stdout（数据行，恒写；沉默是金只约束诊断日志）。
    {
        if (out.separate) {
            std::cout << IDC_CTRF(msg, kLogExportedCount, std::to_string(result.composition.placements.size()), out.path) << "\n";
        } else {
            std::cout << IDC_CTR(msg, kLogExportedMerged) << out.path << IDC_CTR(msg, kLogParenOpen)
                      << result.composition.canvasWidth << "x"
                      << result.composition.canvasHeight << IDC_CTR(msg, kLogParenClose);
        }
    }
    return ExitCode::Ok;
}

// 序列化配置到 JSON 文件（--save-config dry-run）；失败按输出失败（4）报告。
ExitCode saveConfigToFile(const engine::EngineConfig& config, const std::string& path,
                          const JobOptions& opt,
                          const annotation::AnnotationSet* annotations,
                          const core::PercentHints* percentHints) {
    if (!engine::saveEngineConfig(path, config, annotations, percentHints)) {
        reportError(makeError(ExitCode::OutputError, IDC_CTR(msg, kErrSaveConfig), IDC_CTR(msg, kDetPath) + path,
                              IDC_CTR(msg, kHintSaveConfigPath)));
        return ExitCode::OutputError;
    }
    std::cout << IDC_CTR(msg, kLogConfigSaved) << path << "\n";
    return ExitCode::Ok;
}

// 作业收尾：--save-config 优先（dry-run，仅序列化不执行）；否则执行作业。
// 命令行图层（--layer/--layers，四命令通用）：config.layers 有值即先以入参图为底展平合成
//   （资产按当前工作目录解析——非 config 命令无配置文件锚点，无 <配置名>.assets/ 层级），
//   与 config 命令同走 buildLayerCanvas 这一条合成路径。
// --assets-dir 三态（非 config 命令的 --save-config 亦生效）：出现 → 序列化成功后打包；
//   =path 在保存配置目录之内 → 写 §7 assetsDir 字段（加载端按字段优先解析）。
ExitCode finishJob(engine::EngineConfig& config, const core::Image& image,
                   const JobOutput& out, const std::string& saveConfigPath, const JobOptions& opt,
                   const annotation::AnnotationSet* annotations) {
    core::Image work = image;
    if (config.layers.has_value() && !config.layers->empty()) {
        core::Image composed;
        if (const ExitCode ec = buildLayerCanvas(config, /*configPath=*/"", work, composed, opt); ec != ExitCode::Ok)
            return ec;
        work = std::move(composed);
    }
    if (!saveConfigPath.empty()) {
        if (const std::string rel = relativizeAssetsDirForSave(saveConfigPath, opt.assetsDir.value_or("")); !rel.empty())
            config.assetsDir = rel;
        const ExitCode ec = saveConfigToFile(config, saveConfigPath, opt, annotations);
        if (ec != ExitCode::Ok) return ec;
        if (opt.assetsDir.has_value())
            return bundleLayerAssets(config, /*configPath=*/"", saveConfigPath, opt);
        return ec;
    }
    return executeJob(config, work, out, opt, annotations);
}

} // namespace idc::cli
