// ============================================================================
// 文件：model/engine_bridge.cpp
// 作用：实现 EngineBridge——把每个 GUI 需求映射到对应的 Core API 调用（见头文件）。
// 分块依据：每个方法只做「参数转 std::string / 调 Core / 结果或错误回传」，
//           不含任何切割、几何、排序、编解码实现（那些全在 Core；CONTRIBUTING.md「分层纪律」）。
// ============================================================================
#include "model/engine_bridge.h"

#include "engine/image_io.h" // readImageFile（loadImage 委托 Core 解码）。
#include "annotation/annotation_layer.h" // AnnotationLayer（save/load 可选标注层透传，配置为真相源）。
#include "engine/engine_config_json.h" // saveEngineConfig/loadEngineConfig（配置文件存取）。

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

// 仅尺寸预览（委托 Core runEngineGeometry）：以已预处理输出尺寸直接跑几何流水线，不碰像素、不应用 cfg.preprocess。
engine::EngineResult EngineBridge::runPreviewGeometry(const engine::SourceInfo& dims,
                                                      const engine::EngineConfig& cfg) {
    return idc::engine::runEngineGeometry(dims, cfg);
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
bool EngineBridge::saveConfig(const QString& path, const engine::EngineConfig& cfg, QString& err,
                              const annotation::AnnotationLayer* annotations) {
    if (path.isEmpty()) {
        err = QStringLiteral("未指定配置文件路径");
        return false;
    }
    if (!idc::engine::saveEngineConfig(path.toStdString(), cfg, annotations)) {
        err = QStringLiteral("无法写入配置文件：%1").arg(path);
        return false;
    }
    return true;
}

// 加载配置：委托 Core loadEngineConfig；path 空、读盘或解析失败透传中文错误，成功填充 out。
bool EngineBridge::loadConfig(const QString& path, engine::EngineConfig& out, QString& err,
                              annotation::AnnotationLayer* annotations) {
    if (path.isEmpty()) {
        err = QStringLiteral("未指定配置文件路径");
        return false;
    }
    if (!idc::engine::loadEngineConfig(path.toStdString(), out, annotations)) {
        err = QStringLiteral("无法读取或解析配置文件：%1").arg(path);
        return false;
    }
    return true;
}

// 声明式队列物化：委托 Core PreprocessPipeline::apply（逐步分派含 ResizeOp/旋转全在 Core）。
core::Image EngineBridge::applyPipeline(const core::Image& src,
                                       const preprocess::PreprocessPipeline& pipeline) {
    return pipeline.apply(src);
}

} // namespace idc::gui
