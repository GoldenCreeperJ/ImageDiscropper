// ============================================================================
// 文件：app/controllers/session_controller.cpp
// 作用：实现 SessionController 的会话生命周期编排与图层编辑面板接线（见同名头文件说明）；
//       提示经 status_->notify 发出、对话框父窗口取 dialogParent_。
// ============================================================================
#include "app/controllers/session_controller.h"

#include <QDir>
#include <QCheckBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QMessageBox>

#include "app/controllers/doc_history.h"
#include "app/new_canvas_dialog.h"
#include "app/controllers/preprocess_controller.h"
#include "app/app_prefs.h"
#include "app/status_bar.h"
#include "canvas/canvas_mode.h"
#include "model/annotation_bridge.h"
#include "model/document.h"
#include "model/engine_bridge.h"
#include "panels/layer_edit_panel.h"
#include "panels/layer_panel.h"
#include "panels/mode_tool_panel.h"
#include "i18n/gui_texts.h"
#include "i18n/gui_i18n.h"

namespace idc::gui {

SessionController::SessionController(QObject* parent) : QObject(parent) {}

void SessionController::setDocument(Document* doc) { doc_ = doc; }
void SessionController::setAnnotationBridge(AnnotationBridge* anno) { anno_ = anno; }
void SessionController::setPreprocessController(PreprocessController* preprocess) { preprocess_ = preprocess; }
void SessionController::setHistory(DocHistory* history) { history_ = history; }
void SessionController::setStatusBar(StatusBar* status) { status_ = status; }
void SessionController::setToolPanel(ModeToolPanel* toolPanel) { toolPanel_ = toolPanel; }
void SessionController::setLayerEditPanel(LayerEditPanel* panel) { layerEditPanel_ = panel; }
void SessionController::setLayerPanel(LayerPanel* panel) { layerPanel_ = panel; }
void SessionController::setDialogParent(QWidget* parent) { dialogParent_ = parent; }

// 图层编辑面板（多层画布）：意图信号 → Document 声明态 CRUD（均发 layersChanged → 控制器重组）。
// 图层「选中」由画布拾取承担（见 CanvasCoordinator）；面板上/下移/删除以当前拾取层为作用对象。
void SessionController::connectSignals() {
    connect(layerEditPanel_, &LayerEditPanel::layerMoveRequested, this,
            [this](const int id, const bool up) { doc_->moveLayerZOrder(id, up); });
    connect(layerEditPanel_, &LayerEditPanel::layerMoveEdgeRequested, this,
            [this](const int id, const bool toTop) { doc_->moveLayerToEdge(id, toTop); });
    connect(layerEditPanel_, &LayerEditPanel::layerRemoveRequested, this,
            [this](const int id) { doc_->removeLayer(id); });
    connect(layerEditPanel_, &LayerEditPanel::layerTransformRequested, this,
            [this](const int id, const canvas::ImageTransform &t) {
                if (const canvas::ImageLayer* layer = doc_->layerById(id)) {
                    canvas::ImageLayer updated = *layer;
                    updated.transform = t;
                    doc_->updateLayer(id, updated);
                }
            });
    connect(layerEditPanel_, &LayerEditPanel::canvasSizeRequested, this,
            [this](const int width, const int height) { doc_->setCanvasSize(width, height); });
    connect(layerEditPanel_, &LayerEditPanel::addImageRequested, this, &SessionController::onAddImageLayer);
    connect(layerEditPanel_, &LayerEditPanel::clearAllLayersRequested, this, &SessionController::onClearAllLayers);
    // 不透明度目标＝底图（Base）时驱动底图整体透明度（→ setBaseOpacity → layersChanged 重组以淡出底图）。
    connect(layerEditPanel_, &LayerEditPanel::baseOpacityRequested, this,
            [this](const int opacity) { doc_->setBaseOpacity(opacity); });
    // 不透明度目标＝画布（Canvas）时驱动画布级整体透明度（→ setCanvasOpacity 发 changed，随后重放 working_）。
    connect(layerEditPanel_, &LayerEditPanel::canvasOpacityRequested, this,
            [this](const int opacity) {
                doc_->setCanvasOpacity(opacity);
                preprocess_->replayCanvasPreprocess();   // 重算 working_（画布级透明度经 commitPipeline 产出边界施加）。
            });
    // 「图层」面板图片图层单开关：显示 → Document::setImageLayersVisible（落运行态标志 imageLayersVisible_，发 imageLayersVisibleChanged 纯显隐切换，不重组）。
    connect(layerPanel_, &LayerPanel::imageLayerVisibilityChanged, this,
            [this](const bool visible) { doc_->setImageLayersVisible(visible); });
}

// 打开图像：弹出文件对话框选取路径，再交由 openImageFromPath 载入。
// 起始目录取上次打开目录（UiPrefs 跨会话记忆；空则回落系统默认）。
void SessionController::onOpen() const {
    const QString path = QFileDialog::getOpenFileName(dialogParent_, IDC_GTR(label, kDlgTitleOpenImage),
        UiPrefs::instance().lastOpenDir(),
        IDC_GTR(label, kFilterOpenImage));
    if (path.isEmpty()) return;
    openImageFromPath(path);
}

// 新建：**非破坏性**——以对话框生成的扁平画布为新底图（input 生态位），保留现有图层/标注/色彩预处理，
//   仅剥除会改变尺寸的 resize+几何预处理（防重放改变新底图尺寸）。同尺寸新建不清空图层/标注/预处理。
//   注入链路：捕获标注 → Document::resetBaseWith（换底图/剥 resize+几何/尺寸变则清框选）→ layersChanged→recompose 展平重放色彩预处理
//   → 复位标注会话到新工作图 → 回填原标注（始终保留）→ 换撤销会话。新建图无文件路径，imagePath_ 置空。
void SessionController::onNew() const {
    NewCanvasDialog dlg(dialogParent_);
    dlg.setBusyOverlayMode(preprocess_->busyOverlayMode());   // 移动端忙碌框铺满屏幕（SPEC §8.2 形态同构）。
    if (dlg.exec() != QDialog::Accepted) return;
    core::Image img = dlg.resultImage();
    if (img.empty()) { status_->notify(IDC_GTR(status, kSessionNewFailedBadSize), true); return; }

    auto captured = anno_->annotations();   // 复制当前标注（矢量坐标，独立于底图像素）。
    // 有图恒图层画布——resetBaseWith 换 baseImage_ 后发 layersChanged → recompose 自动展平 + 重放色彩预处理产出 working_。
    doc_->resetBaseWith(std::move(img), dlg.resultBaseLayer());   // 换底图、留存底图声明（图案/附图片落位）、保留图层/色彩预处理、剥 resize+几何。
    anno_->setBaseImage(doc_->working());                 // 复位标注会话到新工作图（会清空集合）。
    if (!captured.empty()) anno_->replaceAnnotations(std::move(captured));   // 回填原标注（始终保留，尺寸变亦留）。
    history_->reset();                        // 换底图开新撤销会话。
    // 附加图片底（新建画布内选源图）也接入 MRU：与「打开图像」同口径——仅本地可再开路径登记、跳过 content://
    //（Android SAF 原始 URI 不可复用，provenance 另存的私有副本亦不入最近列表）。
    if (const auto& base = dlg.resultBaseLayer(); base && !base->recipe.has_value() && !base->decodedPixels.empty()) {
        if (const QString& src = dlg.imageSourceOriginalPath(); !src.isEmpty() && !src.startsWith(QString::fromUtf8(texts::ident::kContentUriScheme))) {
            UiPrefs::instance().addRecentFile(src);
            UiPrefs::instance().setLastOpenDir(QFileInfo(src).absolutePath());
        }
    }
    status_->notify(IDC_GTR(status, kSessionNewOk).arg(dlg.summary()), false);
}

// 图层编辑「添加图片」：复用「新建画布」对话框采参，往现有画布追加一个图片图层（SPEC §8.2：不撑大，
// 超出画布部分合成时自然裁剪）。有图即图层画布；ensureLayerCanvas 仅作空文档兜底。
//   · 勾选附加图片 路 → File 层：携「适应 + 图片相对图层」内层落位烘进图层框的像素（图层内容）+ 外层「图层相对底图」落位作 transform，另携源图相对名 provenance（位图不入配置、无法由配方复现）。
//   · 未勾选（纯图案）路 → Recipe 层：携 PatternParams，合成时按 transform 尺寸经 makePattern 现算（配方入 config、可导出、可复现）。
// id / zOrder 由 Document::addImageLayer 统一分配。
void SessionController::onAddImageLayer() const {
    if (!doc_->hasImage()) {
        status_->notify(IDC_GTR(status, kSessionNeedImageForLayer), true);
        return;
    }
    doc_->ensureLayerCanvas();   // 兜底确保图层画布（有图恒真、无操作）。
    NewCanvasDialog dlg(dialogParent_);
    dlg.setAddLayerMode(doc_->canvasWidth(), doc_->canvasHeight());   // 宽高预填真实画布（可改＝图层扁平尺寸）。
    dlg.setBusyOverlayMode(preprocess_->busyOverlayMode());
    if (dlg.exec() != QDialog::Accepted) return;

    canvas::ImageLayer layer;
    if (dlg.producesLayerCanvas()) {
        // 附加图片路：取对话框构造的可序列化 File 层（原始源 + 两级落位 + 图层框底色），仅补 fileName（相对名作 provenance、供落 assets）。
        layer = dlg.resultLayer();
        if (const QString& srcPath = dlg.imageSourcePath(); !srcPath.isEmpty())
            layer.fileName = QFileInfo(srcPath).fileName().toStdString();
    } else {
        // 未勾附加图片的程序化图案：作配方层——recipe 可序列化供 config 导出、合成时 makePattern 现算（与对话框预览逐像素一致）。
        layer.recipe = dlg.resultParams();
        const core::Image& generated = dlg.resultImage();
        layer.transform.width = generated.width();    // 目标尺寸＝对话框生成的图案尺寸。
        layer.transform.height = generated.height();
        layer.transform.opacity = dlg.resultOpacity();   // 整体不透明度（对话框采集）：与附加图片 File 层同口径走 transform.opacity。
    }
    const int newId = doc_->addImageLayer(std::move(layer));   // → layersChanged → recomposeLayerCanvas → working_ → 落位框刷新。
    doc_->selectLayer(newId);                                  // 新层设为当前预处理/编辑目标。
    toolPanel_->setMode(CanvasMode::ImagePick);               // 切图片拾取态：落位框即时可拾取 / 拖拽且高亮。
    status_->notify(IDC_GTR(status, kSessionLayerAdded), false);
}

// 清空全部叠加图片图层：弹确认后仅留底图（保留图层画布/标注/预处理声明），layersChanged→recompose 重放色彩预处理。
void SessionController::onClearAllLayers() const {
    if (!doc_->hasLayers()) { status_->notify(IDC_GTR(status, kSessionNoLayersToClear), true); return; }
    if (const auto ret = QMessageBox::question(dialogParent_, IDC_GTR(msg, kClearLayersTitle),
            IDC_GTR(msg, kClearLayersAsk)
                .arg(doc_->layerCount()),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        ret != QMessageBox::Yes) return;
    doc_->clearAllLayersToBase();            // 仅清叠加层、保留图层画布；发 layersChanged → recompose 以仅底图重出 working_（含色彩预处理重放）。
    if (doc_->selectedLayerId()) doc_->selectLayer(std::nullopt);
    toolPanel_->setMode(CanvasMode::Marquee);   // 离开图片拾取态。
    status_->notify(IDC_GTR(status, kSessionLayersCleared), false);
}

// 一键重置全部：清图层（回底图）+ 清全部标注 + 重置全部预处理（含几何/resize）+ 清选区，默认仅保留底图。
// 对话框内嵌「同时删除底图」复选框（QMessageBox::setCheckBox，默认不勾）：勾选则连底图一并删、文档回空白态。
void SessionController::onDestroyAll() const {
    if (!doc_->hasImage()) { status_->notify(IDC_GTR(status, kSessionNothingToReset), true); return; }
    QMessageBox box(QMessageBox::Question, IDC_GTR(msg, kResetAllTitle), IDC_GTR(msg, kResetAllAsk),
                    QMessageBox::Yes | QMessageBox::No, dialogParent_);
    box.setDefaultButton(QMessageBox::No);
    QCheckBox deleteBaseCheck(IDC_GTR(msg, kResetAllDeleteBase), &box);
    box.setCheckBox(&deleteBaseCheck);
    if (box.exec() != QMessageBox::Yes) return;

    const bool deleteBase = deleteBaseCheck.isChecked();
    if (deleteBase) {
        doc_->resetToEmpty();                 // 连底图一并删→空白文档（发 imageChanged → 预览 clearAll 清场；选区/图层/预处理随之置空）。
        anno_->setBaseImage(doc_->working()); // working_ 已空→复位标注会话＝清空全部标注 + 重置重做栈。
    } else {
        if (doc_->hasLayers()) doc_->clearAllLayersToBase();   // 仅清叠加层、保留底图为图层画布（layersChanged→recompose 以仅底图重出）。
        doc_->resetPreprocess();                               // 清画布预处理（含 resize/几何），working_=original_。
        anno_->setBaseImage(doc_->working());                  // 复位标注会话＝清空全部标注 + 重置重做栈。
        doc_->clearRect();
        doc_->clearRects();
        doc_->clearCells();
        if (doc_->selectedLayerId()) doc_->selectLayer(std::nullopt);
    }
    toolPanel_->setMode(CanvasMode::Marquee);
    history_->reset();
    status_->notify(deleteBase ? IDC_GTR(status, kSessionAllResetNoBase)
                               : IDC_GTR(status, kSessionAllReset), false);
}

// 从给定路径载入图像（委托 EngineBridge → Core readImageFile），成功后建预览并刷新。
void SessionController::openImageFromPath(const QString& path) const {
    if (path.isEmpty()) return;

    // Android SAF：文件选择器返回 content:// URI（内容提供者地址、非文件路径），Core 的 stb 按 C 接口
    // 读文件路径开不了——EngineBridge::loadImage 内部先经 materializeForRead 物化为真实临时路径再解码，
    // 故此处直接委托、无需再内联拷贝（桌面平台返回本地路径，物化分支不触发）。doc_ 与提示仍保留原始路径。
    const bool isContent = path.startsWith(QString::fromUtf8(texts::ident::kContentUriScheme));
    core::Image img;
    if (QString err; !EngineBridge::loadImage(path, img, err)) {
        // content:// 物化/解码失败给专用提示；其余透传解码错误（均非模态）。
        status_->notify(isContent ? IDC_GTR(status, kSessionOpenContentFailed)
                                  : IDC_GTR(status, kSessionOpenFailed).arg(err), true);
        return;
    }
    doc_->setImage(std::move(img), path); // 触发 imageChanged。
    anno_->setBaseImage(doc_->working()); // 新图＝新标注会话（Core setImage 清空旧标注）。
    history_->reset();          // 换图开新撤销会话：清空跨图历史（快照不含图像，跨图撤销无意义）。
    // 会话记忆（UiPrefs，QSettings 应用级、非 §7、不入撤销栈）：本地可再开路径记 MRU + 回写上次打开目录；
    // content://（SAF 临时内容 URI）不可复用直开，跳过 MRU。
    if (!isContent) {
        UiPrefs::instance().addRecentFile(path);
        UiPrefs::instance().setLastOpenDir(QFileInfo(path).absolutePath());
    }
    status_->notify(IDC_GTR(status, kSessionOpened).arg(path).arg(doc_->width()).arg(doc_->height()), false);
}

} // namespace idc::gui
