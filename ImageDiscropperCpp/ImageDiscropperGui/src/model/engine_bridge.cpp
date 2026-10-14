// ============================================================================
// 文件：model/engine_bridge.cpp
// 作用：实现 EngineBridge——把每个 GUI 需求映射到对应的 Core API 调用（见头文件）。
// 分块依据：每个方法只做「参数转 std::string / 调 Core / 结果或错误回传」，
//           不含任何切割、几何、排序、编解码实现（那些全在 Core；CONTRIBUTING.md「分层纪律」）。
// ============================================================================
#include "model/engine_bridge.h"

#include <QByteArray>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QStandardPaths>

#include "engine/image_io.h" // readImageFile（loadImage 委托 Core 解码）。
#include "annotation/annotation_set.h" // AnnotationSet（save/load 可选标注集合透传，配置为真相源）。
#include "engine/engine_config_json.h" // saveEngineConfig/loadEngineConfig（配置文件存取）。
#include "generate/pattern.h" // makePattern（generatePattern 委托 Core 程序化生成）。
#include "canvas/layer.h"     // composeCanvas（多层画布展平，委托 Core canvas 层）。

#include "i18n/gui_texts.h"
#include "i18n/gui_i18n.h"

namespace idc::gui {

namespace {
// 把内容 URI 的字节读入内存（QFile 在 Android 能开 content://）；失败返回空 QByteArray。
QByteArray readContentBytes(const QString& uri) {
    QFile src(uri);
    if (!src.open(QIODevice::ReadOnly)) return {};
    const QByteArray data = src.readAll();
    src.close();
    return data;
}

// 把字节写入 dir 下唯一命名的文件（自动建目录）；成功返回真实路径，失败返回空串。
QString writeTempFile(const QString& dir, const QString& prefix, const QByteArray& data) {
    QDir().mkpath(dir);
    const QString path = dir + u'/' + prefix + QString::number(QDateTime::currentMSecsSinceEpoch());
    QFile out(path);
    if (!out.open(QIODevice::WriteOnly) || out.write(data) != data.size()) return {};
    out.close();
    return path;
}
} // namespace

// content:// 物化：SAF 内容 URI 拷到缓存目录真实临时文件；非 content:// 原样返回；失败返回空串。
QString EngineBridge::materializeForRead(const QString& path) {
    if (!path.startsWith(QString::fromUtf8(texts::ident::kContentUriScheme))) return path;
    const QByteArray data = readContentBytes(path);
    if (data.isEmpty()) return {};
    return writeTempFile(QStandardPaths::writableLocation(QStandardPaths::CacheLocation),
                         QStringLiteral("read-image-"), data);
}

// 源图持久化：content:// 拷到 AppDataLocation 子目录的稳定文件；非 content:// 原样返回；失败返回空串。
QString EngineBridge::persistForProvenance(const QString& path) {
    if (!path.startsWith(QString::fromUtf8(texts::ident::kContentUriScheme))) return path;
    const QByteArray data = readContentBytes(path);
    if (data.isEmpty()) return {};
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
                        + u'/' + QString::fromUtf8(texts::ident::kPickedImageSubDir);
    return writeTempFile(dir, QStringLiteral("source-"), data);
}

// 从文件解码图像（委托 Core readImageFile）。
bool EngineBridge::loadImage(const QString& path, core::Image& out, QString& err) {
    if (path.isEmpty()) {
        err = IDC_GTR(msg, kBridgeEmptyImagePath);
        return false;
    }
    // Android SAF：content:// 先物化为可解码的真实路径（Core readImageFile 用 std::ifstream 开不了内容 URI）。
    const QString resolved = materializeForRead(path);
    if (resolved.isEmpty()) {
        err = IDC_GTR(msg, kBridgeReadImageFailed).arg(path);   // 透传原始路径供上层展示（不泄露临时名）。
        return false;
    }
    // Core 统一把常见格式解码为 RGBA；失败（不存在/不可读/格式不支持）返回 false。
    if (!engine::readImageFile(resolved.toStdString(), out)) {
        err = IDC_GTR(msg, kBridgeReadImageFailed).arg(path);
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
        err = IDC_GTR(msg, kBridgeNoOutputPath);
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
        err = IDC_GTR(msg, kBridgeWriteOutputFailed).arg(outputPath);
        return false;
    }
    return true;
}

// ---- 配置文件存取：逐个委托 Core saveEngineConfig/loadEngineConfig，不自实现 JSON（CONTRIBUTING.md「分层纪律」）----

// 保存配置：委托 Core saveEngineConfig（SPEC §7 schema、缩进美化）；path 空或写盘失败透传中文错误。
bool EngineBridge::saveConfig(const QString& path, const engine::EngineConfig& cfg, QString& err,
                              const annotation::AnnotationSet* annotations) {
    if (path.isEmpty()) {
        err = IDC_GTR(msg, kBridgeNoConfigPath);
        return false;
    }
    if (!idc::engine::saveEngineConfig(path.toStdString(), cfg, annotations)) {
        err = IDC_GTR(msg, kBridgeWriteConfigFailed).arg(path);
        return false;
    }
    return true;
}

// 加载配置：委托 Core loadEngineConfig；path 空、读盘或解析失败透传中文错误，成功填充 out。
bool EngineBridge::loadConfig(const QString& path, engine::EngineConfig& out, QString& err,
                              annotation::AnnotationSet* annotations) {
    if (path.isEmpty()) {
        err = IDC_GTR(msg, kBridgeNoConfigPath);
        return false;
    }
    if (!idc::engine::loadEngineConfig(path.toStdString(), out, annotations)) {
        err = IDC_GTR(msg, kBridgeReadConfigFailed).arg(path);
        return false;
    }
    return true;
}

// 声明式队列物化：委托 Core PreprocessPipeline::applyExec（执行层融合：色彩折单次 channelMap、几何折单次 D4，
// 与逐步 apply 逐像素等价；ResizeOp/旋转分派全在 Core）。
core::Image EngineBridge::applyPipeline(const core::Image& src,
                                       const preprocess::PreprocessPipeline& pipeline) {
    return pipeline.applyExec(src);
}

// 程序化生成新图（委托 Core generate::makePattern）：新建对话框采参后取得初始画布；像素生成全在 Core。
core::Image EngineBridge::generatePattern(const int width, const int height, const generate::PatternParams& params) {
    return generate::makePattern(width, height, params);
}

// 多层画布展平：委托 Core canvas::composeCanvas（先落底图 base、再逐层叠加→缩放→source-over 落位，越界裁剪全在 Core）。
// materialized 透传：命中预物化位的图层直接取用、跳过 Core materializeLayer（改 transform 不重跑预处理）。
core::Image EngineBridge::composeCanvas(const std::vector<canvas::ImageLayer>& layers, const int width, const int height, const core::Image& base,
                                       const std::unordered_map<int, core::Image>* materialized) {
    return canvas::composeCanvas(layers, width, height, base, materialized);
}

// 单层物化：委托 Core canvas::materializeLayer（配方现算 / 文件解码像素 / 空→空图，再跑该层 preprocess）。
core::Image EngineBridge::materializeLayer(const canvas::ImageLayer& layer) {
    return canvas::materializeLayer(layer);
}

// 底图展平：委托 Core canvas::composeBase（纯图案底按 source 尺寸现算；文件底转调 composeCanvas 两段合成）。
core::Image EngineBridge::composeBase(const canvas::ImageLayer& base, const int width, const int height) {
    return canvas::composeBase(base, width, height);
}

} // namespace idc::gui
