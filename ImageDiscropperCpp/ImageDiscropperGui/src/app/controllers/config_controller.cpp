// ============================================================================
// 文件：app/config_controller.cpp
// 作用：实现 ConfigController 的配置存取编排（见同名头文件说明）；自
//       MainWindow::onSaveConfig/onLoadConfig 原封迁出，仅父窗口改 dialogParent_、
//       提示改 status_->notify。
// ============================================================================
#include "app/controllers/config_controller.h"

#include <QDateTime>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QInputDialog>
#include <QStandardPaths>

#include "app/controllers/doc_history.h"
#include "app/controllers/preprocess_controller.h"
#include "app/status_bar.h"
#include "model/annotation_bridge.h"
#include "model/document.h"
#include "model/engine_bridge.h"
#include "i18n/gui_texts.h"
#include "i18n/gui_i18n.h"

namespace idc::gui {

ConfigController::ConfigController(QObject* parent) : QObject(parent) {}

void ConfigController::setDocument(Document* doc) { doc_ = doc; }
void ConfigController::setAnnotationBridge(AnnotationBridge* anno) { anno_ = anno; }
void ConfigController::setPreprocessController(PreprocessController* preprocess) { preprocess_ = preprocess; }
void ConfigController::setHistory(DocHistory* history) { history_ = history; }
void ConfigController::setStatusBar(StatusBar* status) { status_ = status; }
void ConfigController::setDialogParent(QWidget* parent) { dialogParent_ = parent; }

// 保存配置：把当前作业配置写为 SPEC §7 schema 的 JSON 文件（委托 EngineBridge → Core）。
void ConfigController::onSaveConfig() {
#ifdef Q_OS_ANDROID
    // 移动端免对话框：SAF 保存框返回 content:// 不可写，且系统选择器进不了应用目录。
    // 配置为应用自用数据，存固定目录（onLoadConfig 列表选择读取，SPEC §8.3 形态差异）。
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation)
                        + u'/' + QString::fromUtf8(texts::ident::kAppDocsConfigSubDir);
    QDir().mkpath(dir);
    const QString path = dir + u'/' + QString::fromUtf8(texts::ident::kConfigFileNamePrefix)
                         + QDateTime::currentDateTime().toString(QString::fromUtf8(texts::ident::kTimestampFormat))
                         + QStringLiteral(".json");
#else
    const QString path = QFileDialog::getSaveFileName(dialogParent_, IDC_GTR(label, kDlgTitleSaveConfig), QString(),
        IDC_GTR(label, kFilterConfigJson));
    if (path.isEmpty()) return;
#endif
    // 保存携带完整声明式预处理队列（含 ResizeOp）：用 buildSerializableConfig（非运行配置的空 preprocess）。
    // 标注层同时序列化进 SPEC §7 annotations 段（配置为真相源，往返不丢）。
    if (QString err; EngineBridge::saveConfig(path, doc_->buildSerializableConfig(), err, &anno_->set()))
        status_->notify(IDC_GTR(status, kConfigSaved).arg(path), false);
    else
        status_->notify(IDC_GTR(status, kConfigSaveFailed).arg(err), true);
}

// 加载配置：读 JSON → DocHistory::recordConfigLoad（抑制采集地应用 + 压栈，载入本身可一步撤销）。
void ConfigController::onLoadConfig() {
#ifdef Q_OS_ANDROID
    // 移动端列表选择（系统选择器进不了应用目录）：枚举 Config 目录，时间戳命名新在前。
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation)
                        + u'/' + QString::fromUtf8(texts::ident::kAppDocsConfigSubDir);
    const QStringList files = QDir(dir).entryList({QString::fromUtf8(texts::ident::kJsonGlob)}, QDir::Files,
                                                  QDir::Name | QDir::Reversed);
    if (files.isEmpty()) { status_->notify(IDC_GTR(status, kConfigNoneSaved), true); return; }
    bool ok = false;
    const QString name = QInputDialog::getItem(dialogParent_, IDC_GTR(label, kDlgTitleLoadConfig),
                                               IDC_GTR(label, kDlgLabelChooseConfig), files, 0, false, &ok);
    if (!ok || name.isEmpty()) return;
    const QString path = dir + u'/' + name;
#else
    const QString path = QFileDialog::getOpenFileName(dialogParent_, IDC_GTR(label, kDlgTitleLoadConfig), QString(),
        IDC_GTR(label, kFilterConfigJson));
    if (path.isEmpty()) return;
#endif
    engine::EngineConfig cfg;
    // 标注先收进临时集合：若直接灌真实层，随后的 applyLoadedQueue 尺寸门（维度变→清标注）会误清刚载入的标注。
    annotation::AnnotationSet loadedAnno;
    if (QString err; !EngineBridge::loadConfig(path, cfg, err, &loadedAnno)) { status_->notify(IDC_GTR(status, kConfigLoadFailed).arg(err), true); return; }
    // 文件层解码注入（Phase 2/4）：配置的 layers 只存相对文件名、位图不入配置，
    // 按 <配置名>.assets/<相对名> 现解码填充 decodedPixels，须在 recordConfigLoad 前完成
    //（applyEngineConfig 内 emit layersChanged → 控制器立即 composeCanvas 用上解码像素）。
    // 越界防护：仅接受 assets 目录内的直接相对名；缺文件/越界 → 保持空像素（Core 合成降级占位），不阻断载入。
    if (cfg.layers.has_value()) {
        const QFileInfo cfgInfo(path);
        const QString assetsDir = cfgInfo.absolutePath() + u'/' + cfgInfo.completeBaseName() + QString::fromUtf8(texts::ident::kAssetsDirSuffix);
        for (auto& layer : *cfg.layers) {
            if (layer.sourceKind != canvas::LayerSourceKind::File || layer.fileName.empty()) continue;
            const QString rel = QString::fromStdString(layer.fileName);
            if (rel.contains(QStringLiteral("..")) || rel.contains(u'/') || rel.contains(QDir::separator())) continue;
            core::Image px;
            if (QString err; EngineBridge::loadImage(assetsDir + u'/' + rel, px, err))
                layer.decodedPixels = std::move(px);
        }
    }
    // ---- 底图（input 生态位）像素重生：applyEngineConfig 只还原 baseLayer_＝cfg.base、不碰 baseImage_，像素重生尽在于此。
    //      Recipe 底：composeBase 按 source 尺寸现生成图案（逐像素复现，与旧 generate 段等价）；
    //      File 底：按 cfg.base.file 相对名从 <config>.assets 解码注入 decodedPixels 后 composeBase 展平（缺文件降级）；
    //      cfg.base 缺省：外部打开图＝恒等底图（layers 态复用当前原图、单图态走 original_），外部用法不变。----
    auto rebuildBase = [&](core::Image& flat) -> bool {
        if (!cfg.base.has_value()) return false;
        canvas::ImageLayer base = *cfg.base;
        if (base.sourceKind == canvas::LayerSourceKind::File && !base.fileName.empty()) {
            const QFileInfo cfgInfo(path);
            const QString assetsDir = cfgInfo.absolutePath() + u'/' + cfgInfo.completeBaseName() + QString::fromUtf8(texts::ident::kAssetsDirSuffix);
            if (const QString rel = QString::fromStdString(base.fileName); !rel.contains(QStringLiteral("..")) && !rel.contains(u'/') && !rel.contains(QDir::separator())) {
                core::Image px;
                if (QString err; EngineBridge::loadImage(assetsDir + u'/' + rel, px, err))
                    base.decodedPixels = std::move(px);   // 缺文件/越界→保持空像素，composeBase 降级为底色。
            }
        }
        base.preprocess.clear();   // 底图色彩预处理不烘焙 baseImage_：改由 fadedCanvasBase 边界施加（applyEngineConfig 已提入 baseSession_），杜绝双应用。
        flat = EngineBridge::composeBase(base, cfg.source.width, cfg.source.height);
        return !flat.empty();
    };
    if (cfg.layers.has_value()) {
        if (core::Image genBase; rebuildBase(genBase)) doc_->setBaseImage(std::move(genBase));
        else if (!doc_->original().empty()) doc_->setBaseImage(doc_->original());   // 无底图声明：复用当前已打开原图作基座（GUI 无 --input 通道）。
    } else if (core::Image genBase; rebuildBase(genBase)) {
        // 无图层、程序/附图底图：作为单一载入图回归 original_（layersActive_ 假，canvasBase() 走 original_）。
        doc_->setImage(std::move(genBase), QString{});
    }
    history_->recordConfigLoad(cfg);   // 还原切割/选择/排序/导出参数（预处理队列为空 preprocess，不在此步处理）。
    // 预处理队列单独从 original_ 重放物化（过尺寸门）：压栈快照走 buildEngineConfig（运行态，preprocess 恒空），
    // 不是传入的 cfg，故撤销参数不会覆盖队列。
    preprocess_->applyLoadedQueue(cfg.preprocess);
    // 标注还原在队列重放之后（SPEC §7 顺序纪律：先重放预处理、再还原标注；坐标基准＝预处理产出）。
    // 已知限制：还原不进 DocHistory 撤销栈（标注撤销由 AnnotationEditor 内建条目级会话态承担）。
    anno_->replaceAnnotations(loadedAnno.annotations());
    status_->notify(IDC_GTR(status, kConfigLoaded).arg(path), false);
}

} // namespace idc::gui
