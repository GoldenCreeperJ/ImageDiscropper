// ============================================================================
// 文件：app/controllers/config_controller.cpp
// 作用：实现 ConfigController 的配置存取编排（见同名头文件说明）——保存/载入作业配置，
//       对话框父窗口取 dialogParent_、提示经 status_->notify 发出。
// ============================================================================
#include "app/controllers/config_controller.h"

#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QInputDialog>
#include <QStandardPaths>

#include <algorithm>
#include <filesystem>

#include "app/controllers/burn_in_reminder.h"
#include "app/controllers/doc_history.h"
#include "app/controllers/preprocess_controller.h"
#include "app/forms/config_io_dialog.h"   // 保存/加载配置对话框（路径 + 图层资产四选项）
#include "app/status_bar.h"
#include "model/annotation_bridge.h"
#include "model/config_assets.h"          // 资产查找链解析 / 解码注入 / 打包写盘（零 Qt，与 CLI 同规）
#include "model/document.h"
#include "model/engine_bridge.h"
#include "panels/export_panel.h"
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
void ConfigController::setExportPanel(ExportPanel* panel) { panel_ = panel; }

// 保存配置：把当前作业配置写为 SPEC §7 schema 的 JSON 文件（委托 EngineBridge → Core）。
void ConfigController::onSaveConfig() {
    QString path;
#ifdef Q_OS_ANDROID
    // 移动端免路径选择（SPEC §8.3 形态差异）：SAF 保存框返回 content:// 不可写，且系统选择器进不了应用目录。
    // 配置为应用自用数据，存固定目录；路径预设进对话框（路径行隐藏），资产四选项照常显式选择。
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation)
                        + u'/' + QString::fromUtf8(texts::ident::kAppDocsConfigSubDir);
    QDir().mkpath(dir);
    path = dir + u'/' + QString::fromUtf8(texts::ident::kConfigFileNamePrefix)
           + QDateTime::currentDateTime().toString(QString::fromUtf8(texts::ident::kTimestampFormat))
           + QStringLiteral(".json");
#endif

    // 资产四选项显式化：对话框采集「是否导出（打包图层资产）+ 导出到哪里（资产目录，空＝默认 <配置名>.assets/）」。
    const bool hasFileLayers = std::ranges::any_of(doc_->layersSnapshot(),
                                                  [](const canvas::ImageLayer& l) { return !l.fileName.empty() && !l.recipe.has_value(); });
    ConfigIoDialog dlg(ConfigIoDialog::Mode::Save, dialogParent_, hasFileLayers);
    dlg.setPath(path);
    if (dlg.exec() != QDialog::Accepted) return;
    path = dlg.path();
    if (path.isEmpty()) return;

    // 未烧录提醒（三键快捷框，可经「外观/交互→其他→确认与吸附」的「未烧录时导出前提醒」关闭，默认开）：有标注/图层但未勾对应烧录时提醒；
    //   「开烧录并导出」经面板信号链视同勾选后继续，取消则中止。下面据更新后的烧录态决定配置是否写入对应段。
    if (!confirmBurnInBeforeExport(dialogParent_, panel_, doc_, anno_)) return;

    // 保存携带完整声明式预处理队列（含 ResizeOp）：用 buildSerializableConfig（非运行配置的空 preprocess）。
    // 标注层与图层段的写入由烧录开关决定（用户口径「烧录影响导出配置」）：
    //   · 图层烧录未勾 → 清空 cfg.layers（不写 §7 layers 段）；仅作用于此处的「保存的配置」，
    //     DocHistory 快照虽同走 buildSerializableConfig，但不经本函数、不受影响（图层仍可撤销）。
    //   · 标注烧录未勾 → saveConfig 的 annotations 实参传 nullptr（不写 §7 annotations 段）。
    engine::EngineConfig cfg = doc_->buildSerializableConfig();
    if (!doc_->layersBurnIn()) cfg.layers.reset();
    // 资产目录为配置目录之内的自定义目录 → 写 assetsDir 字段（相对化，加载端按字段优先解析）；
    //   配置目录之外仍照常打包，但不写字段（字段契约＝配置内相对声明）。
    const QString assetDir = dlg.assetDir();
    if (dlg.assetActionEnabled() && !assetDir.isEmpty()) {
        const QFileInfo cfgInfo(path);
        const QString cfgAbs = cfgInfo.absolutePath();
        if (const QString dirAbs = QFileInfo(assetDir).absoluteFilePath(); dirAbs.startsWith(cfgAbs + u'/'))
            cfg.assetsDir = QDir(cfgAbs).relativeFilePath(dirAbs).toStdString();
    }
    if (QString err; !EngineBridge::saveConfig(path, cfg, err, anno_->burnInEnabled() ? &anno_->set() : nullptr, &percentHints_)) {
        status_->notify(IDC_GTR(status, kConfigSaveFailed).arg(err), true);
        return;
    }
    QString savedMsg = IDC_GTR(status, kConfigSaved).arg(path);
    // 「是否导出」落地：把 file 层像素按相对名写盘进目标目录（默认 <配置名>.assets/）。
    if (dlg.assetActionEnabled()) {
        engine::EngineConfig bundleCfg;
        bundleCfg.layers = doc_->layersSnapshot();
        std::filesystem::path targetDir;
        if (!assetDir.isEmpty())
            targetDir = std::filesystem::path(assetDir.toStdWString());
        else {
            std::filesystem::path assetsName = std::filesystem::path(path.toStdWString()).stem();
            assetsName += std::filesystem::path(L".assets");
            targetDir = std::filesystem::path(QFileInfo(path).absolutePath().toStdWString()) / assetsName;
        }
        if (std::string bErr; bundleFileLayers(bundleCfg, targetDir, bErr))
            savedMsg += IDC_GTR(status, kConfigBundledFmt).arg(QDir::toNativeSeparators(QString::fromStdWString(targetDir.wstring())));
        else
            status_->notify(IDC_GTR(status, kConfigBundleFailed).arg(QString::fromStdString(bErr)), true);
    }
    status_->notify(savedMsg, false);
}

// 加载配置：读 JSON → DocHistory::recordConfigLoad（抑制采集地应用 + 压栈，载入本身可一步撤销）。
void ConfigController::onLoadConfig() {
    QString path;
#ifdef Q_OS_ANDROID
    // 移动端列表选择（系统选择器进不了应用目录）：枚举 Config 目录，时间戳命名新在前；路径预设进对话框。
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation)
                        + u'/' + QString::fromUtf8(texts::ident::kAppDocsConfigSubDir);
    const QStringList files = QDir(dir).entryList({QString::fromUtf8(texts::ident::kJsonGlob)}, QDir::Files,
                                                  QDir::Name | QDir::Reversed);
    if (files.isEmpty()) { status_->notify(IDC_GTR(status, kConfigNoneSaved), true); return; }
    bool ok = false;
    const QString name = QInputDialog::getItem(dialogParent_, IDC_GTR(label, kDlgTitleLoadConfig),
                                               IDC_GTR(label, kDlgLabelChooseConfig), files, 0, false, &ok);
    if (!ok || name.isEmpty()) return;
    path = dir + u'/' + name;
#endif

    // 资产四选项显式化：对话框采集「是否导入（解析 file 层像素，不勾则全部降级占位）+ 从哪里导入（资产来源，空＝自动查找链）」。
    // 是否导入在解析配置前无法预知 file 层有无，故资产分组恒可用（无 file 层时勾选亦为空操作，无害）。
    ConfigIoDialog dlg(ConfigIoDialog::Mode::Load, dialogParent_, /*hasFileLayers=*/true);
    dlg.setPath(path);
    if (dlg.exec() != QDialog::Accepted) return;
    path = dlg.path();
    if (path.isEmpty()) return;

    engine::EngineConfig cfg;
    // 标注先收进临时集合：若直接灌真实层，随后的 applyLoadedQueue 尺寸门（维度变→清标注）会误清刚载入的标注。
    annotation::AnnotationSet loadedAnno;
    // 百分比几何字段先收进局部记录：载入成功才替换当前记录（失败时旧记录与当前文档配置仍相配）。
    core::PercentHints loadedHints;
    if (QString err; !EngineBridge::loadConfig(path, cfg, err, &loadedAnno, &loadedHints)) { status_->notify(IDC_GTR(status, kConfigLoadFailed).arg(err), true); return; }
    percentHints_ = std::move(loadedHints);
    // 「是否导入」落地：勾选 → 按查找链（资产来源 → config.assetsDir 字段 → <配置名>.assets/ → 配置所在目录）
    //   解码注入 decodedPixels（须在 recordConfigLoad 前完成：applyEngineConfig 内 emit layersChanged → 控制器
    //   立即 composeCanvas 用上解码像素）；不勾 → file 层保持空像素（Core 合成降级占位）。
    //   缺文件/解码失败 → 保持空像素、不阻断载入，但累计失败数、载入末尾给非阻断警示；file 名越界 → 拒绝载入。
    int missingLayerAssets = 0;
    if (dlg.assetActionEnabled()) {
        if (std::string aErr; !decodeFileLayers(cfg, path.toStdString(), dlg.assetDir().toStdString(), missingLayerAssets, aErr)) {
            status_->notify(IDC_GTR(status, kConfigLoadFailed).arg(QString::fromStdString(aErr)), true);
            return;
        }
    }
    // ---- 底图（input 生态位）重生策略（导入配置不覆盖已有底图与画布尺寸）：
    //   「当前已有打开图」(original 非空) → preserveBase：导入配置一律不动底图——像素(baseImage_)、
    //     声明(baseLayer_)、整体透明度(baseOpacity_)、色彩预处理会话(baseSession_) 及画布宽高(canvasWidth_/canvasHeight_) 全保留，配置仅还原
    //     切割/选择/排序/导出/图层/标注参数（recordConfigLoad 传 preserveBase=true 令 applyEngineConfig 跳过 base 段与画布尺寸）。
    //   「空文档」(无打开图) → 才据配置 generate 段现出底图：配方底→composeBase 按 source 现算图案；
    //     落位声明底（无 recipe、需外部位图）→ 空文档无当前图可注入，composeBase 依 backdrop 出白/透明底。----
    const bool preserveBase = !doc_->original().empty();
    bool baseBitmapMissing = false;   // 空文档重载时底图声明为「无 recipe 的 File/placement 底」但位图不可得（配置不含位图）→ composeBase 退白/透明底，需非阻断告知。
    if (!preserveBase) {
        // 底图像素重生仅在空文档进行；applyEngineConfig 只还原声明态、不碰 baseImage_，像素重生尽在于此。
        auto rebuildBase = [&](core::Image& flat) -> bool {
            if (!cfg.base.has_value()) return false;
            canvas::ImageLayer base = *cfg.base;
            base.preprocess.clear();   // 底图色彩预处理不烘焙 baseImage_：由 fadedCanvasBase 边界施加，杜绝双应用。
            flat = EngineBridge::composeBase(base, cfg.source.width, cfg.source.height);
            return !flat.empty();
        };
        if (cfg.layers.has_value()) {
            // 空文档 + 图层：仅配方底可现算并 setBaseImage；落位声明底留空底，交 composeCanvas 铺透明。
            if (core::Image genBase; rebuildBase(genBase)) doc_->setBaseImage(std::move(genBase));
        } else if (core::Image genBase; rebuildBase(genBase)) {
            // 空文档 + 无图层：程序/图案底经 setImage 建「含底图图层画布」（baseImage_=genBase、layersActive_ 真）。
            doc_->setImage(std::move(genBase), QString{});
        }
        // 底图声明需外部位图（有 base 且无 recipe）→ 空文档无从注入像素，实际已退白/透明底：标记以便末尾提示。
        baseBitmapMissing = cfg.base.has_value() && !cfg.base->recipe.has_value();
    }
    history_->recordConfigLoad(cfg, preserveBase);   // 还原切割/选择/排序/导出参数（preserveBase=true 时底图像素与声明均不动）。
    // 预处理队列单独从 original_ 重放物化（过尺寸门）：压栈快照走 buildEngineConfig（运行态，preprocess 恒空），
    // 不是传入的 cfg，故撤销参数不会覆盖队列。
    preprocess_->applyLoadedQueue(cfg.preprocess);
    // 标注还原在队列重放之后（SPEC §7 顺序纪律：先重放预处理、再还原标注；坐标基准＝预处理产出）。
    // 已知限制：还原不进 DocHistory 撤销栈（标注撤销由 AnnotationEditor 内建条目级会话态承担）。
    anno_->replaceAnnotations(loadedAnno.annotations());
    // 载入后按存在性单向自动开烧录（仅开不关）：配置带标注→开标注烧录，带叠加图片图层→开图层烧录；
    //   没有则保持现状不动（绝不主动关）。经 panel 的 enable* 走既有信号链（写回真相源 + 同步面板/菜单/预览）；
    //   panel 缺位时回退直设真相源（烧录为纯 GUI 运行态、不入配置）。
    if (anno_->count() > 0) {
        if (panel_) panel_->enableBurnIn();
        else anno_->setBurnIn(true);
    }
    if (!doc_->layerStack().empty()) {
        if (panel_) panel_->enableLayersBurnIn();
        else doc_->setLayersBurnIn(true);
    }
    // 载入成功回报：底图位图/图层资源有降级时，在成功句后逐段追加非阻断警示片段（任一则 isError=true 取 8s 警示色），载入绝不阻断。
    QString loadedMsg = IDC_GTR(status, kConfigLoaded).arg(path);
    bool degraded = false;
    if (baseBitmapMissing) { loadedMsg += IDC_GTR(status, kDegradedBase); degraded = true; }
    if (missingLayerAssets > 0) { loadedMsg += IDC_GTR(status, kDegradedLayersFmt).arg(missingLayerAssets); degraded = true; }
    status_->notify(loadedMsg, degraded);
}

} // namespace idc::gui
