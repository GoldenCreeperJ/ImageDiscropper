// ============================================================================
// 文件：app/controllers/export_controller.cpp
// 作用：实现 ExportController 的导出编排（见同名头文件说明）——对应 onExport 职责：
//       对话框父窗口取 dialogParent_、提示经 status_->notify 发出、移动端发布层调
//       mobile/android_export_publish。
// ============================================================================
#include "app/controllers/export_controller.h"

#include <QCheckBox>
#include <QDateTime>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QMessageBox>
#include <QStandardPaths>

#include "app/controllers/burn_in_reminder.h"
#include "app/controllers/preprocess_controller.h"
#include "app/app_prefs.h"
#include "app/status_bar.h"
#include "model/annotation_bridge.h"
#include "model/document.h"
#include "model/engine_bridge.h"
#include "mobile/android_export_publish.h"
#include "panels/export_panel.h"
#include "i18n/gui_texts.h"
#include "i18n/gui_i18n.h"

namespace idc::gui {

ExportController::ExportController(QObject* parent) : QObject(parent) {}

void ExportController::setDocument(Document* doc) { doc_ = doc; }
void ExportController::setAnnotationBridge(AnnotationBridge* anno) { anno_ = anno; }
void ExportController::setPreprocessController(PreprocessController* preprocess) { preprocess_ = preprocess; }
void ExportController::setExportPanel(ExportPanel* panel) { panel_ = panel; }
void ExportController::setStatusBar(StatusBar* status) { status_ = status; }
void ExportController::setDialogParent(QWidget* parent) { dialogParent_ = parent; }

void ExportController::connectSignals() {
    connect(panel_, &ExportPanel::exportRequested, this, &ExportController::onExport);
}

// 导出（委托 EngineBridge → Core runEngine + exportImage）。
void ExportController::onExport() {
    if (!doc_->hasImage()) { status_->notify(IDC_GTR(status, kExportNoImage), true); return; }
    // 选区形态随模式不同（SPEC §8.3 行为同构、零形态分支的唯一必要分派）：
    // L1/L2 = 矩形选区（含 L2 多矩形）；L3 = 显式单元选择集（rect 恒空）。
    const bool hasSelection = doc_->mode() == engine::Tier::Grid
        ? !doc_->selectedCells().empty()
        : doc_->hasRect();
    if (!hasSelection) { status_->notify(IDC_GTR(status, kExportNoSelection), true); return; }

    const engine::EngineConfig cfg = doc_->buildEngineConfig();

    // 合并重排参数警告（cols*rows < 保留块数，或 cw/ch < 网格单元尺寸）：导出前弹窗二次确认。
    // 内联红字已在导出面板实时显示，此处按需求再加一道模态确认；可由「外观/交互→其他→确认与吸附」的「重排前二次确认」关掉。
    if (UiPrefs::instance().confirmRearrangeRisk()) {
        if (const QString rearrangeWarn = panel_->rearrangeWarning(); !rearrangeWarn.isEmpty()) {
            // 实例式弹框以容纳内嵌复选框（同「重置全部」/未烧录提醒做法）：Yes/No、默认 No。
            QMessageBox box(dialogParent_);
            box.setIcon(QMessageBox::Warning);
            box.setWindowTitle(IDC_GTR(msg, kRearrangeWarnTitle));
            box.setText(rearrangeWarn + IDC_GTR(msg, kRearrangeAskContinue));
            box.setStandardButtons(QMessageBox::Yes | QMessageBox::No);
            box.setDefaultButton(QMessageBox::No);
            // 「下次不再提醒」复选框（默认不勾）：无论点 Yes 还是 No，勾选即在关闭后关掉
            //   UiPrefs::confirmRearrangeRisk 偏好（落盘），下次导出不再弹。
            QCheckBox dontRemind(IDC_GTR(msg, kRearrangeDontRemind), &box);
            box.setCheckBox(&dontRemind);
            const int ret = box.exec();
            if (dontRemind.isChecked()) UiPrefs::instance().setConfirmRearrangeRisk(false);
            if (ret != QMessageBox::Yes) return;
        }
    }

    // 未烧录提醒（可由「外观/交互→其他→确认与吸附」的「未烧录时导出前提醒」关掉，默认开）：有标注/图层但未勾对应烧录时弹三键快捷框；
    //   「开烧录并导出」经面板信号链视同用户勾选对应烧录（同步写回真相源/菜单/预览）后继续，「按现状导出」不改开关继续，取消则中止。
    //   下面的导出烧录据此读取更新后的 burnInEnabled()/layersBurnIn() 决定是否并入像素。
    if (!confirmBurnInBeforeExport(dialogParent_, panel_, doc_, anno_)) return;

    QString path;
#ifdef Q_OS_ANDROID
    // Android 发布编排（SPEC §8.3 形态差异）：Core 只写真实路径，故先导出到缓存临时区，
    // 再经发布层（mobile/android_export_publish.h）流式写入最终目的地；桌面分支不变。
    // 临时工作目录以原图基名命名：Core 的 {name} 占位取自输出目录的文件夹名
    //（export.cpp），否则产物名会变成 export-<msecs>_000.png。
    const auto workName = [&]() {
        const QString base = QFileInfo(doc_->imagePath()).completeBaseName();
        return !base.isEmpty() && !doc_->imagePath().startsWith(QString::fromUtf8(texts::ident::kContentUriScheme))
                   ? base : QStringLiteral("image");
    };
    const QString ts = QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-hhmmss"));
    const QString tempBase = QStandardPaths::writableLocation(QStandardPaths::CacheLocation)
                             + QStringLiteral("/export-%1").arg(QDateTime::currentMSecsSinceEpoch());
    QString galleryRel;     // 相册相对路径（Pictures/ 前缀由发布层补）。
    QString galleryDisplay; // 单文件在相册中的显示名（自动名或面板自定义名）。
    QString tempRoot;       // 临时根（空=直写路径，无需发布层）。
#endif
    if (cfg.exportParams.mode == engine::ExportMode::Separate) {
        path = doc_->outputDir();
#ifdef Q_OS_ANDROID
        // 手输的 content:// 在移动端无意义：视同留空走相册默认子目录。
        if (!path.isEmpty() && path.startsWith(QString::fromUtf8(texts::ident::kContentUriScheme))) path.clear();
#endif
        if (path.isEmpty()) {
#ifdef Q_OS_ANDROID
            // 默认：相册 Pictures/ImageDiscropper/<时间戳> 子目录。临时路径不写回 doc_
            //（不可复用直写）；doc_ 留空让下次导出重新走本默认分支。
            tempRoot = tempBase;
            path = tempRoot + u'/' + workName(); // Core 建目录并应用命名模板。
            galleryRel = QString::fromUtf8(texts::ident::kGalleryAppDir) + u'/' + ts;
#else
            path = QFileDialog::getExistingDirectory(dialogParent_, IDC_GTR(label, kDlgTitleChooseOutDir),
                                                    UiPrefs::instance().exportSeedDir());
            if (path.isEmpty()) return;
            doc_->setOutputDir(path);
            UiPrefs::instance().setLastExportDir(path);
            panel_->syncFromDocument();
#endif
        }
        // 手输绝对路径（QFile 可写）：直写，无发布层。
    } else {
        path = doc_->outputFile();
#ifdef Q_OS_ANDROID
        // 手输的 content:// 在移动端无意义：视同留空走相册默认。
        if (!path.isEmpty() && path.startsWith(QString::fromUtf8(texts::ident::kContentUriScheme))) path.clear();
#endif
        if (path.isEmpty()) {
#ifdef Q_OS_ANDROID
            // 默认：相册 Pictures/ImageDiscropper 平铺。临时文件必须带真实格式扩展名
            //（Core 按扩展名选择编码器）；临时路径不写回 doc_（同上）。
            tempRoot = tempBase;
            QDir().mkpath(tempRoot);
            path = tempRoot + u'/' + QString::fromUtf8(texts::ident::kGalleryAppDir) + u'-' + ts
                   + u'.' + exportFormatExt(doc_->format());
            galleryRel = QString::fromUtf8(texts::ident::kGalleryAppDir);
            galleryDisplay = QFileInfo(path).fileName();
#else
            path = QFileDialog::getSaveFileName(dialogParent_, IDC_GTR(label, kDlgTitleExportAs),
                                                UiPrefs::instance().exportSeedDir(),
                                                IDC_GTR(label, kFilterExportImage));
            if (path.isEmpty()) return;
            doc_->setOutputFile(path);
            UiPrefs::instance().setLastExportDir(QFileInfo(path).absolutePath());
            panel_->syncFromDocument();
#endif
        }
#ifdef Q_OS_ANDROID
        else if (!QDir::isAbsolutePath(path)) {
            // 面板输入的自定义文件名（非路径文本）：按名称处理，缺扩展名按当前
            // 格式补全，经相册公共目录发布；绝对路径则走直写（下方不变）。
            const QString typed = path;
            tempRoot = tempBase;
            QDir().mkpath(tempRoot);
            path = tempRoot + QStringLiteral("/merged.") + exportFormatExt(doc_->format());
            QString display = QFileInfo(typed).fileName().trimmed();
            if (display.isEmpty()) display = QString::fromUtf8(texts::ident::kGalleryAppDir) + u'-' + ts;
            if (QFileInfo(display).suffix().isEmpty())
                display += QLatin1Char('.') + exportFormatExt(doc_->format());
            galleryDisplay = display;
            galleryRel = QString::fromUtf8(texts::ident::kGalleryAppDir);
        }
#endif
    }

    // 导出烧录（SPEC §8.2 导出侧的标注烧录 + 图片图层合并）：
    //   · 图片图层（多层画布）——由「导出面板·烧录图片图层」开关独立决定是否并入，与图层面板「显示」（纯预览显隐、不影响工作图）正交；
    //     composeLayerCanvasForExport(layersBurnIn)：含层直接复用 working_（恒全量合成），不含则无副作用地现算仅底图形态。
    //   · 标注——开关开且有标注时以当前导出基准逐个调 Core rasterize 合成（标注随像素被切开）。
    core::Image exportSrc = doc_->working();
    if (doc_->hasLayers()) {
        exportSrc = preprocess_->composeLayerCanvasForExport(doc_->layersBurnIn());
    }
    if (anno_->burnInEnabled() && anno_->count() > 0) {
        exportSrc = anno_->burnIn(exportSrc);
    }

    if (QString err; EngineBridge::exportResult(exportSrc, cfg, path, err)) {
#ifdef Q_OS_ANDROID
        if (!tempRoot.isEmpty()) {
            QString msg;
            const bool pubOk = publishExport(path, galleryRel, ts, galleryDisplay, msg);
            QDir(tempRoot).removeRecursively(); // 成功/失败都清临时区。
            status_->notify(msg, !pubOk);
            return;
        }
#endif
        status_->notify(IDC_GTR(status, kExportOk).arg(path), false);
    } else {
#ifdef Q_OS_ANDROID
        if (!tempRoot.isEmpty()) QDir(tempRoot).removeRecursively();
#endif
        status_->notify(IDC_GTR(status, kExportFailed).arg(err), true);
    }
}

} // namespace idc::gui
