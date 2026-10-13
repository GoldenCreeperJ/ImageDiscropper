// ============================================================================
// 文件：include/i18n/cli_texts.h（CLI 常量层 · 字符串 · 聚合转发头）
// 作用：CLI 层全部字符串常量的统一入口。原单文件过长（--help 散文 + 全量选项 / 报因），
//       按「可译 / 契约 + 分组」拆为两半后，本头转为**薄聚合转发**：仅 #include 两个半文件，
//       消费方仍 `#include "i18n/cli_texts.h"` 并以 `texts::opt::kXxx` / `texts::help::kTextXxx`
//       等形态引用，引用点与拆分前逐字一致（只是目录前缀由扁平改为 i18n/）。
// 两半归属：
//       · cli_texts_base.h —— 不可译契约组 opt / cmd / env + 可译组 msg；
//       · cli_texts_help.h —— help 组（用法列 kUse*〔契约不译〕/ 描述列 kDesc* / 小节 lead kLead* / 散文块 kText*）。
// 架构定位：CLI 顶层 i18n 基建（与 cli_i18n.h / cli_nums.h 同族，统一住 include/i18n）。
// 目录与对账：本层可译组的英文表见 src/i18n/lang_en.cpp（msg）与 src/i18n/lang_en_help.cpp（help），
//       由 i18n::installCatalogs() 先后登记；tests/test_i18n_catalog.cpp 逐文件对账全部半文件。
// ============================================================================
#pragma once

#include "cli_texts_base.h"
#include "cli_texts_help.h"
