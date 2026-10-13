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
//       （include/i18n/cli_texts.h），本文件不再直接写常量名也不写字面量——键由预处理器拼出，
//       常量改名即未声明标识符，编译期按落点直接报回（zh-CN 下表为空，输出与改写前逐字相同）。
//       未提取项：后缀 L".assets"（宽字面量，与 char[] 类型不符；GUI 侧同契约读的是
//       gui_texts.h ident::kAssetsDirSuffix，两处必须同字）、比较用的 ".."、以及语言中性的
//       "x" / " → " 格式碎片（随语句局部阅读更直观）；尺寸括注的全角「（）」已入 msg 组供译。
// ============================================================================
#include "job.h"

#include <filesystem>
#include <iostream>
#include <optional>

#include "engine/engine_config_json.h" // saveEngineConfig
#include "engine/image_io.h"           // readImageFile
#include "engine/export.h"             // exportImage（new 命令复用同一落盘通道）
#include "engine/composition.h"        // Composition / Placement / CompositionParams
#include "engine/region.h"             // RectRegion（全画布源/目标区域）
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

// 在候选目录中定位 File 层源图：先「配置所在目录 / 相对名」，再「<配置名>.assets / 相对名」。
// 命中首个存在者返回其路径；均未命中返回空 path（调用方据此降级占位或报错）。
std::filesystem::path resolveAssetPath(const std::filesystem::path& baseDir,
                                       const std::filesystem::path& assetsDir,
                                       const std::string& assetName) {
    std::error_code ec;
    // assetName 为 UTF-8 相对名（来自配置 JSON）：经 u8path → 宽字符拼接，非 ASCII 资源名可正确定位。
    if (const std::filesystem::path direct = baseDir / std::filesystem::u8path(assetName); std::filesystem::exists(direct, ec))
        return direct;
    if (const std::filesystem::path bundled = assetsDir / std::filesystem::u8path(assetName); std::filesystem::exists(bundled, ec))
        return bundled;
    return {};
}

// 由配置路径推导其所在目录（父目录；无父目录时退化为当前目录）。configPath 按 UTF-8 解析。
std::filesystem::path configDirectory(const std::string& configPath) {
    const std::filesystem::path p = std::filesystem::u8path(configPath);
    std::filesystem::path parent = p.parent_path();
    return parent.empty() ? std::filesystem::path(".") : parent;
}

} // namespace

// 读取输入图像；失败按输入文件错误（3）报告。
ExitCode loadInputImage(const std::string& path, core::Image& out, const JobOptions& opt) {
    if (path.empty()) {
        reportError(makeError(ExitCode::ArgError, IDC_CTR(msg, kErrNoInputPath),
                              "", IDC_CTR(msg, kHintUseInputFile)));
        return ExitCode::ArgError;
    }
    // Core 解码：支持 PNG/JPEG/BMP/WebP 等；文件不存在 / 非图像 / 不可读均返回 false。
    if (!engine::readImageFile(path, out)) {
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
                             const engine::CompositionParams& emit, const JobOptions& opt) {
    if (image.empty()) {
        reportError(makeError(ExitCode::OutputError, IDC_CTR(msg, kErrEmptyImage), IDC_CTR(msg, kDetTarget) + outPath,
                              IDC_CTR(msg, kHintCheckGenArgs)));
        return ExitCode::OutputError;
    }
    // 单片段：源区域与目标区域均为整幅画布 [0,W)×[0,H)；canvas 即图像尺寸（合并单图）。
    engine::Composition comp;
    comp.canvasWidth = image.width();
    comp.canvasHeight = image.height();
    comp.format = emit.format;         // 扩展名不明时的回退格式。
    comp.quality = emit.quality;
    comp.padColor = emit.padColor;     // JPEG/BMP 压平背景（合并画布空位填充色，全幅覆盖时用不到）。
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

// 多层画布合成：逐层解码 File 叠加层源图像（相对配置目录 / <配置名>.assets）注入 decodedPixels → 以 base 为底 composeCanvas 展平。
ExitCode buildLayerCanvas(const engine::EngineConfig& config, const std::string& configPath,
                          const core::Image& base, core::Image& out, const JobOptions& opt) {
    if (!config.layers.has_value()) {
        reportError(makeError(ExitCode::RuntimeError, IDC_CTR(msg, kErrLayerCompose), IDC_CTR(msg, kDetNoLayers),
                              IDC_CTR(msg, kHintInternalLayersOnly)));
        return ExitCode::RuntimeError;
    }
    // config 为 const（不能改写回），但需把解码出的 File 层像素注入后再合成——取可变副本作工作数组。
    std::vector<canvas::ImageLayer> layers = *config.layers;
    const std::filesystem::path cfgPath = std::filesystem::u8path(configPath);
    const std::filesystem::path baseDir = configDirectory(configPath);
    std::filesystem::path assetsName = cfgPath.stem();
    assetsName += std::filesystem::path(L".assets");   // <配置名>.assets（保持宽字符，避免窄串码页失配）
    const std::filesystem::path assetsDir = baseDir / assetsName;

    for (auto& layer : layers) {
        if (layer.sourceKind != canvas::LayerSourceKind::File || layer.fileName.empty())
            continue; // 配方 / 占位层无需解码；空名视为占位（降级）。
        if (!isSafeRelativeAsset(layer.fileName)) {
            reportError(makeError(ExitCode::InputError, IDC_CTR(msg, kErrAssetPathEscape),
                                  IDC_CTR(msg, kDetLayerFileName) + layer.fileName,
                                  IDC_CTR(msg, kHintAssetRelative)));
            return ExitCode::InputError;
        }
        const std::filesystem::path resolved = resolveAssetPath(baseDir, assetsDir, layer.fileName);
        if (resolved.empty()) {
            if (opt.verbose)
                std::cerr << IDC_CTR(msg, kLogPrefix) << IDC_CTR(msg, kLogAssetMissing) << layer.fileName << "\n";
            continue; // 缺文件 → decodedPixels 保持空，composeCanvas 按占位（空图）处理。
        }
        core::Image px;
        if (!engine::readImageFile(resolved.u8string(), px)) {
            reportError(makeError(ExitCode::InputError, IDC_CTR(msg, kErrAssetDecode),
                                  IDC_CTR(msg, kDetPath) + resolved.u8string(),
                                  IDC_CTR(msg, kHintInputReadable)));
            return ExitCode::InputError;
        }
        layer.decodedPixels = std::move(px);
    }

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
    layer.sourceKind = canvas::LayerSourceKind::File;   // 运行时把外部输入图视作 File 源注入
    layer.decodedPixels = io;
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

// 资源打包（--bundle-assets）：把各 File 层源图按相对名复制进 <saveConfigPath 同名>.assets/。
ExitCode bundleLayerAssets(const engine::EngineConfig& config, const std::string& configPath,
                           const std::string& saveConfigPath, const JobOptions& opt) {
    if (!config.layers.has_value()) return ExitCode::Ok; // 无图层可打包，空操作。
    const std::filesystem::path cfgPath = std::filesystem::u8path(configPath);
    const std::filesystem::path baseDir = configDirectory(configPath);
    std::filesystem::path srcAssetsName = cfgPath.stem();
    srcAssetsName += std::filesystem::path(L".assets");
    const std::filesystem::path srcAssetsDir = baseDir / srcAssetsName;
    const std::filesystem::path savePath = std::filesystem::u8path(saveConfigPath);
    std::filesystem::path outAssetsName = savePath.stem();
    outAssetsName += std::filesystem::path(L".assets");
    const std::filesystem::path outAssetsDir =
        (savePath.parent_path().empty() ? std::filesystem::path(".") : savePath.parent_path()) / outAssetsName;

    std::error_code ec;
    for (const auto& layer : *config.layers) {
        if (layer.sourceKind != canvas::LayerSourceKind::File || layer.fileName.empty())
            continue;
        if (!isSafeRelativeAsset(layer.fileName)) {
            reportError(makeError(ExitCode::ArgError, IDC_CTR(msg, kErrAssetEscapeReject),
                                  IDC_CTR(msg, kDetLayerFileName) + layer.fileName,
                                  IDC_CTR(msg, kHintBundleRelative)));
            return ExitCode::ArgError;
        }
        const std::filesystem::path src = resolveAssetPath(baseDir, srcAssetsDir, layer.fileName);
        if (src.empty()) {
            reportError(makeError(ExitCode::InputError, IDC_CTR(msg, kErrAssetMissingBundle),
                                  IDC_CTR(msg, kDetMissing) + layer.fileName,
                                  IDC_CTR(msg, kHintBundleNeedsSource)));
            return ExitCode::InputError;
        }
        const std::filesystem::path dst = outAssetsDir / std::filesystem::u8path(layer.fileName);
        if (dst.parent_path() != outAssetsDir && !std::filesystem::create_directories(dst.parent_path(), ec)) {
            reportError(makeError(ExitCode::OutputError, IDC_CTR(msg, kErrMkdirAsset),
                                  IDC_CTR(msg, kDetPath) + dst.parent_path().u8string(), ec.message()));
            return ExitCode::OutputError;
        }
        std::filesystem::copy_file(src, dst, std::filesystem::copy_options::overwrite_existing, ec);
        if (ec) {
            reportError(makeError(ExitCode::OutputError, IDC_CTR(msg, kErrCopyAsset),
                                  src.u8string() + " → " + dst.u8string(), ec.message()));
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
    // FR-1：切割前把 preprocess / 标注物化为「工作图 work」。
    //   几何流水线（runEngine）与落盘（exportImage）必须共用同一张 work——runEngine 内部会按预处理
    //   产出尺寸（rotate ±90 宽高互换 / resize 改尺寸）计算裁剪区域，若仍拿未预处理的原图落盘，
    //   裁剪矩形会套在尺寸/像素都不符的原图上，导致裁错内容或越界。物化后清空 config.preprocess，
    //   令 runEngine 不再二次应用（与 GUI「引擎收已预处理图 + 空 preprocess」同一约定）。
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
    const engine::EngineResult result = engine::runEngine(*work, config);
    if (!result.ok) {
        // Core 业务错误（E-1/E-5/E-6/E-7 等）统一归为运行时错误（2）。
        reportError(makeError(ExitCode::RuntimeError, IDC_CTR(msg, kErrEngineFailed), result.error,
                              IDC_CTR(msg, kHintCheckSelection)));
        return ExitCode::RuntimeError;
    }

    // 显式要求坍缩但不可坍缩 → 退出码 2（固定错误文本）。
    if (opt.explicitCollapse && !result.collapsible) {
        reportError(makeError(ExitCode::RuntimeError, IDC_CTR(msg, kErrNotCollapsible),
                              IDC_CTR(msg, kDetNotCollapsible),
                              IDC_CTR(msg, kHintUseRearrange)));
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

    // 正常结果输出到 stdout（quiet 时抑制；CONTRIBUTING.md「分层纪律」）。
    if (!opt.quiet) {
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
                          const annotation::AnnotationSet* annotations) {
    if (!engine::saveEngineConfig(path, config, annotations)) {
        reportError(makeError(ExitCode::OutputError, IDC_CTR(msg, kErrSaveConfig), IDC_CTR(msg, kDetPath) + path,
                              IDC_CTR(msg, kHintSaveConfigPath)));
        return ExitCode::OutputError;
    }
    if (!opt.quiet) std::cout << IDC_CTR(msg, kLogConfigSaved) << path << "\n";
    return ExitCode::Ok;
}

// 作业收尾：--save-config 优先（dry-run，仅序列化不执行）；否则执行作业。
ExitCode finishJob(engine::EngineConfig& config, const core::Image& image,
                   const JobOutput& out, const std::string& saveConfigPath, const JobOptions& opt,
                   const annotation::AnnotationSet* annotations) {
    if (!saveConfigPath.empty()) return saveConfigToFile(config, saveConfigPath, opt, annotations);
    return executeJob(config, image, out, opt, annotations);
}

} // namespace idc::cli
