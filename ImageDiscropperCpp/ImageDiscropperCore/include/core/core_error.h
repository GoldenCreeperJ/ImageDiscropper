// ============================================================================
// 文件：include/core/core_error.h
// 作用：Core 统一错误对象 CoreError——std::expected<T, CoreError> 的错误侧载荷，
//       把失败面显式化。
// 分块依据：错误分两层——
//           · code（枚举）：程序化分派用，对齐 Rust GuideLine §3.3 的 E-1~E-8 变体，
//             供 CLI 映射退出码、供跨语言/上层按类别处理，不随语言变化。
//           · message（std::string）：在产生点即经 i18n 本地化的中文可读提示，供 UI/CLI 直接
//             展示；I/O 类失败可由上层自产文案，此时 message 允许为空。
// 说明：置于 idc::core（地基命名空间），engine / annotation 等上层错误统一携带它，避免
//       与 engine 配置头的循环依赖。header-only、无第三方类型泄漏（对齐「依赖 PRIVATE」纪律）。
// ============================================================================
#pragma once

#include <string>

namespace idc::core {

// Core 统一错误对象：Code 供程序化分派（退出码 / 跨语言映射），message 携已本地化中文报因。
struct CoreError {
    // 错误类别——对齐 SPEC §5 E-1~E-8 与 Rust GuideLine §3.3 CoreError 变体。
    enum class Code {
        // —— 引擎业务错误（切割流水线，SPEC §5 E-1~E-7）——
        SourceEmpty,                // 源图空 / 输入尺寸非正
        SourceEmptyAfterPreprocess, // 预处理后工作图为空
        DegenerateSelection,        // E-1：选框/矩形退化（宽高均非正）
        MultiRectEmpty,             // 多矩形并集但矩形列表为空
        NoGrid,                     // 未诱导任何网格单元
        EmptyDeleteRegion,          // E-6：删除区为空（REMOVE 却无单元被剔除）
        EmptyKeepRegion,            // E-7：删除覆盖全图（保留区为空）
        RearrangeNeedsL3,           // 仅网格模式可重排（非网格显式请求重排）
        NotCollapsible,             // 请求坍缩但不可坍缩（标准/擦除模式无重排可用）
        // —— 图像 I/O 与配置（SPEC §5 E-8）——
        PathEmpty,          // 路径为空
        Io,                 // 读写盘 / 目录创建失败
        DecodeFailed,       // 图像解码失败（不存在 / 不可读 / 格式不支持）
        EncodeFailed,       // 图像编码失败
        ConfigParseFailed,  // 配置 JSON 读盘 / 解析失败
        AnnotationInvalid,  // 标注段校验失败（message 携逐条报因）
    };

    Code code{Code::Io};
    std::string message; // 本地化中文提示（I/O 类可由上层自产文案，此处允许为空）。

    // 便捷构造：错误类别 + 已本地化的报因文本。
    CoreError(const Code c, std::string msg) : code(c), message(std::move(msg)) {}
};

} // namespace idc::core
