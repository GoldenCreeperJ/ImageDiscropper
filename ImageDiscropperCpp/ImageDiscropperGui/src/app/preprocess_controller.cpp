// ============================================================================
// 文件：app/preprocess_controller.cpp
// 作用：实现 PreprocessController（见同名头文件说明）。
// 分块依据：
//   - applyWorkingImage：维度变化失效清理 + 写回工作图（公共收尾）；
//   - runWithBusyDialog：耗时重采样的模态忙碌对话框；
//   - 8 个 on* 槽：各操作经 EngineBridge 调 Core pixel_ops 后走公共收尾。
// ============================================================================
#include "app/preprocess_controller.h"

#include <QApplication>
#include <QProgressDialog>
#include <QString>

#include "app/status_bar.h"
#include "model/annotation_bridge.h"
#include "model/document.h"
#include "model/engine_bridge.h"
#include "panels/image_panel.h"

namespace idc::gui {

PreprocessController::PreprocessController(QObject* parent) : QObject(parent) {}

void PreprocessController::setDocument(Document* doc) { doc_ = doc; }
void PreprocessController::setAnnotationBridge(AnnotationBridge* anno) { anno_ = anno; }
void PreprocessController::setStatusBar(StatusBar* status) { status_ = status; }
void PreprocessController::setDialogParent(QWidget* parent) { dialogParent_ = parent; }
void PreprocessController::setImagePanel(ImagePanel* panel) { imagePanel_ = panel; }

// 图像处理面板（预处理）：各意图信号→对应槽（经 EngineBridge 调 Core pixel_ops）。
void PreprocessController::connectSignals() {
    connect(imagePanel_, &ImagePanel::rotateRequested, this, &PreprocessController::onRotate);
    connect(imagePanel_, &ImagePanel::flipRequested, this, &PreprocessController::onFlip);
    connect(imagePanel_, &ImagePanel::scaleRequested, this, &PreprocessController::onScale);
    connect(imagePanel_, &ImagePanel::resizeRequested, this, &PreprocessController::onResize);
    connect(imagePanel_, &ImagePanel::grayRequested, this, &PreprocessController::onGray);
    connect(imagePanel_, &ImagePanel::invertRequested, this, &PreprocessController::onInvert);
    connect(imagePanel_, &ImagePanel::splitRequested, this, &PreprocessController::onSplit);
    connect(imagePanel_, &ImagePanel::resetRequested, this, &PreprocessController::onResetPreprocess);
}

// 预处理公共收尾：维度变化（旋转 90/270、缩放）会使既有选区坐标越界/失配，
// 故先清除失效选区（避免 Core 切割报错），再写回工作图（触发 imageChanged→重建预览底图+刷新）。
void PreprocessController::applyWorkingImage(core::Image next, const QString& okMsg) const {
    if (!doc_->hasImage()) return;
    if (next.empty()) { status_->notify(QStringLiteral("预处理失败：结果为空图"), true); return; }
    if (next.width() != doc_->width() || next.height() != doc_->height()) {
        doc_->clearRect();    // 单选区坐标基于旧尺寸，已失效。
        doc_->clearRects();   // L2 多矩形同理。
        doc_->clearCells();   // L3 选择集序号对应旧网格，一并清空。
        anno_->clearAll();    // 维度变化使标注坐标失配，一并清空（G-4）。
    }
    doc_->setWorkingImage(std::move(next));
    status_->notify(okMsg, false);
}

// 在模态忙碌对话框内同步执行 op：缩放/尺寸重采样在大图上可能耗时，为避免用户误以为卡死
// 而在处理期间再次点击，弹出不可取消、应用级模态的进度对话框（不确定进度条）阻断其余输入。
// 同步执行下忙碌条不会动画，但 processEvents 先保证对话框绘制出来；执行完立即关闭。
void PreprocessController::runWithBusyDialog(const QString& text, const std::function<void()>& op) const {
    QProgressDialog dlg(text, QString(), 0, 0, dialogParent_);
    dlg.setWindowTitle(QStringLiteral("正在处理图像"));
    dlg.setWindowModality(Qt::ApplicationModal); // 模态阻断全部其他窗口的输入。
    dlg.setCancelButton(nullptr);                // 不可取消（重采样中途无法安全回退）。
    dlg.setMinimumDuration(0);                   // 立即显示，不等阈值。
    dlg.setRange(0, 0);                          // 不确定进度（忙碌滚动样式）。
    dlg.show();
    QApplication::processEvents();               // 先让对话框绘制出来，再进入耗时处理。
    op();
    dlg.close();
}

// 旋转（90 的整数倍；-90=左转、90=右转、180）。
void PreprocessController::onRotate(const int angleDeg) {
    if (!doc_->hasImage()) return;
    applyWorkingImage(EngineBridge::rotateImage(doc_->working(), angleDeg),
                      QStringLiteral("已旋转 %1°").arg(angleDeg));
}

// 翻转（水平/垂直）。
void PreprocessController::onFlip(const bool horizontal) {
    if (!doc_->hasImage()) return;
    applyWorkingImage(EngineBridge::flipImage(doc_->working(), horizontal),
                      horizontal ? QStringLiteral("已水平翻转") : QStringLiteral("已垂直翻转"));
}

// 按比例缩放（factor 为倍数）。可能耗时，包在模态忙碌对话框内并加重入守卫。
void PreprocessController::onScale(const double factor) {
    if (!doc_->hasImage() || busyResample_) return;
    busyResample_ = true;
    runWithBusyDialog(QStringLiteral("正在按比例缩放图像，请稍候…"), [this, factor] {
        applyWorkingImage(EngineBridge::scaleImage(doc_->working(), factor),
                          QStringLiteral("已缩放至 %1%").arg(qRound(factor * 100.0)));
    });
    busyResample_ = false;
}

// 目标尺寸缩放。可能耗时，包在模态忙碌对话框内并加重入守卫。
void PreprocessController::onResize(const int newWidth, const int newHeight) {
    if (!doc_->hasImage() || busyResample_) return;
    busyResample_ = true;
    runWithBusyDialog(QStringLiteral("正在缩放到目标尺寸，请稍候…"), [this, newWidth, newHeight] {
        applyWorkingImage(EngineBridge::resizeImage(doc_->working(), newWidth, newHeight),
                          QStringLiteral("已缩放到 %1×%2").arg(newWidth).arg(newHeight));
    });
    busyResample_ = false;
}

// 黑白（灰度）。
void PreprocessController::onGray() {
    if (!doc_->hasImage()) return;
    applyWorkingImage(EngineBridge::toGrayImage(doc_->working()), QStringLiteral("已转为黑白"));
}

// 色道反色：依勾选的 R/G/B 拼出长度 3 的反相掩码（'1' 反相、'0' 保持）；灰度图 Core 忽略掩码、整体反相。
void PreprocessController::onInvert(const bool invR, const bool invG, const bool invB) {
    if (!doc_->hasImage()) return;
    const std::string mask = std::string(invR ? "1" : "0") + (invG ? "1" : "0") + (invB ? "1" : "0");
    applyWorkingImage(EngineBridge::invertImage(doc_->working(), mask), QStringLiteral("已按通道反色"));
}

// 色道分离：依勾选的 R/G/B 拼出长度 3 的保留掩码（'1' 保留、'0' 置零）。
void PreprocessController::onSplit(const bool keepR, const bool keepG, const bool keepB) {
    if (!doc_->hasImage()) return;
    const std::string mask = std::string(keepR ? "1" : "0") + (keepG ? "1" : "0") + (keepB ? "1" : "0");
    applyWorkingImage(EngineBridge::splitImage(doc_->working(), mask), QStringLiteral("已按通道分离"));
}

// 重置预处理：工作图恢复为原图。若原图与当前工作图尺寸不同（曾旋转/缩放），先清失效选区。
void PreprocessController::onResetPreprocess() const {
    if (!doc_->hasImage() || !doc_->hasPreprocess()) return;
    if (doc_->original().width() != doc_->width() || doc_->original().height() != doc_->height()) {
        doc_->clearRect();
        doc_->clearRects();
        doc_->clearCells();
    }
    doc_->resetPreprocess();
    status_->notify(QStringLiteral("已重置预处理，恢复原图"), false);
}

} // namespace idc::gui
