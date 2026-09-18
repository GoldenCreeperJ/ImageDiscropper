// ============================================================================
// 文件：include/help.h
// 作用：集中生成帮助与版本文本。帮助文本是 CLI 的「自解释入口」，
//       术语须与 SPEC §2.2 一致（切割线 / 带 / 单元 / 十字带 / 极性 / 坍缩 / 重排）。
// 分块依据：帮助 / 版本是纯文本输出，独立于参数解析与命令执行；单列一处便于统一维护
//       命令清单与示例，避免散落在各命令文件里造成措辞不一致。
// ============================================================================
#pragma once

#include <string>

namespace idc::cli {

// 顶层帮助：工具定位 + 三层模式（L1/L2/L3）+ 子命令清单 + 全局选项 + 各层示例。
void printTopHelp();

// 版本信息：形如 "idc <version> (core <core-version>)"。
// 版本号来自编译期宏 IDC_CLI_VERSION / IDC_CORE_VERSION（与构建系统一致）。
void printVersion();

// 子命令帮助：命令语义（对应哪一层 / 模式）+ 全部选项（含义 / 默认 / 必填）+ 示例 + 常见错误。
// cmd 为子命令名（extract / erase / grid / config）；未知命令回退为顶层帮助。
void printCommandHelp(const std::string& cmd);

} // namespace idc::cli
