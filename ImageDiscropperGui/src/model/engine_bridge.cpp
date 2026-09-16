// ============================================================================
// 文件：model/engine_bridge.cpp
// 作用：实现 EngineBridge——把每个 GUI 需求映射到对应的 Core API 调用（见头文件）。
// 分块依据：每个方法只做「参数转 std::string / 调 Core / 结果或错误回传」，
//           不含任何切割、几何、排序、编解码实现（那些全在 Core；A-0.1/A-0.3）。
// ============================================================================
#include "model/engine_bridge.h"

#include "engine/image_io.h" // readImageFile（loadImage 委托 Core 解码）。

namespace idc::gui {

// 从文件解码图像（委托 Core readImageFile）。
bool EngineBridge::loadImage(const QString& path, idc::core::Image& out, QString& err) const {
    if (path.isEmpty()) {
        err = QStringLiteral("路径为空，无法打开图像");
        return false;
    }
    // Core 统一把常见格式解码为 RGBA；失败（不存在/不可读/格式不支持）返回 false。
    if (!idc::engine::readImageFile(path.toStdString(), out)) {
        err = QStringLiteral("无法读取图像：%1").arg(path);
        return false;
    }
    return true;
}

// 跑一次预览计算（委托 Core runEngine）。
idc::engine::EngineResult EngineBridge::runPreview(const idc::core::Image& work,
                                                   const idc::engine::EngineConfig& cfg) const {
    // runEngine 内部：预处理(空) → 切割线 → 网格 → 选择集 → 极性 → split → 合成，
    // 只做区域数学，不搬像素，故可对全分辨率工作图实时调用。
    return idc::engine::runEngine(work, cfg);
}

// 由切割配置生成贯穿全图的切割线集合（委托 Core generateCutLines）。
idc::engine::CutLineSet EngineBridge::cutLines(const idc::engine::CutConfig& cut,
                                               const idc::engine::SourceInfo& src) const {
    return idc::engine::generateCutLines(cut, src);
}

// 导出（委托 Core runEngine + exportImage，对全分辨率工作图操作）。
bool EngineBridge::exportResult(const idc::core::Image& work, const idc::engine::EngineConfig& cfg,
                                const QString& outputPath, QString& err) const {
    if (outputPath.isEmpty()) {
        err = QStringLiteral("未指定输出路径");
        return false;
    }
    // 先算（全分辨率），失败则把 Core 的中文原因透传给上层展示。
    const idc::engine::EngineResult res = idc::engine::runEngine(work, cfg);
    if (!res.ok) {
        err = QString::fromStdString(res.error);
        return false;
    }
    // 再落盘：分离模式 outputPath 为目录，合并模式为单图文件路径（由 Core 内部区分）。
    if (!idc::engine::exportImage(res.composition, work, outputPath.toStdString())) {
        err = QStringLiteral("导出失败：无法写入 %1").arg(outputPath);
        return false;
    }
    return true;
}

} // namespace idc::gui
