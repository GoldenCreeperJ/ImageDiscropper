// ============================================================================
// 文件：src/layer_args.cpp
// 作用：layer_args.h 的实现——读取 --layer / --layers 两类命令行图层选项，
//       经 Core 的 parseImageLayersFromJsonText 解析为 ImageLayer 列表并累积。
// 分块依据：CLI 不解析 JSON（委托 Core），本文件只做「取选项值 + 读文件 + 调 Core + 汇总错误」，
//       无合成 / 落位 / 引擎语义（CONTRIBUTING.md「分层纪律」）。文件读入用标准库 ifstream（与 job 读图同层依赖）。
// 常量层：选项名取 texts::opt（不可译）、报因文案经门面 IDC_CTR(msg, ...) 取 texts::msg。
// ============================================================================
#include "layer_args.h"

#include <fstream>
#include <sstream>
#include <vector>

#include "canvas/layer.h"      // canvas::ImageLayer（Core 合成 / 落位数据模型）
#include "engine/engine_config_json.h" // engine::parseImageLayersFromJsonText（图层段级解析，委托 Core）
#include "command_support.h"   // readFileText（整文件读入，与 annotation_args 共用）
#include "i18n/cli_texts.h"
#include "i18n/cli_i18n.h"

namespace idc::cli {

// 选项名简写（不可译契约；报因文案走 IDC_CTR）。
namespace opts = texts::opt;

bool hasLayerOptions(const ArgParser& args) {
    return args.has(opts::kLayer) || args.has(opts::kLayers);
}

bool collectLayers(const ArgParser& args, std::vector<canvas::ImageLayer>& out, std::string& err,
                   const int basisWidth, const int basisHeight) {
    // 1) 内联：--layer 可重复，逐条 JSON 对象（或数组）解析后追加（保持出现顺序）。
    for (const std::string& item : args.getAll(opts::kLayer)) {
        if (!engine::parseImageLayersFromJsonText(item, out, err, basisWidth, basisHeight))
            return false; // err 由 Core 给出（语法错误 / 根类型非法 / 无有效条目 / 百分比无基准）。
    }

    // 2) 文件：--layers 指向「根为图层数组（或单对象）」的 JSON 文件；可重复出现亦逐个累积。
    for (const std::string& path : args.getAll(opts::kLayers)) {
        std::string text;
        if (!readFileText(path, text)) {
            err = IDC_CTR(msg, kErrLayerFileUnreadable) + path;
            return false;
        }
        if (!engine::parseImageLayersFromJsonText(text, out, err, basisWidth, basisHeight))
            return false;
    }
    return true;
}

// 追加层 zOrder 定序：未给（§7 缺省 0）→ 按「既有最大 zOrder + 1」起自增续尾；显式非 0 尊重原值。
// existing 传配置自带层（无则空表，追加层从 0 起自增）。
void finalizeAppendedLayers(const std::vector<canvas::ImageLayer>& existing,
                            std::vector<canvas::ImageLayer>& appended) {
    int maxZ = -1;
    for (const canvas::ImageLayer& l : existing) maxZ = std::max(maxZ, l.zOrder);
    for (std::size_t i = 0; i < appended.size(); ++i) {
        canvas::ImageLayer& l = appended[i];
        if (l.zOrder == 0) l.zOrder = maxZ + 1 + static_cast<int>(i);
        else maxZ = std::max(maxZ, l.zOrder);
    }
}

} // namespace idc::cli
