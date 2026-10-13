// ============================================================================
// 文件：app/new_canvas_dialog.h
// 作用：「文件 → 新建」模态对话框——采集新画布的尺寸与程序化图案参数（图案采参表单已拆至
//       panels/PatternFormWidget：底色/前景色/11 种图案/方向/周期/线宽/点半径/噪声），
//       实时预览，接受后经 EngineBridge::generatePattern（委托 Core generate::makePattern）产出初始画布。
//       图片不再是图案类型（旧「贴图图案」已彻底移除），而是独立的**可选附加层**入口：
//       勾选后可选源图 + 三适应（裁剪/拉伸/原生）+ **两级落位**（各自独立成组、勿混淆）：
//         「图片 相对 图层」（图片锚点/枢轴/位置，随附加图片开关显示）；
//         「图层 相对 底图」（图层锚点/枢轴/位置，**仅「添加图片图层」语境显示、不随附加图片开关隐藏**——新建画布产扁平整幅、无叠层概念）。
//       **顶部宽/高即图层尺寸**（图层框 W×H），不另设图层框；三适应均是「图片适应到这个尺寸」。
//       合成走 Core **两级**（已升为 ImageLayer 持久字段）：内层 源图→图层框（ImageFit 适应 + imageAnchor/Pivot/偏移 + boxBackdrop 底色），
//       外层 图层框→画布（transform）；composeCanvas 一次产图，预览 / 定稿 / config 重载同一条路径。「原生」把顶部宽/高回填为源图尺寸并锁定。
// 分块依据：对话框只做「尺寸/附加图片/两级落位采参 + 预览/定稿编排」，像素生成一律走桥接调 Core
//           （CONTRIBUTING.md「分层纪律」）；图案参数表单独立成件（app/forms/pattern_form）。
//           预览用小尺寸代理（按比例缩放周期/线宽/点半径/偏移）避免大图逐像素重算卡顿，接受时才产全尺寸图。
// 说明：属 app 编排层（与四控制器并列），可直接调用 model 桥；产出的 core::Image 由 MainWindow 注入 Document。
//       适应模式与两级落位已升为 Core ImageLayer **持久字段**（hasImagePlacement/imageFit/boxWidth/boxHeight/imageAnchor/imagePivot/imageX/imageY/boxBackdrop + transform）；config 仅凭 fileName + 这些字段即可完整复现，reload 一致。
// ============================================================================
#pragma once

#include <QDialog>

#include "core/image.h"
#include "canvas/layer.h"        // canvas::ImageTransform（图片相对图层的落位）
#include "generate/pattern.h"

class QCheckBox;
class QComboBox;
class QLabel;
class QPushButton;
class QSpinBox;

namespace idc::gui {

class PatternFormWidget;
class PlacementRow;

// ---------------------------------------------------------------------------
// ImageFit：附加图片对「图层尺寸（顶部宽/高，即本对话框产出图的 W×H）」的适应方式（与 Core canvas::ImageFit 同序，经 int 中转移写入层内持久字段）。
//   Crop    —— 图层尺寸不动，源图按原生尺寸贴入、按锚点/枢轴/偏移落位，超出图层部分自然裁剪。
//   Stretch —— 图层尺寸不动，源图重采样拉伸铺满图层（忽略宽高比，落位使不上力）。
//   Native  —— 图层尺寸恒等于源图原生尺寸（顶部宽/高回填并锁定，图片不缩放、不变形）。
// ---------------------------------------------------------------------------
enum class ImageFit { Crop, Stretch, Native };

// ---------------------------------------------------------------------------
// NewCanvasDialog：新建画布的参数采集 + 实时预览对话框（图案采参表单内嵌 PatternFormWidget）。
// ---------------------------------------------------------------------------
class NewCanvasDialog : public QDialog {
    Q_OBJECT
public:
    explicit NewCanvasDialog(QWidget* parent = nullptr);

    // 忙碌框形态（SPEC §8.2）：由 MainWindow 依 preprocess 控制器的 busyOverlayMode() 下发——
    // MobileShell 下为 true，定稿全尺寸生成的忙碌框铺满屏幕作全屏遮罩；桌面默认 false（居中小框）。
    void setBusyOverlayMode(const bool overlay) { busyOverlay_ = overlay; }

    // 接受后取生成的全尺寸画布（accept() 内产好；未接受时为空）。
    const core::Image& resultImage() const { return result_; }
    // 供状态栏回显的摘要文本，如「1024×768 纯色 底色(255,255,255,255)」（非纯色图案另附「前景(r,g,b,a)」）。
    QString summary() const { return summary_; }

    // ---- 附加图片路（**仅「添加图片图层」语境**）：产出图层内容 ----
    // 仅当处于「添加图片图层」语境且勾选了「附加图片」时 accept() 置真：MainWindow::onAddImageLayer 直接取
    // resultLayer()——一个携原始源像素 + 两级落位 + 图层框底色（boxBackdrop）的可序列化 File 层（config 完整往返）。
    // 【新建画布语境恒为假】——result_ = 整幅扁平画布（同样走 Core 合成），作底图 base（不入 layers、不可拖）。
    // 语义由「附加图片开关」驱动（旧 kind==Image 判定已随「贴图图案」移除而废弃）。
    bool producesLayerCanvas() const { return producesLayerCanvas_; }
    // 附加图片「添加图层」路：accept() 构造的可序列化结果层（原始源像素 + hasImagePlacement 内层落位/适应/图层框底色
    //   + transform 图层落位），MainWindow::onAddImageLayer 取它作新图层（仅需补 fileName）。与预览走同一条 Core composeCanvas。
    const canvas::ImageLayer& resultLayer() const { return resultLayer_; }

    // ---- 新建画布语境的底图 ImageLayer 声明（同构于图层，仅角色为钉住最底）----
    // 未勾附加图片 → Recipe 底（recipe＝图案配方、transform 恒等、无落位）；勾选 → File 底
    // （fileName + 内层图片落位 + box＝画布尺寸 + boxBackdrop＝图案配方、transform 恒等）。
    //   供 MainWindow::onNew 交 Document 作 §7 generate 段声明（使底图落位可随配置往返、可复现）。
    //   File 底已携 decodedPixels 供会话合成；序列化时上游剖离位图、仅凭 fileName 引用。
    const std::optional<canvas::ImageLayer>& resultBaseLayer() const { return resultBaseLayer_; }

    // ---- 复用为「添加图片图层」入口（LayerEditPanel 「添加图片…」）----
    // 采参完毕（accept() 后）取汇编的 PatternParams：未勾选附加图片时→作 Recipe 图层来源（recipe 可序列化供 config 导出、可复现）。
    const generate::PatternParams& resultParams() const { return resultParams_; }
    // 附加图片路：onPickImage 选中的源图完整路径（供取相对名作 File 层 provenance）。
    const QString& imageSourcePath() const { return imageSourcePath_; }
    // 切换为「添加图层」语境（默认「新建画布」；同一采参面板复用）：改标题 + 置语境标志 +
    // 附加图片开关默认开（添加图层语境必带图），使 accept() 产出图层内容（producesLayerCanvas）；新建画布语境下恒不产出图层。
    // 顶部宽/高 spin 预填真实画布 W×H 并**保持可编辑**：此即图层尺寸（适应目标），改小即裁剪、改大即留白。
    // 合成语义与新建画布完全一致（底图图案 + 两级落位两段合成 + 越界裁剪），不叠底图、不撑大。
    void setAddLayerMode(int canvasW, int canvasH);

private slots:
    void onPickImage();          // 弹出文件对话框选源图，经 EngineBridge::loadImage 解码到 imageSource_。
    void onParamsChanged() const;      // 任一控件变更 → 图案表单可用性 + 图层/图片落位显隐 + 重生成预览。
    void accept() override;      // 全尺寸生成结果并交存后接受。

private:
    void updateControlAvailability() const;                       // 依附加图片开关/语境/适应方式显隐落位组并锁定尺寸（图案相关显隐在 PatternFormWidget 内）。
    void syncCanvasSizeToSource() const;                           // Native 适应下把顶部宽/高（图层尺寸）回填为源图原生尺寸。
    // 内层构造：按当前落位/适应/图层框底色组装一个 File 图层（decodedPixels 预缩 factor 为预览代理，
    //   boxWidth/Height = 传入尺寸，transform = 图层落位）；预览与定稿共用，保证与 Core 合成单一真相源。
    canvas::ImageLayer buildAttachedLayer(double factor, const canvas::ImageTransform& layerPlace,
                                          const generate::PatternParams& boxBackdrop, int boxW, int boxH,
                                          const canvas::ImageTransform& imgPlace, ImageFit fit) const;
    // 预览便捷：未选源图→纯图案底图；否则单张图层（内层合成图层框 + 外层落位）走 composeCanvas。factor≠1 为预览代理。
    core::Image composeResult(const generate::PatternParams& p, int outW, int outH, double factor,
                              const canvas::ImageTransform& layerPlace, const canvas::ImageTransform& imgPlace,
                              ImageFit fit) const;
    void refreshPreview() const;                                  // 小尺寸代理生成并缩放到预览标签。

    // 尺寸（＝图层尺寸：本对话框产出图/层的 W×H）。
    QSpinBox* widthSpin_{nullptr};
    QSpinBox* heightSpin_{nullptr};
    // 图案参数表单（底色/前景色/类型/方向/周期/线宽/点半径/噪声；app/forms/pattern_form）。
    PatternFormWidget* patternForm_{nullptr};
    // 附加图片（可选）区块：开关驱动显隐，不再依赖图案类型（旧「贴图图案」已移除）。
    QCheckBox* attachImageCheck_{nullptr};       // 「附加图片（可选）」开关：未勾选即可直接导出纯图案底图。
    QPushButton* imagePickButton_{nullptr};
    QLabel* imagePathLabel_{nullptr};
    QLabel*    imageFitLabel_{nullptr};          // 「适应方式：」标题。
    QComboBox* imageFitCombo_{nullptr};          // 三适应模式（存 ImageFit 于 itemData）：裁剪/拉伸/原生。
    // 两级落位（app/forms/placement_row）：图层落位（图层 相对 底图）与图片落位（图片 相对 图层）——
    // 锚点/枢轴/偏移三行一组，语义与 canvas::ImageTransform 字段一一对应。
    PlacementRow* layerPlaceRow_{nullptr};       // 图层落位：仅「添加图片图层」语境显示（新建画布隐藏）、不受附加图片开关隐藏。
    PlacementRow* imagePlaceRow_{nullptr};       // 图片落位：附加图片开启时随开关显示。
    core::Image imageSource_{};   // 已解码的源图（EngineBridge::loadImage 产出；段1 图片→图层框的输入）。
    // 预览。
    QLabel* previewLabel_{nullptr};

    core::Image result_;   // accept() 产出的全尺寸画布（新建画布扁平整幅）。
    canvas::ImageLayer resultLayer_{};   // 附加图片「添加图层」路：可序列化结果层（原始源 + 两级落位 + 图层框底色）。
    std::optional<canvas::ImageLayer> resultBaseLayer_{};   // 新建画布路：底图 ImageLayer 声明（Recipe 图案底 / File 附图底 + 落位）。
    generate::PatternParams resultParams_{};   // accept() 时汇编的图案参数（供「添加图层」未勾图时产 Recipe 层）。
    QString imageSourcePath_;                   // 附加图片路选中的源图路径（File 层 provenance 取相对名）。
    QString summary_;      // accept() 生成的状态栏摘要。
    bool producesLayerCanvas_{false};   // accept()：「添加图片图层」语境且勾选附加图片时置真（新建画布语境恒假）。
    bool addLayerMode_{false};          // 语境标志：setAddLayerMode() 置真（区分「添加图片图层」与「新建画布」）。
    bool busyOverlay_{false};   // 忙碌全屏遮罩形态（MobileShell 下 MainWindow 置 true；SPEC §8.2）。

    static constexpr int kPreviewMaxDim = 256;   // 预览代理最长边（越小越流畅）。
};

} // namespace idc::gui
