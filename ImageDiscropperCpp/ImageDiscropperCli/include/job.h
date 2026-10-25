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
// 说明：坍缩不可行的处理对齐 SPEC §8.1 CLI 行为契约（可行性口径见 §4.4）——用户显式 --merge collapse 而 Core 判定
//       不可坍缩时返回退出码 2 并提示改用 rearrange（仅 L3 时 Core 会自动改用重排，CLI 仍须显式报错）。
// ============================================================================
#pragma once

#include <string>

#include "core/image.h"
#include "core/percent.h"      // PercentHints（配置中以百分比书写的几何字段，保存时原样回写）
#include "engine/engine.h" // EngineConfig（聚合各阶段头）

#include "exit_code.h"

namespace idc::annotation { class AnnotationSet; }

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
    bool verbose{false};          // 输出详细进度到 stderr（默认静默；--verbose/-v 打开，见 makeJobOptions）。
    bool strict{false};             // --strict（伞形）：图层资源缺失报错退出码 3，「尺寸错配静默裁切」（→输入错误码 3）与「零产出」（→运行错误码 2）由 WARN 升级为报错。
    // --assets-dir 三态（缺省 nullopt＝不打包、纯约定查找；flag 空串＝打包进默认目录；非空＝自定义目录：
    //   加载时插查找链最前、打包输出到该目录）。
    std::optional<std::string> assetsDir;
};

// 读取输入图像（Core readImageFile）。成功返回 Ok 并填充 out；
// 失败（不存在 / 不可读 / 非图像）返回 InputError(3) 并已按错误格式打印错误。
ExitCode loadInputImage(const std::string& path, core::Image& out, const JobOptions& opt);

// 把一张「已生成的图像」作为合并单图落盘（new 命令专用）：内部构造覆盖全画布的单片段
// Composition 复用 Core exportImage，从而共享「扩展名推断格式 / 自动建父目录 / JPEG 压平」等行为
// （exportParams.format 仅作扩展名不明时的回退）。失败返回 OutputError(4) 并已打印错误。
ExitCode writeGeneratedImage(const core::Image& image, const std::string& outPath,
                             const engine::CompositionParams& exportParams, const JobOptions& opt);

// 执行作业：按需物化工作图 work（preprocess / 标注；两者皆无时直指入参图、不深拷贝）
// → 以 work 尺寸校正 config.source → runEngine(work) → 坍缩校验 → exportImage(work)。
// 几何与落盘共用同一张 work，确保含 rotate/resize 的配置坐标一致。返回退出码；
// 失败时已按错误格式打印错误。config 以引用传入（内部会写入 source 尺寸并清空 preprocess）。
ExitCode executeJob(engine::EngineConfig& config, const core::Image& image, const JobOutput& out, const JobOptions& opt, const annotation::AnnotationSet* annotations = nullptr);

// 多层画布合成（SPEC §7 layers 段）：config 带 layers 时，按 configPath 所在目录（及同名
//  <配置名>.assets/ 伴随目录）逐层解码 File 叠加层源图像注入 decodedPixels，再交 Core canvas::composeCanvas
//  以传入的 **base（画布底图，可为空＝仅背景）**为基座、按 zOrder 叠上层展平为 config.source 尺寸的画布写入 out
//  （引擎仍只收一张展平画布，签名不变）。缺文件默认降级为占位并输出一条 WARN 汇总（与 verbose 无关）；
//  加 --strict（opt.strict）则缺文件报输入错误退出码 3。越界（绝对路径 / `..`）文件层名 → InputError(3)。
//  composeCanvas 产空图（尺寸非法）→ RuntimeError(2)。仅在 config.layers 有值时由命令层调用。
ExitCode buildLayerCanvas(const engine::EngineConfig& config, const std::string& configPath,
                          const core::Image& base, core::Image& out, const JobOptions& opt);

// 底图展平（SPEC §7 generate 段，同构 canvas::ImageLayer）：config.base 非空时交 Core canvas::composeBase 展平
//  为 config.source 尺寸写入 out（纯图案底＝按 source 尺寸现算）。
//  底图不外传文件：若配置带内层落位（hasImagePlacement）却未提供外部源图（无 --input），无法展平落位 → InputError(3)。
//  composeBase 产空图（尺寸非法）→ RuntimeError(2)。仅在 config.base 有值且未给 --input/--size 时由命令层调用。
ExitCode buildBaseCanvas(const engine::EngineConfig& config, core::Image& out, const JobOptions& opt);

// 把已解析的外部底图源（--input/--size 产物 io）按 config.base 的内层落位声明摆入 config.source 尺寸画布：
//  运行时以 File 源注入 decodedPixels 后交 composeBase 展平，原地写回 io（**配置不含图片路径，图凭运行时供给**）。
//  无落位声明 → 原样返回（恒等底图）。composeBase 产空图 → RuntimeError(2)。仅在 config.base 带落位时由命令层调用。
ExitCode placeInputIntoBase(const engine::EngineConfig& config, core::Image& io, const JobOptions& opt);

// 资源打包（--assets-dir 出现时，与 --save-config 同用）：把 config 内各 File 层源图按相对名复制到
//  opt.assetsDir 指定的目录（相对路径按保存配置所在目录解析；flag 空串 → <保存配置名>.assets/，保留子目录相对路径）。
//  源图缺失 → InputError(3)；越界文件层名 → ArgError(1)；复制 / 建目录失败 → OutputError(4)；src==dst 同路径 → 幂等跳过。
ExitCode bundleLayerAssets(const engine::EngineConfig& config, const std::string& configPath,
                           const std::string& saveConfigPath, const JobOptions& opt);

// 把 --assets-dir=path 的目录在「保存配置目录之内」时相对化（可写 §7 assetsDir 字段）；
// 相对路径原样返回；目录之外 → 空串（字段契约＝配置内声明，不写）。saveConfigPath 空 → 返回空。
std::string relativizeAssetsDirForSave(const std::string& saveConfigPath, const std::string& overrideDir);

// 把一份 EngineConfig 序列化到 JSON 文件（Core saveEngineConfig），用于 --save-config dry-run。
// annotations 非空时附带标注段（往返不丢）；percentHints 非空时把未改动的百分比几何字段原样回写（"50%"）。
// 成功返回 Ok 并提示（结果行恒写 stdout）；失败（路径空 / 写盘失败）返回 OutputError(4)。
ExitCode saveConfigToFile(const engine::EngineConfig& config, const std::string& path, const JobOptions& opt,
                          const annotation::AnnotationSet* annotations = nullptr,
                          const core::PercentHints* percentHints = nullptr);

// 作业收尾（三层命令 + config 共用）：若 saveConfigPath 非空则序列化配置并返回（dry-run，
// 不执行切割）；否则执行 executeJob。使命令层收尾逻辑单点化（CONTRIBUTING.md「分层纪律」 不重复）。
ExitCode finishJob(engine::EngineConfig& config, const core::Image& image, const JobOutput& out, const std::string& saveConfigPath, const JobOptions& opt, const annotation::AnnotationSet* annotations = nullptr);

} // namespace idc::cli
