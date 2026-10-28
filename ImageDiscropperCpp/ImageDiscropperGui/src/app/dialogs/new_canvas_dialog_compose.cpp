// ============================================================================
// 文件：app/dialogs/new_canvas_dialog_compose.cpp
// 作用：实现 NewCanvasDialog 的合成/定稿簇——图层组装（buildAttachedLayer：原始源 + 两级落位 +
//       图案元素与两元素外观）、合成便捷（composeResult：预览与定稿共用一条 Core 合成路径）、
//       小尺寸代理预览（refreshPreview）与定稿（accept：全尺寸两段合成 + 摘要 + 结果交存）。
// 分块依据：见 new_canvas_dialog.cpp 头注（四 TU 拆分）；本 TU 只做像素编排（走桥接调 Core，
//       不实现生成），预览代理等比缩件 scaled 来自 new_canvas_dialog_internal.h。
// ============================================================================
#include "app/new_canvas_dialog.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <vector>

#include <QCheckBox>
#include <QComboBox>
#include <QFileInfo>
#include <QLabel>
#include <QPixmap>
#include <QSpinBox>

#include "app/canvas_prefs.h"        // rememberNewCanvasSize（新建画布语境记住本次尺寸）
#include "app/controllers/preprocess_controller.h" // runInBusyDialog（全尺寸生成包忙碌框，避免 UI 冻结）
#include "app/forms/pattern_form.h"   // PatternFormWidget::params（本 TU 直接调成员，头里只有前置声明）
#include "app/forms/placement_row.h"  // PlacementRow::anchor/pivot/offsetX/offsetY（同上）
#include "generate/pattern.h"        // PatternParams / patternKindName（摘要显示名）
#include "model/engine_bridge.h"     // composeCanvas / generatePattern（像素生成走桥接）
#include "panels/element_appearance_group.h"   // pipeline()/opacity()（两元素外观与节点外观采集）
#include "util/image_qt_adapter.h"   // toPixmap（core::Image → QPixmap 预览）
#include "i18n/gui_texts.h"
#include "i18n/gui_i18n.h"

#include "new_canvas_dialog_internal.h" // scaled（预览代理等比缩）

namespace idc::gui {

// 组装一个可序列化的附加图片图层：原始源（factor≠1 先等比预缩作预览代理）+ 两级落位（内层源图→
//   图层框 imgPlace/fit，外层图层框→画布 layerPlace）+ 图案元素 backdrop（即顶部图案参数）+ 两元素外观。
//   图层框尺寸 = boxW×boxH（顶部宽高）；外层 transform 的宽高留 0（用框原生尺寸，不缩放）。
canvas::ImageLayer NewCanvasDialog::buildAttachedLayer(const double factor, const canvas::ImageTransform& layerPlace,
                                                       const generate::PatternParams& backdrop, const int boxW, const int boxH,
                                                       const canvas::ImageTransform& imgPlace, const ImageFit fit) const {
    canvas::ImageLayer layer;
    layer.decodedPixels = factor < 1.0 ? imageSource_.scaled(factor) : imageSource_;   // 预览代理先等比预缩源（无 recipe → 文件源派生）。
    layer.hasImagePlacement = true;
    layer.boxWidth = boxW;
    layer.boxHeight = boxH;
    // GUI ImageFit 与 Core canvas::ImageFit 同序（Crop/Stretch/Native），经 int 中转移换。
    layer.imageFit = static_cast<canvas::ImageFit>(static_cast<int>(fit));
    layer.imageAnchor = imgPlace.inCanvasAnchor;
    layer.imagePivot = imgPlace.layerPivot;
    layer.imageX = imgPlace.x;
    layer.imageY = imgPlace.y;
    layer.backdrop = backdrop;   // 图案元素（图层框底色/图案，Core 先铺底再叠源）。
    // 元素外观（SPEC §2.2a 槽位法则）：图案元素外观 + 图片元素外观，由对话框两外观组采集。
    layer.backdropPreprocess = backdropAppearance_->pipeline();
    layer.backdropOpacity = static_cast<std::uint8_t>(std::clamp(backdropAppearance_->opacity(), 0, 255));
    layer.imagePreprocess = imageAppearance_->pipeline();
    layer.imageOpacity = static_cast<std::uint8_t>(std::clamp(imageAppearance_->opacity(), 0, 255));
    layer.preprocess = nodeAppearance_->pipeline();   // 节点外观预处理（对话框第三简表）。
    layer.transform = layerPlace;      // 外层：图层框 相对 画布 落位（可拖、可序列化）。
    return layer;
}

// 预览便捷：未选源图→纯图案底图；否则单张附加层（内层图层框合成 + 外层落位）走 Core composeCanvas。
//   factor≠1 为预览代理（源图与图层框尺寸/偏移已由调用方等比缩）。与定稿、与 config 重载同一条合成路径。
core::Image NewCanvasDialog::composeResult(const generate::PatternParams& p, const int outW, const int outH,
                                           const double factor, const canvas::ImageTransform& layerPlace,
                                           const canvas::ImageTransform& imgPlace, const ImageFit fit) const {
    if (imageSource_.empty()) {   // 未选：纯图案底图（底色已烤入像素）。
        auto flat = EngineBridge::generatePattern(outW, outH, p);
        // 图案元素外观：预处理简表 + 元素不透明度（装配序：生成→其预处理→其透明度，与 Core materializeLayer 同口径）。
        if (!backdropAppearance_->pipeline().empty())
            flat = backdropAppearance_->pipeline().applyExec(flat);
        canvas::applyAlpha(flat, static_cast<std::uint8_t>(std::clamp(backdropAppearance_->opacity(), 0, 255)));
        // 节点外观预处理（第三简表）：装配序在区域缩放之后（纯图案按目标尺寸生成、无缩放，直接施）。
        if (!nodeAppearance_->pipeline().empty())
            flat = nodeAppearance_->pipeline().applyExec(flat);
        // 纯图案 makePattern 不施节点 alpha 乘子（合成边界单点施加、防双淡的不变式）：预览在 layerPlace.opacity 上此处施加；
        //   定稿恒以 opacity=255 调本函数（flat 保持不透明，透度另由 baseOpacity/图层 transform 于运行时施加），故不双淡。
        if (const int op = layerPlace.opacity; op < 255) canvas::applyAlpha(flat, static_cast<std::uint8_t>(op));
        return flat;
    }
    std::vector<canvas::ImageLayer> layers;   // 无 base（图案元素 backdrop 已含于层内）。
    layers.push_back(buildAttachedLayer(factor, layerPlace, p, outW, outH, imgPlace, fit));
    return EngineBridge::composeCanvas(layers, outW, outH);
}

// 小尺寸代理两段合成并缩放到预览标签（factor ≤ 1，图案参数/尺寸/偏移等比缩放，避免大图逐像素卡顿）。
// 附加图片路走 composeResult（底图→段1 图片入图层框→段2 图层框按图层落位叠到底图），令 适应方式 / 两级落位 各控件实时反映。
void NewCanvasDialog::refreshPreview() const {
    // 尺寸恒取顶部 spin＝图层尺寸＝图层框：新建画布＝画布尺寸；添加图层＝图层扁平尺寸（预填真实画布、可改）。
    const int w = widthSpin_->value();
    const int h = heightSpin_->value();

    const bool attach = attachImageCheck_->isChecked();
    if (attach && imageSource_.empty()) {   // 勾了开关尚未选源图：预览直接提示，不跑生成。
        previewLabel_->setText(IDC_GTR(label, kNeedSourceImage));
        return;
    }
    const auto fit = static_cast<ImageFit>(imageFitCombo_->currentData().toInt());

    const int pw = scaled(w, std::max(w, h) > kPreviewMaxDim ? kPreviewMaxDim / static_cast<double>(std::max(w, h)) : 1.0);
    const int ph = scaled(h, std::max(w, h) > kPreviewMaxDim ? kPreviewMaxDim / static_cast<double>(std::max(w, h)) : 1.0);
    const double factor = w > 0 ? pw / static_cast<double>(w) : 1.0;
    const auto off = [factor](const int v) { return static_cast<int>(std::lround(v * factor)); };

    // 图层落位（图层 相对 底图，常显）与图片落位（图片 相对 图层，勾选时），偏移均按 factor 等比缩。
    canvas::ImageTransform layerPlace;
    canvas::ImageTransform imgPlace;
    layerPlace.inCanvasAnchor = layerPlaceRow_->anchor();
    layerPlace.layerPivot = layerPlaceRow_->pivot();
    layerPlace.x = off(layerPlaceRow_->offsetX());
    layerPlace.y = off(layerPlaceRow_->offsetY());
    layerPlace.opacity = opacitySpin_->value();   // 预览即时反映整体不透明度（附图路经 composeCanvas 合成可见）。
    if (attach) {
        imgPlace.inCanvasAnchor = imagePlaceRow_->anchor();
        imgPlace.layerPivot = imagePlaceRow_->pivot();
        imgPlace.x = off(imagePlaceRow_->offsetX());
        imgPlace.y = off(imagePlaceRow_->offsetY());
    }

    generate::PatternParams p;
    patternForm_->params(p);
    if (factor < 1.0) {   // 周期/线宽/点半径随代理等比缩，保持纹理视觉密度一致。
        p.period = scaled(p.period, factor);
        p.lineWidth = scaled(p.lineWidth, factor);
        p.dotRadius = scaled(p.dotRadius, factor);
    }

    const core::Image img = composeResult(p, pw, ph, factor, layerPlace, imgPlace, fit);
    if (img.empty()) { previewLabel_->setText(IDC_GTR(label, kBadCanvasSize)); return; }
    const QPixmap pm = toPixmap(img);
    previewLabel_->setPixmap(pm.scaled(previewLabel_->size(), Qt::KeepAspectRatio, Qt::FastTransformation));
}

// 定稿：全尺寸两段合成（图片→图层框→叠到底图）+ 记录摘要，成功后交存并接受（空图＝尺寸非法，不关闭）。
//   渐变/图案类在全尺寸下逐像素插值可耗时数秒，重采样/落位亦按总像素计，故大尺寸（总像素≥阈值）时
//   包一层模态忙碌框，小尺寸直接生成免闪烁。
void NewCanvasDialog::accept() {
    generate::PatternParams p;
    patternForm_->params(p);
    // 尺寸恒取 spin：新建画布＝画布尺寸；添加图层＝图层扁平尺寸（预填真实画布、可改，改小即裁剪、改大即留白）。
    const int w = widthSpin_->value();
    const int h = heightSpin_->value();

    // 附加图片早校验：勾选后必须先选源图；未勾选→直接产纯图案底图（可不加图导出）。
    const bool attach = attachImageCheck_->isChecked();
    if (attach && imageSource_.empty()) {
        previewLabel_->setText(IDC_GTR(label, kNeedSourceImage));
        return;
    }

    // 两级落位：与 refreshPreview / onAddImageLayer 同口径，走两段 composeCanvas 现算（所见即所得）。
    const auto fit = static_cast<ImageFit>(imageFitCombo_->currentData().toInt());
    canvas::ImageTransform layerPlace;   // 图层 相对 底图（常显；「添加图层」语境作新层 transform，可再拖）。
    canvas::ImageTransform imgPlace;     // 图片 相对 图层。
    layerPlace.inCanvasAnchor = layerPlaceRow_->anchor();
    layerPlace.layerPivot = layerPlaceRow_->pivot();
    layerPlace.x = layerPlaceRow_->offsetX();
    layerPlace.y = layerPlaceRow_->offsetY();
    if (attach) {
        imgPlace.inCanvasAnchor = imagePlaceRow_->anchor();
        imgPlace.layerPivot = imagePlaceRow_->pivot();
        imgPlace.x = imagePlaceRow_->offsetX();
        imgPlace.y = imagePlaceRow_->offsetY();
    }
    core::Image flat;   // 新建画布扁平整幅（亦作 add-layer 的合法性/预览像素）。
    // 总像素≥ 2M 才弹忙碌框（逐像素/重采样在此量级以上开始可感）。全屏标志由 SessionController 依
    // busyOverlayMode() 下发（busyOverlay_）：MobileShell 下为 true 铺满屏幕作全屏遮罩（SPEC §8.2），
    // 桌面为 false 居中小框；两者模态阻断语义一致。
    const auto generate = [&] {
        flat = composeResult(p, w, h, 1.0, layerPlace, imgPlace, fit);   // 未选源→纯图案；否则单张附加层合成（内层+外层）。
    };
    if (static_cast<long long>(w) * static_cast<long long>(h) >= 2LL * 1000 * 1000) {
        runInBusyDialog(this, busyOverlay_, IDC_GTR(status, kNewCanvasGenerateBusy), generate);
    } else {
        generate();
    }
    if (flat.empty()) {
        if (previewLabel_) previewLabel_->setText(IDC_GTR(label, kBadCanvasSize));
        return;
    }

    // 存结果：新建画布 → result_ = flat 扁平 base；附加图片「添加图层」 → 额外构造可序列化 resultLayer_（原始源 + 两级落位 + 图层框底色）。
    // 整体不透明度：只随产出载体（图层/底图）携于声明字段，不烘进 flat 像素（底图透度由运行时 fadedCanvasBase 单点施加，避免与 baseOpacity 双淡）。
    const int op = opacitySpin_->value();
    resultParams_ = p;   // 留存汇编参数（供「添加图片图层」入口未勾图时产纯图案层，可序列化供 config 导出）。
    resultOpacity_ = op;
    result_ = flat;
    if (attach && addLayerMode_) {
        resultLayer_ = buildAttachedLayer(1.0, layerPlace, p, w, h, imgPlace, fit);   // decodedPixels＝原始源（config 仅存 fileName，重载重解码）。
        resultLayer_.transform.opacity = op;   // 图层整体不透明度（composeCanvas 合成期施加）
        producesLayerCanvas_ = true;
    }
    // 新建画布语境（非「添加图层」）：产出底图 ImageLayer 声明，使底图落位可随 §7 generate 段往返、可复现。
    if (!addLayerMode_) {
        canvas::ImageLayer base;
        base.zOrder = 0;   // 钉住最底（与叠加层 zOrder 域分离，语义标记）。
        base.transform.opacity = op;   // 底图整体不透明度：resetBaseWith 回填运行态 baseOpacity_（内存声明随后归一 255）。
        if (attach) {
            // 附图片底：文件源（携位图/落位）+ box＝画布尺寸 + backdrop＝图案元素；transform 恒等（底图不可拖）。
            base.fileName = QFileInfo(imageSourcePath_).fileName().toStdString();   // 相对名 provenance（供落/取 assets）。
            base.decodedPixels = imageSource_;   // 会话合成用原始源像素；序列化时上游剖离。
            base.hasImagePlacement = true;
            base.boxWidth = w;
            base.boxHeight = h;
            base.imageFit = static_cast<canvas::ImageFit>(static_cast<int>(fit));
            base.imageAnchor = imgPlace.inCanvasAnchor;
            base.imagePivot = imgPlace.layerPivot;
            base.imageX = imgPlace.x;
            base.imageY = imgPlace.y;
            base.backdrop = p;   // 图案元素（图层框底色/图案，Core 先铺底再叠源）。
            // 元素外观（与 buildAttachedLayer 同口径采集；底图为落位声明底，元素外观随 §7 generate 段往返）。
            base.backdropPreprocess = backdropAppearance_->pipeline();
            base.backdropOpacity = static_cast<std::uint8_t>(std::clamp(backdropAppearance_->opacity(), 0, 255));
            base.imagePreprocess = imageAppearance_->pipeline();
            base.imageOpacity = static_cast<std::uint8_t>(std::clamp(imageAppearance_->opacity(), 0, 255));
        } else {
            // 纯图案底：图案元素 + 无图片元素、transform 恒等（序列化为纯 backdrop 形态的 generate 段）。
            base.backdrop = p;
            base.backdropPreprocess = backdropAppearance_->pipeline();
            base.backdropOpacity = static_cast<std::uint8_t>(std::clamp(backdropAppearance_->opacity(), 0, 255));
        }
        base.preprocess = nodeAppearance_->pipeline();   // 节点外观＝底图整体外观（仅色彩步；SPEC §7 generate 段 preprocess 同口径）。
        resultBaseLayer_ = base;
    }
    summary_ = IDC_GTR(label, kSummaryBaseFmt)
                   .arg(w)
                   .arg(h)
                   .arg(QString::fromUtf8(generate::patternKindName(p.kind).c_str()))   // 显示名按当前语言现取（经 c_str() 走 fromUtf8 的 const char* 重载）
                   .arg(p.background.r).arg(p.background.g).arg(p.background.b).arg(p.background.a);
    // 非纯色时前景色（含 alpha）参与图案，一并回显。
    if (p.kind != generate::PatternKind::Solid)
        summary_ += IDC_GTR(label, kSummaryFgPartFmt)
                        .arg(p.foreground.r).arg(p.foreground.g).arg(p.foreground.b).arg(p.foreground.a);
    // 附加图片路回显适应模式与源图名。
    if (attach) {
        static constexpr std::array<std::pair<ImageFit, i18n::TextRef>, 3> kFitNames{{
            {ImageFit::Crop, IDC_GT(label, kFitNameCrop)}, {ImageFit::Stretch, IDC_GT(label, kFitNameStretch)},
            {ImageFit::Native, IDC_GT(label, kFitNameNative)}}};
        auto fitName = IDC_GT(label, kFitNameCrop);
        for (const auto&[f, name] : kFitNames)
            if (f == fit) { fitName = name; break; }
        summary_ += IDC_GTR(label, kSummaryAttachPartFmt)
                        .arg(i18n::qs(fitName), QFileInfo(imageSourcePath_).fileName());
    }

    // 新建画布语境（非「添加图层」）：记住本次尺寸，下次新建默认沿用（应用级 CanvasPrefs，不改历史）。
    if (!addLayerMode_) CanvasPrefs::instance().rememberNewCanvasSize(w, h);
    // 【新建画布语境不产图层】——result_ = 整幅扁平画布直接作底图 base（input 生态位、不入 layers、不可拖）。
    // 【添加图层语境产图层】resultLayer_ = 可序列化 File 层（原始源 + 两级落位 + 图层框底色），producesLayerCanvas_ 已置；config 完整往返。
    QDialog::accept();
}

} // namespace idc::gui