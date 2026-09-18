// ============================================================================
// 文件：model/engine_bridge.cpp
// 作用：实现 EngineBridge——把每个 GUI 需求映射到对应的 Core API 调用（见头文件）。
// 分块依据：每个方法只做「参数转 std::string / 调 Core / 结果或错误回传」，
//           不含任何切割、几何、排序、编解码实现（那些全在 Core；CONTRIBUTING.md「分层纪律」）。
// ============================================================================
#include "model/engine_bridge.h"

#include "engine/image_io.h" // readImageFile（loadImage 委托 Core 解码）。
#include "engine/engine_config_json.h" // saveEngineConfig/loadEngineConfig（配置文件存取）。
#include "pixel_ops/color_ops.h"     // toGray/invertChannels/splitChannels（预处理颜色变换）。
#include "pixel_ops/geometric_ops.h" // rotate/flip/resize/scale（预处理几何变换）。

namespace idc::gui {

// 从文件解码图像（委托 Core readImageFile）。
bool EngineBridge::loadImage(const QString& path, core::Image& out, QString& err) {
    if (path.isEmpty()) {
        err = QStringLiteral("路径为空，无法打开图像");
        return false;
    }
    // Core 统一把常见格式解码为 RGBA；失败（不存在/不可读/格式不支持）返回 false。
    if (!engine::readImageFile(path.toStdString(), out)) {
        err = QStringLiteral("无法读取图像：%1").arg(path);
        return false;
    }
    return true;
}

// 跑一次预览计算（委托 Core runEngine）。
engine::EngineResult EngineBridge::runPreview(const core::Image& work,
                                                   const engine::EngineConfig& cfg) {
    // runEngine 内部：预处理(空) → 切割线 → 网格 → 选择集 → 极性 → split → 合成，
    // 只做区域数学，不搬像素，故可对全分辨率工作图实时调用。
    return idc::engine::runEngine(work, cfg);
}

// 由切割配置生成贯穿全图的切割线集合（委托 Core generateCutLines）。
engine::CutLineSet EngineBridge::cutLines(const engine::CutConfig& cut,
                                               const engine::SourceInfo& src) {
    return idc::engine::generateCutLines(cut, src);
}

// 依切割配置产出诱导网格（镜像 runEngine 的网格产出：GRID 走 Grid::build、其余走线诱导）。
engine::Grid EngineBridge::buildGrid(const engine::CutConfig& cut,
                                          const engine::SourceInfo& src) {
    engine::Grid grid;
    if (cut.generator == engine::CutGenerator::GRID) {
        grid.build(cut.grid, src.width, src.height);  // 参数化网格，含余量策略。
    } else {
        grid = idc::engine::induceGrid(idc::engine::generateCutLines(cut, src), src);
    }
    return grid;
}

// 导出（委托 Core runEngine + exportImage，对全分辨率工作图操作）。
bool EngineBridge::exportResult(const core::Image& work, const engine::EngineConfig& cfg,
                                const QString& outputPath, QString& err) {
    if (outputPath.isEmpty()) {
        err = QStringLiteral("未指定输出路径");
        return false;
    }
    // 先算（全分辨率），失败则把 Core 的中文原因透传给上层展示。
    const engine::EngineResult res = idc::engine::runEngine(work, cfg);
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

// ---- 配置文件存取：逐个委托 Core saveEngineConfig/loadEngineConfig，不自实现 JSON（CONTRIBUTING.md「分层纪律」）----

// 保存配置：委托 Core saveEngineConfig（SPEC §7 schema、缩进美化）；path 空或写盘失败透传中文错误。
bool EngineBridge::saveConfig(const QString& path, const engine::EngineConfig& cfg, QString& err) {
    if (path.isEmpty()) {
        err = QStringLiteral("未指定配置文件路径");
        return false;
    }
    if (!idc::engine::saveEngineConfig(path.toStdString(), cfg)) {
        err = QStringLiteral("无法写入配置文件：%1").arg(path);
        return false;
    }
    return true;
}

// 加载配置：委托 Core loadEngineConfig；path 空、读盘或解析失败透传中文错误，成功填充 out。
bool EngineBridge::loadConfig(const QString& path, engine::EngineConfig& out, QString& err) {
    if (path.isEmpty()) {
        err = QStringLiteral("未指定配置文件路径");
        return false;
    }
    if (!idc::engine::loadEngineConfig(path.toStdString(), out)) {
        err = QStringLiteral("无法读取或解析配置文件：%1").arg(path);
        return false;
    }
    return true;
}

// ---- 预处理转发（FR-1）：逐个委托 Core pixel_ops::*，不做任何像素运算（CONTRIBUTING.md「分层纪律」）----

// 旋转：委托 pixel_ops::rotate（仅 90 的整数倍，其余角度返回未旋转的副本）。
core::Image EngineBridge::rotateImage(const core::Image& src, const int angleDeg) {
    return pixel_ops::rotate(src, angleDeg);
}

// 翻转：委托 pixel_ops::flip（horizontal=true 左右、false 上下）。
core::Image EngineBridge::flipImage(const core::Image& src, const bool horizontal) {
    return pixel_ops::flip(src, horizontal);
}

// 目标尺寸缩放：委托 pixel_ops::resize（双线性插值）；非正尺寸时原图返回（不产生空图）。
core::Image EngineBridge::resizeImage(const core::Image& src, const int newW, const int newH) {
    if (newW <= 0 || newH <= 0) return src;
    return pixel_ops::resize(src, newW, newH);
}

// 按比例缩放：委托 pixel_ops::scale（factor 必须 > 0，否则原图返回）。
core::Image EngineBridge::scaleImage(const core::Image& src, const double factor) {
    if (factor <= 0.0) return src;
    return pixel_ops::scale(src, factor);
}

// 黑白（灰度）：委托 pixel_ops::toGray。
core::Image EngineBridge::toGrayImage(const core::Image& src) {
    return pixel_ops::toGray(src);
}

// 按通道反色：委托 pixel_ops::invertChannels（mask 长度 3，'1' 反色对应 R/G/B）。
core::Image EngineBridge::invertImage(const core::Image& src, const std::string& mask) {
    return pixel_ops::invertChannels(src, mask);
}

// 按通道分离：委托 pixel_ops::splitChannels（mask 长度 3，'1' 保留对应 R/G/B，'0' 置零）。
core::Image EngineBridge::splitImage(const core::Image& src, const std::string& mask) {
    return pixel_ops::splitChannels(src, mask);
}

} // namespace idc::gui
