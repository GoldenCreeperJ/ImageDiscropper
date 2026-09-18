// ============================================================================
// 文件：include/job.h
// 作用：定义 CLI 与 Core 引擎之间的「执行桥」——把一份装配好的 EngineConfig 跑通并落盘，
//       并把 Core 的结果 / 失败映射为退出码。这是 CLI「调用 Core + 返回退出码」职责的落点。
// 分块依据：
//   - 三层命令（extract/erase/grid）与 config 命令共用同一条执行路径（NFR-0：模式即参数
//     预设），故把「读图 → runEngine → 坍缩校验 → exportImage → 退出码」抽到本模块，
//     命令层只负责「解析参数 + 装配 EngineConfig」，不重复执行逻辑（CONTRIBUTING.md「分层纪律」）。
//   - 本模块只调用 Core 公开 API（readImageFile / runEngine / exportImage /
//     save/loadEngineConfig），不含任何切割 / 合成 / 编码实现。
// 说明：坍缩不可行的处理对齐 Cli README「合并重排」——用户显式 --merge collapse 而 Core 判定
//       不可坍缩时返回退出码 2 并提示改用 rearrange（仅 L3 时 Core 会自动改用重排，CLI 仍须显式报错）。
// ============================================================================
#pragma once

#include <string>

#include "core/image.h"
#include "engine/engine.h" // EngineConfig（聚合各阶段头）

#include "exit_code.h"

namespace idc::cli {

// ---------------------------------------------------------------------------
// JobOutput：一次作业的输出目标。
//   separate=true  → path 为「目标文件夹」（Core 自动创建，多图分离写入）。
//   separate=false → path 为「单图文件路径」（合并导出；格式优先由扩展名推断）。
// ---------------------------------------------------------------------------
struct JobOutput {
    std::string path;
    bool separate{false};
};

// ---------------------------------------------------------------------------
// JobOptions：执行期的行为开关（不影响 Core 计算，只影响 CLI 的判定与输出）。
// ---------------------------------------------------------------------------
struct JobOptions {
    bool explicitCollapse{false}; // 用户是否显式要求 collapse（不可坍缩时 → 退出码 2）。
    bool verbose{false};          // 输出详细进度到 stderr。
    bool quiet{false};            // 静默：抑制正常结果输出（错误仍写 stderr）。
};

// 读取输入图像（Core readImageFile）。成功返回 Ok 并填充 out；
// 失败（不存在 / 不可读 / 非图像）返回 InputError(3) 并已按错误格式打印错误。
ExitCode loadInputImage(const std::string& path, core::Image& out, const JobOptions& opt);

// 执行作业：以 image 的实际尺寸校正 config.source → runEngine → 坍缩校验 → exportImage。
// 返回退出码；失败时已按错误格式打印错误。config 以引用传入（内部会写入 source 尺寸）。
ExitCode executeJob(engine::EngineConfig& config, const core::Image& image,
                    const JobOutput& out, const JobOptions& opt);

// 把一份 EngineConfig 序列化到 JSON 文件（Core saveEngineConfig），用于 --save-config dry-run。
// 成功返回 Ok 并（非 quiet 时）提示；失败（路径空 / 写盘失败）返回 OutputError(4)。
ExitCode saveConfigToFile(const engine::EngineConfig& config, const std::string& path,
                          const JobOptions& opt);

// 作业收尾（三层命令 + config 共用）：若 saveConfigPath 非空则序列化配置并返回（dry-run，
// 不执行切割）；否则执行 executeJob。使命令层收尾逻辑单点化（CONTRIBUTING.md「分层纪律」 不重复）。
ExitCode finishJob(engine::EngineConfig& config, const core::Image& image,
                   const JobOutput& out, const std::string& saveConfigPath, const JobOptions& opt);

} // namespace idc::cli
