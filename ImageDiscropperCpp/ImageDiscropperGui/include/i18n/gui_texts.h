// ============================================================================
// 文件：i18n/gui_texts.h（GUI 常量层 · 字符串 · 聚合转发头）
// 作用：GUI 层全部字符串常量的统一入口。常量体量大（六个分组 · 千余行），
//       按「不可译契约 + 可译 menu/label」与「可译 tooltip/status/msg」分为两半存放，
//       本头作**薄聚合转发**：仅 #include 两个半文件，消费方统一 `#include "i18n/gui_texts.h"`
//       并以 `texts::menu::kXxx` / `texts::label::kXxx` 等形态引用。
// 两半归属：
//       · gui_texts_base.h   —— 不可译契约组 ident + 可译组 menu / label；
//       · gui_texts_detail.h —— 可译组 tooltip / status / msg。
// 架构定位：GUI 顶层 i18n 基建（与 gui_i18n.h / retranslate.h / gui_nums.h 同族，统一住 include/i18n）。
// 目录与对账：本层可译组的英文表见 src/i18n/lang_en.cpp（menu/label）与 src/i18n/lang_en_detail.cpp
//       （tooltip/status/msg），由 i18n::installCatalogs() 先后登记；ident 组永不进表。
//       tests/test_i18n_catalog.cpp 逐文件对账全部半文件。
// ============================================================================
#pragma once

#include "gui_texts_base.h"
#include "gui_texts_detail.h"
