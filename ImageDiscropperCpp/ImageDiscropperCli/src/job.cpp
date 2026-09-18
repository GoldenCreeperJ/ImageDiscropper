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
// ============================================================================
#include "job.h"

#include <iostream>

#include "engine/engine_config_json.h" // saveEngineConfig
#include "engine/image_io.h"           // readImageFile

#include "cli_error.h"

namespace idc::cli {

// 读取输入图像；失败按输入文件错误（3）报告。
ExitCode loadInputImage(const std::string& path, core::Image& out, const JobOptions& opt) {
    if (path.empty()) {
        reportError(makeError(ExitCode::ArgError, "缺少输入图像路径",
                              "", "请用 --input <file> 指定输入图像"));
        return ExitCode::ArgError;
    }
    // Core 解码：支持 PNG/JPEG/BMP/WebP 等；文件不存在 / 非图像 / 不可读均返回 false。
    if (!engine::readImageFile(path, out)) {
        reportError(makeError(ExitCode::InputError, "无法读取输入图像",
                              "路径：" + path,
                              "请确认文件存在、可读，且为受支持的图像格式（PNG/JPEG/BMP/WebP 等）"));
        return ExitCode::InputError;
    }
    if (opt.verbose)
        std::cerr << "idc: 已读取输入图像 " << path
                  << "（" << out.width() << "x" << out.height() << "）\n";
    return ExitCode::Ok;
}

// 执行作业：校正 source → runEngine → 坍缩校验 → exportImage → 退出码。
ExitCode executeJob(engine::EngineConfig& config, const core::Image& image,
                    const JobOutput& out, const JobOptions& opt) {
    // 以实际图像尺寸校正 source（若含预处理，runEngine 内部会以预处理产出尺寸再覆盖）。
    config.source.width = image.width();
    config.source.height = image.height();

    if (opt.verbose) std::cerr << "idc: 运行 Grid-Selection-Emit 引擎...\n";
    const engine::EngineResult result = engine::runEngine(image, config);
    if (!result.ok) {
        // Core 业务错误（E-1/E-5/E-6/E-7 等）统一归为运行时错误（2）。
        reportError(makeError(ExitCode::RuntimeError, "引擎执行失败", result.error,
                              "请检查切割 / 选择参数是否产生了有效的保留区域"));
        return ExitCode::RuntimeError;
    }

    // 显式要求坍缩但不可坍缩 → 退出码 2（固定错误文本）。
    if (opt.explicitCollapse && !result.collapsible) {
        reportError(makeError(ExitCode::RuntimeError, "当前选择集不满足坍缩条件",
                              "Core 返回：保留集不构成整行或整列的补集",
                              "请改用 --merge rearrange，或调整切割线使删除区域覆盖整行/整列"));
        return ExitCode::RuntimeError;
    }

    if (opt.verbose)
        std::cerr << "idc: 导出到 " << out.path
                  << (out.separate ? "（分离多图）" : "（合并单图）") << "\n";

    // 落盘：分离 → 目标文件夹（Core 自动创建）；合并 → 单图文件路径。
    if (!engine::exportImage(result.composition, image, out.path)) {
        reportError(makeError(ExitCode::OutputError, "导出失败", "目标：" + out.path,
                              out.separate ? "请确认输出目录可创建且可写（未被同名文件占用、权限足够）"
                                           : "请确认输出路径的父目录存在、文件未被占用、磁盘未满"));
        return ExitCode::OutputError;
    }

    // 正常结果输出到 stdout（quiet 时抑制；CONTRIBUTING.md「分层纪律」）。
    if (!opt.quiet) {
        if (out.separate) {
            std::cout << "已导出 " << result.composition.placements.size()
                      << " 张图像到文件夹：" << out.path << "\n";
        } else {
            std::cout << "已导出合并图像：" << out.path << "（"
                      << result.composition.canvasWidth << "x"
                      << result.composition.canvasHeight << "）\n";
        }
    }
    return ExitCode::Ok;
}

// 序列化配置到 JSON 文件（--save-config dry-run）；失败按输出失败（4）报告。
ExitCode saveConfigToFile(const engine::EngineConfig& config, const std::string& path,
                          const JobOptions& opt) {
    if (!engine::saveEngineConfig(path, config)) {
        reportError(makeError(ExitCode::OutputError, "无法保存配置文件", "路径：" + path,
                              "请确认路径的父目录存在且可写"));
        return ExitCode::OutputError;
    }
    if (!opt.quiet) std::cout << "已保存配置到：" << path << "\n";
    return ExitCode::Ok;
}

// 作业收尾：--save-config 优先（dry-run，仅序列化不执行）；否则执行作业。
ExitCode finishJob(engine::EngineConfig& config, const core::Image& image,
                   const JobOutput& out, const std::string& saveConfigPath, const JobOptions& opt) {
    if (!saveConfigPath.empty()) return saveConfigToFile(config, saveConfigPath, opt);
    return executeJob(config, image, out, opt);
}

} // namespace idc::cli
