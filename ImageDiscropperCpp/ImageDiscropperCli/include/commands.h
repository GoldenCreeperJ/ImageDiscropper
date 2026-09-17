// ============================================================================
// 文件：include/commands.h
// 作用：声明各子命令入口与 CLI 顶层入口 cliMain。命令与终稿 §4.2 / guideline §4.2 一一对应：
//       extract(L1) / erase(L2) / grid(L3) / config(配置文件)。
// 分块依据：
//   - 每个子命令单独一个 .cpp（src/commands/），只负责「解析本命令参数 + 装配 EngineConfig
//     + 交由 job 执行」，彼此不耦合，也不含引擎逻辑（A-0.1）。
//   - cliMain 是唯一的顶层编排点（全局选项 / help / version / 分派 / 异常兜底），main.cpp
//     仅把 argv 转发给它；集成测试直接调用 cliMain 做进程内端到端验证（无需启动子进程）。
// 说明：命令函数返回「退出码整数」（已含 §5 映射），可直接作为 main 的返回值。
// ============================================================================
#pragma once

#include <string>
#include <vector>

#include "arg_parser.h"

namespace idc::cli {

// L1 标准提取：--input + (--rect|--hband|--vband) + --output（极性恒 keep，输出单图）。
int cmdExtract(const std::vector<std::string>& tokens, const GlobalOptions& go);

// L2 反向剔除：--input + (--rect 可重复|--hband|--vband) + [--merge] + 输出（极性恒 remove）。
int cmdErase(const std::vector<std::string>& tokens, const GlobalOptions& go);

// L3 网格分割：--input + --grid + (--keep|--remove|--order) + 排序/余量/合成等。
int cmdGrid(const std::vector<std::string>& tokens, const GlobalOptions& go);

// 配置文件：config --load <file> 执行；配合 --save-config 可加载后再序列化（转换/校验）。
int cmdConfig(const std::vector<std::string>& tokens, const GlobalOptions& go);

// CLI 顶层入口：解析全局选项 → 处理 help/version → 定位子命令 → 分派 → 未捕获异常兜底(5)。
// args 为不含程序名的 argv。返回退出码整数。
int cliMain(const std::vector<std::string>& args);

} // namespace idc::cli
