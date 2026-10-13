// ============================================================================
// 文件：src/annotation_args.cpp
// 作用：annotation_args.h 的实现——读取 --annotation / --annotations 两类命令行标注选项，
//       经 Core 的 annotation::parseAnnotationsFromJsonText 解析为标注对象并累积。
// 分块依据：CLI 不解析 JSON（委托 Core），本文件只做「取选项值 + 读文件 + 调 Core + 汇总错误」，
//       无几何 / 无引擎语义（CONTRIBUTING.md「分层纪律」）。文件读入用标准库 ifstream（与 job 读图同层依赖）。
// 常量层：选项名取 texts::opt（不可译）、报因文案经门面 IDC_CTR(msg, ...) 取 texts::msg。
// ============================================================================
#include "annotation_args.h"

#include <filesystem>
#include <fstream>
#include <sstream>

#include "annotation/annotation_set.h" // annotation::Annotation（仅记录类型，供 vector 实例化）
#include "annotation/annotation_json.h" // annotation::parseAnnotationsFromJsonText（标注 JSON 段级解析，归属 annotation 模块）

#include "i18n/cli_texts.h"
#include "i18n/cli_i18n.h"

namespace idc::cli {

// 选项名简写（不可译契约；报因文案走 IDC_CTR）。
namespace opts = texts::opt;

namespace {

// 读取整个文本文件到 outText；文件不存在或不可读返回 false。path 按 UTF-8 → 宽字符开（中文 / 非 ASCII 路径可读）。
bool readFileText(const std::string& path, std::string& outText) {
    const std::ifstream ifs(std::filesystem::u8path(path), std::ios::binary);
    if (!ifs) return false;
    std::ostringstream ss;
    ss << ifs.rdbuf();
    outText = ss.str();
    return true;
}

} // namespace

bool hasAnnotationOptions(const ArgParser& args) {
    return args.has(opts::kAnnotation) || args.has(opts::kAnnotations);
}

bool collectAnnotations(const ArgParser& args, std::vector<annotation::Annotation>& out,
                        std::string& err) {
    // 1) 内联：--annotation 可重复，逐条 JSON 对象解析后追加（保持出现顺序）。
    for (const std::string& item : args.getAll(opts::kAnnotation)) {
        std::vector<annotation::Annotation> parsed;
        if (!annotation::parseAnnotationsFromJsonText(item, parsed, err))
            return false; // err 由 Core 给出（语法错误 / 无有效条目）。
        for (auto& a : parsed) out.push_back(std::move(a));
    }

    // 2) 文件：--annotations 指向「根为标注数组（或单对象）」的 JSON 文件；可重复出现亦逐个累积。
    for (const std::string& path : args.getAll(opts::kAnnotations)) {
        std::string text;
        if (!readFileText(path, text)) {
            err = IDC_CTR(msg, kErrAnnoFileUnreadable) + path;
            return false;
        }
        if (!annotation::parseAnnotationsFromJsonText(text, out, err))
            return false;
    }
    return true;
}

} // namespace idc::cli
