// ============================================================================
// 文件：include/generate_args.h
// 作用：把命令行「图像生成选项」解析为 Core 的 generate::PatternParams，并据此合成一张画布图
//       ——三层命令（extract/erase/grid）与 config 命令、以及 new 命令共用的「生成入参装配」薄胶水。
//       与 annotation_args.h 同构：都是「选项 token → 调 Core → 产出结果」的参数装配模块，本身不含
//       任何像素 / 图案逻辑（生成算法全部委托 Core 的 generate::makePattern，CONTRIBUTING.md「分层纪律」）。
// 分块依据：
//   - 只做「取选项值 + 值解析（复用 value_parser 的 parseColor）+ 调 Core 生成 + 汇总错误」，
//     尺寸 / 图案名 / 方向的「字符串 → 类型」纯转换在本文件匿名 ns 内实现（体量小、仅此模块用）。
//   - resolveInputImage 统一「输入图像来源」：--input 读文件（委托 job::loadInputImage），
//     或用 --size(+--pattern…) 生成画布；命令层据此把「必填 / 互斥」校验与取图一步收敛，避免四命令重复。
// 说明：选项集合与 GUI「新建」对话框字段一一对应——
//       --size WxH | --pattern <kind> | --orientation horizontal|vertical|diagonal-down|diagonal-up |
//       --bg <#RRGGBB/#AARRGGBB> | --fg <同> | --period N | --line-width N | --dot-radius N |
//       --seed N（噪声种子，>=0，仅 --pattern noise 生效）| --noise full-color|grayscale|two-color（仅 noise）。
//       底色 --bg 的 alpha 生效（<FF 即透明 / 半透明画布）。缺省：pattern=solid、bg=白(不透明)、fg=黑、
//       period=32、line-width=1、dot-radius=2、seed=0、noise=full-color（与 Core PatternParams 默认一致）。
// ============================================================================
#pragma once

#include <string>

#include "core/image.h"
#include "generate/pattern.h" // generate::PatternParams（供 new --save-config 序列化）

#include "arg_parser.h"
#include "exit_code.h"
#include "job.h"

namespace idc::cli {

// 是否给出图像生成选项（以 --size 为触发标志：合成画布必须有尺寸）。
bool hasGenerateOptions(const ArgParser& args);

// 解析图像生成选项并合成一张 RGBA 画布图（委托 Core generate::makePattern）。
//   --size 必填且须为 WxH 正整数、且不超 Core 上限；图案名 / 方向 / 颜色 / 周期等逐格式校验。
//   任一非法 → 写简短 err 返回 false（命令层转退出码 1）；合法则 out 得到非空图像返回 true。
bool buildGeneratedImage(const ArgParser& args, core::Image& out, std::string& err);

// 仅解析图像生成选项为尺寸 + PatternParams（不落像素、不生成图）；供 `new --save-config`
//   将生成配方序列化进配置。校验与 buildGeneratedImage 同口径（任一非法 → false 并写 err）。
bool parseGenerateParams(const ArgParser& args, int& w, int& h,
                         generate::PatternParams& params, std::string& err);

// 统一「输入图像来源」解析：给出 --input 则读文件（失败退出码 3）；否则用生成选项合成画布
//   （失败退出码 1）。二者必填其一、且互斥的前置校验由命令层完成，本函数只做取图 + 打印错误。
//   返回退出码；失败已按约定格式打印。成功后 out 为非空图像（尺寸即画布尺寸，供后续带 / 网格装配）。
ExitCode resolveInputImage(const ArgParser& args, core::Image& out, const JobOptions& opt);

} // namespace idc::cli
