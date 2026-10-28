// ============================================================================
// 文件：app/new_canvas_dialog.h
// 作用：「文件 → 新建」模态对话框——SPEC §2.2a 合成模型的采参视图：**三段纵向折叠**（节点外观＝宽/高/
//       整体不透明度 + 图层落位（仅「添加图片图层」语境显示）+ 节点外观简表；图案元素＝PatternFormWidget
//       采参 | 色彩外观组（仅色彩步）左右并排；图片元素＝「附加图片」开关放节标题行（勾选＝启用并展开、
//       取消＝自动收起）+ [源图/适应/图片落位 | 两区外观（可含几何步）] 左右并排；屏宽 <900px 上下叠）。
//       三节皆纯折叠（标题行箭头按钮，无勾选语义——开关与折叠分离）。**预览独立固定列**：宽屏＝表单右侧
//       恒可见、窄屏＝表单下方恒可见（滚动区外）。屏幕旋转触发宽/窄形态切换，形态链按实际最小宽量测
//       降档（宽-全宽 → 宽-紧凑 → 窄），且折叠/展开等任何最小宽变化后重跑；旋转收钳/居中另有屏幕尺寸护栏兜底。
//       实时预览，接受后经 EngineBridge（委托 Core generate::makePattern / composeCanvas）产出初始画布。
//       图片不是图案类型，而是**图片元素**的可选来源：勾选后可选源图 + 三适应（裁剪/拉伸/原生）+ 图片落位
//       （图片 相对 图层：锚点/枢轴/偏移，随附加图片开关显示）；「图层 相对 底图」图层落位**仅「添加图片图层」
//       语境显示、不随附加图片开关隐藏**——新建画布产扁平整幅、无叠层概念。
//       **顶部宽/高即图层尺寸**（图层框 W×H），不另设图层框；三适应均是「图片适应到这个尺寸」。
//       **不透明度**（0–255，与图层编辑面板同词同义）为节点外观一行、恒显：作用本次产出的图层或底图整体 alpha
//       （落 transform.opacity，底图经 resetBaseWith 回填 baseOpacity_）；画布级整体透明度不在本对话框采集。
//       合成走 Core 装配序（§2.2a 唯一口径，不另列简写）。「原生」把顶部宽/高回填为源图尺寸并锁定。
// 分块依据：对话框只做「尺寸/两元素/两级落位采参 + 预览/定稿编排」，像素生成一律走桥接调 Core
//           （CONTRIBUTING.md「分层纪律」）；图案参数表单独立成件（app/forms/pattern_form）。
//           预览用小尺寸代理（按比例缩放周期/线宽/点半径/偏移）避免大图逐像素重算卡顿，接受时才产全尺寸图。
// 说明：属 app 编排层（与四控制器并列），可直接调用 model 桥；产出的 core::Image 由 SessionController 注入 Document。
//       适应模式与两级落位已升为 Core ImageLayer **持久字段**（hasImagePlacement/imageFit/boxWidth/boxHeight/
//       imageAnchor/imagePivot/imageX/imageY + 元素外观 imagePreprocess/imageOpacity + 图案元素 backdrop/
//       backdropPreprocess/backdropOpacity + transform）；config 仅凭 fileName + 这些字段即可完整复现，reload 一致。
// ============================================================================
#pragma once

#include <QDialog>

#include "core/image.h"
#include "canvas/layer.h"        // canvas::ImageTransform（图片相对图层的落位）
#include "generate/pattern.h"

class QBoxLayout;
class QCheckBox;
class QComboBox;
class QLabel;
class QPushButton;
class QScrollArea;
class QSpinBox;
class QSplitter;
class QVBoxLayout;

namespace idc::gui {

class PatternFormWidget;
class PlacementRow;
class ElementAppearanceGroup;

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

    // 忙碌框形态（SPEC §8.2）：由 SessionController 依 preprocess 控制器的 busyOverlayMode() 下发——
    // MobileShell 下为 true，定稿全尺寸生成的忙碌框铺满屏幕作全屏遮罩；桌面默认 false（居中小框）。
    void setBusyOverlayMode(const bool overlay) { busyOverlay_ = overlay; }

    // 接受后取生成的全尺寸画布（accept() 内产好；未接受时为空）。
    const core::Image& resultImage() const { return result_; }
    // 供状态栏回显的摘要文本，如「1024×768 纯色 底色(255,255,255,255)」（非纯色图案另附「前景(r,g,b,a)」）。
    QString summary() const { return summary_; }

    // 采参对话框采集的整体不透明度（0–255，255＝不透明）：accept() 时随产出载体（图层/底图）一并落定，
    //   供「添加图片图层」的纯图案层（未勾附加图片）回填 transform.opacity；图片层与底图已在 accept() 内直接携 transform.opacity。
    int resultOpacity() const { return resultOpacity_; }

    // ---- 附加图片路（**仅「添加图片图层」语境**）：产出图层内容 ----
    // 仅当处于「添加图片图层」语境且勾选了「附加图片」时 accept() 置真：SessionController::onAddImageLayer 直接取
    // resultLayer()——一个携原始源像素 + 两级落位 + 图案元素（backdrop）与两元素外观的可序列化图片层（config 完整往返）。
    // 【新建画布语境恒为假】——result_ = 整幅扁平画布（同样走 Core 合成），作底图 base（不入 layers、不可拖）。
    // 语义由「附加图片开关」驱动（图案类型不含贴图项，故无 kind==Image 判定）。
    bool producesLayerCanvas() const { return producesLayerCanvas_; }
    // 附加图片「添加图层」路：accept() 构造的可序列化结果层（原始源像素 + hasImagePlacement 内层落位/适应 + 图案元素
    //   backdrop 与两元素外观 + transform 图层落位），SessionController::onAddImageLayer 取它作新图层（仅需补 fileName）。与预览走同一条 Core composeCanvas。
    const canvas::ImageLayer& resultLayer() const { return resultLayer_; }

    // ---- 新建画布语境的底图 ImageLayer 声明（同构于图层，仅角色为钉住最底）----
    // 未勾附加图片 → 纯图案底（backdrop 图案元素 + 其外观、transform 恒等、无落位）；勾选 → 图片底
    // （fileName + 内层图片落位 + box＝画布尺寸 + backdrop 图案元素及两元素外观、transform 恒等）。
    //   供 SessionController::onNew 交 Document 作 §7 generate 段声明（使底图落位可随配置往返、可复现）。
    //   图片底已携 decodedPixels 供会话合成；序列化时上游剖离位图、仅凭 fileName 引用。
    const std::optional<canvas::ImageLayer>& resultBaseLayer() const { return resultBaseLayer_; }

    // ---- 复用为「添加图片图层」入口（LayerEditPanel 「添加图片…」）----
    // 采参完毕（accept() 后）取汇编的 PatternParams：未勾选附加图片时→作纯图案层（backdrop 图案元素，可序列化供 config 导出、可复现）。
    const generate::PatternParams& resultParams() const { return resultParams_; }
    // 图案元素外观（未勾附加图片的纯图案层回填用；附加图片路已随 resultLayer() 内联携出）。
    preprocess::PreprocessPipeline resultBackdropPreprocess() const;
    int resultBackdropOpacity() const;
    // 节点外观预处理（第三简表：新建画布＝底图整体外观仅色彩步、添加图层＝逐层预处理可含几何步；
    //   附加图片路已随 resultLayer() 内联携出，未勾附加图片的纯图案层经本访问器回填）。
    preprocess::PreprocessPipeline resultNodePreprocess() const;
    // 附加图片路：onPickImage 选中的源图完整路径（供取相对名作 File 层 provenance）。
    const QString& imageSourcePath() const { return imageSourcePath_; }
    // persistForProvenance 前的原始选取路径（桌面＝imageSourcePath_；Android SAF＝content://），供上层接入 MRU 判定。
    const QString& imageSourceOriginalPath() const { return imageSourceOriginalPath_; }
    // 切换为「添加图层」语境（默认「新建画布」；同一采参面板复用）：改标题 + 置语境标志 +
    // 附加图片开关默认关（与新建画布同口径：未勾即产纯图案层），勾选后 accept() 才产图片层内容（producesLayerCanvas）；新建画布语境下恒不产出图层。
    // 顶部宽/高 spin 预填真实画布 W×H 并**保持可编辑**：此即图层尺寸（适应目标），改小即裁剪、改大即留白。
    // 合成语义与新建画布完全一致（装配序见 §2.2a 唯一口径 + 越界裁剪），不叠底图、不撑大。
    void setAddLayerMode(int canvasW, int canvasH);

    // 父窗口 Resize/Move（旋转）→ 重跑形态链：与屏幕几何信号互为备份——Android 旋转会重建 QScreen
    //   对象，旧屏对象上的信号连接随其销毁失效；父窗口事件恒定存活（同护栏挂父窗口的口径）。
    bool eventFilter(QObject* watched, QEvent* event) override;
    // 移动端旋转的可靠触发器：平台旋转直接改本窗尺寸/位置（护栏按此钳高即证）——本窗 Resize/Move
    //   即重跑形态链并收钳（幂等：同尺寸/同位不再触发事件）。仅移动端启用——桌面用户手动拖窗不可被打扰。
    void resizeEvent(QResizeEvent* event) override;
    void moveEvent(QMoveEvent* event) override;

private slots:
    void onPickImage();          // 弹出文件对话框选源图，经 EngineBridge::loadImage 解码到 imageSource_。
    void onParamsChanged();      // 任一控件变更 → 图案表单可用性 + 图层/图片落位显隐 + 重生成预览 + 重跑形态链（显隐改变内容最小宽）。
    void accept() override;      // 全尺寸生成结果并交存后接受。

private:
    void updateControlAvailability() const;                       // 依附加图片开关/语境/适应方式显隐落位组并锁定尺寸（图案相关显隐在 PatternFormWidget 内）。
    void syncCanvasSizeToSource() const;                           // Native 适应下把顶部宽/高（图层尺寸）回填为源图原生尺寸。
    void resizeToContent();                                        // 折叠/展开变化：重算表单滚动区最小宽并让窗口跟上（同主窗页签切换更新滚动区最小宽手法）。
    void refreshModeChain();                                       // 内容最小宽变化（折叠/展开、控件显隐、语境切换）后重算最小宽并重跑形态链（防展开后最小宽顶超屏宽）。
    void applyModeChain(const QRect& avail);                       // 形态候选链（宽-全宽 → 宽-紧凑 → 窄，窄恒紧凑）：逐级量测整窗最小宽，装不下退一级；幂等可多拍重跑。
    void arrangeSections(bool narrow, bool compact);               // 形态编排：节内方向 + 外观组操作行形态 + 表单/预览外壳重挂 + 内容最小宽重算（形态未变即跳过）。
    void applyLayoutMode();                                        // 屏幕几何变化（旋转）→ 形态链降档 + 按新形态舒适尺寸重收整窗（几何收钳每次调用都做）。
    void applyWideMinimums(const QRect& avail);                    // 宽形态下限随屏自适应（构造期与旋转切换共用）：固定 480/280 下限会顶过小屏护栏钳值而溢出。
    void refreshDiag() const;                                     // 【临时诊断】刷新体内几何信息行（移动端无标题栏；定位后移除）。
    // 内层构造：按当前落位/适应/图案元素（backdrop）与两元素外观组装一个图片层（decodedPixels 预缩 factor 为预览代理，
    //   boxWidth/Height = 传入尺寸，transform = 图层落位）；预览与定稿共用，保证与 Core 合成单一真相源。
    canvas::ImageLayer buildAttachedLayer(double factor, const canvas::ImageTransform& layerPlace,
                                          const generate::PatternParams& backdrop, int boxW, int boxH,
                                          const canvas::ImageTransform& imgPlace, ImageFit fit) const;
    // 预览便捷：未选源图→纯图案底图；否则单张图层（内层合成图层框 + 外层落位）走 composeCanvas。factor≠1 为预览代理。
    core::Image composeResult(const generate::PatternParams& p, int outW, int outH, double factor,
                              const canvas::ImageTransform& layerPlace, const canvas::ImageTransform& imgPlace,
                              ImageFit fit) const;
    void refreshPreview() const;                                  // 小尺寸代理生成并缩放到预览标签。

    // 尺寸（＝图层尺寸：本对话框产出图/层的 W×H）。
    QSpinBox* widthSpin_{nullptr};
    QSpinBox* heightSpin_{nullptr};
    // 整体不透明度（0–255，255＝不透明）：作用产出的图层或底图（合成期 alpha 乘子）；画布级不在本对话框采集。
    QSpinBox* opacitySpin_{nullptr};
    // 图案参数表单（底色/前景色/类型/方向/周期/线宽/点半径/噪声；app/forms/pattern_form）。
    PatternFormWidget* patternForm_{nullptr};
    // 附加图片（可选）区块：开关驱动显隐，与图案类型解耦。
    QCheckBox* attachImageCheck_{nullptr};       // 「附加图片（可选）」开关：未勾选即可直接导出纯图案底图。
    QPushButton* imagePickButton_{nullptr};
    QLabel* imagePathLabel_{nullptr};
    QLabel*    imageFitLabel_{nullptr};          // 「适应方式：」标题。
    QComboBox* imageFitCombo_{nullptr};          // 三适应模式（存 ImageFit 于 itemData）：裁剪/拉伸/原生。
    // 两级落位（app/forms/placement_row）：图层落位（图层 相对 底图）与图片落位（图片 相对 图层）——
    // 锚点/枢轴/偏移三行一组，语义与 canvas::ImageTransform 字段一一对应。
    PlacementRow* layerPlaceRow_{nullptr};       // 图层落位：仅「添加图片图层」语境显示（新建画布隐藏）、不受附加图片开关隐藏。
    PlacementRow* imagePlaceRow_{nullptr};       // 图片落位：附加图片开启时随开关显示。
    // 外观组（SPEC §2.2a 槽位法则，三实例各携预处理简表 + 不透明度）：图案元素外观（左栏，仅色彩步——
    //   图案＝底图口径、几何步改区域尺寸）/ 图片元素外观（右栏，随附加图片开关显隐，可含几何步）/
    //   节点外观（横贯分组第三简表：新建画布语境仅色彩步、setAddLayerMode 放开几何步）；
    //   accept() 时落进 ImageLayer 的 backdrop*/image* 外观字段与节点 preprocess。
    ElementAppearanceGroup* backdropAppearance_{nullptr};
    ElementAppearanceGroup* imageAppearance_{nullptr};
    ElementAppearanceGroup* nodeAppearance_{nullptr};
    QWidget* imageSub_{nullptr};   // 图片元素节左列容器（源图/适应/落位）：随节展开显隐。
    core::Image imageSource_{};   // 已解码的源图（EngineBridge::loadImage 产出；段1 图片→图层框的输入）。
    // 预览。
    QLabel* previewLabel_{nullptr};

    core::Image result_;   // accept() 产出的全尺寸画布（新建画布扁平整幅）。
    canvas::ImageLayer resultLayer_{};   // 附加图片「添加图层」路：可序列化结果层（原始源 + 两级落位 + 图案元素与两元素外观）。
    std::optional<canvas::ImageLayer> resultBaseLayer_{};   // 新建画布路：底图 ImageLayer 声明（纯图案底 / 图片底 + 落位 + 元素外观）。
    generate::PatternParams resultParams_{};   // accept() 时汇编的图案参数（供「添加图层」未勾图时产纯图案层）。
    int resultOpacity_{255};   // accept() 时采集的整体不透明度（供纯图案层回填 transform.opacity）。
    QString imageSourcePath_;                   // 附加图片路选中的源图路径（图片层 provenance 取相对名）。
    QString imageSourceOriginalPath_;           // onPickImage 原始选取路径（持久化前）；MRU 据此仅在本地可再开路径登记。
    QString summary_;      // accept() 生成的状态栏摘要。
    bool producesLayerCanvas_{false};   // accept()：「添加图片图层」语境且勾选附加图片时置真（新建画布语境恒假）。
    bool addLayerMode_{false};          // 语境标志：setAddLayerMode() 置真（区分「添加图片图层」与「新建画布」）。
    bool busyOverlay_{false};   // 忙碌全屏遮罩形态（SessionController 依 busyOverlayMode() 下发，MobileShell 下为 true；SPEC §8.2）。
    bool narrow_{false};        // 窄形态（<kStackColumnsMaxWidthPx 起判）：节内上下叠 + 预览固定底部；宽形态装不下时形态链仍可落回窄。
    bool compact_{false};       // 操作行紧凑形态（2×2 按钮行压最小宽）：窄形态恒紧凑；宽形态装不下全宽操作行时经形态链启用。
    QWidget* body_{nullptr};    // 滚动表单主体（resizeToContent 据其最小宽重算）。
    QScrollArea* formScroll_{nullptr};   // 表单滚动区（resizeToContent 更新其最小宽）。
    QVBoxLayout* rootLayout_{nullptr};   // 对话框根布局（applyLayoutMode 重排表单/预览外壳）。
    QBoxLayout* nodeLay_{nullptr};       // 节点外观节内方向（applyLayoutMode 切换）。
    QBoxLayout* backdropLay_{nullptr};   // 图案元素节内方向（同上）。
    QBoxLayout* imageLay_{nullptr};      // 图片元素节内方向（同上）。
    QSplitter* splitter_{nullptr};       // 宽屏表单/预览分隔条（窄屏为 null）。
    QWidget* previewCol_{nullptr};       // 预览列（宽屏＝右侧列、窄屏＝底部固定）。
    QLabel* diagLabel_{nullptr};        // 【临时诊断】几何信息行（按钮行上方；移动端无标题栏故放体内；定位后移除）。

    static constexpr int kPreviewMaxDim = 256;   // 预览代理最长边（越小越流畅）。
};

} // namespace idc::gui
