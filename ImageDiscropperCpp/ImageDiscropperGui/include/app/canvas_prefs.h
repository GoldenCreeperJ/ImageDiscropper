// ============================================================================
// 文件：app/canvas_prefs.h
// 作用：画布交互/视图偏好单例——选区吸附开关、手柄/命中区屏幕基准、内容外滚动余量、缩放步进与上下限、
//       新建画布默认尺寸的读写与 QSettings 应用级持久化；渲染/交互层绘制或换算期直接取值。
// 分块依据：
//   - 与 Palette 同属「纯呈现/交互偏好」：不进 §7 EngineConfig（作业参数态，CLI 不感知交互手感）、
//     不进 DocHistory 撤销栈（改偏好不构成可撤销的一步）；持久化走 QSettings（跨会话，对标「设置」）；
//   - 与 Palette 分家：配色与交互是两类关注点，各自独立存盘组（palette/ 与 canvas/），互不耦合。
// 说明：
//   · snapEnabled 为「运行时开关」：视图菜单 / 移动「更多」菜单挂可勾选 QAction 即时切换，
//     变更后经 changed() 联动 CanvasScene::applySnapEnabled 回设全部选区图元（无需重建）；
//   · handleScreenPx：0 = 跟随平台默认（桌面 8px / 触控 12px+，见输入适配器 screenOverlayPx），
//     >0 显式覆盖（换算手柄/抓边条带/点选阈值的屏幕基准，NFR-7 / SPEC §8.3）；
//   · scrollMarginPercent：画布内容（原图 ∪ L3 补白单元）**外面**可平移的滚动余量（**视口各边百分比**，0 = 不留，
//     默认 3）：横向按视口宽、纵向按视口高（窗口越大余量越大，与图幅与缩放档无关），由 CanvasView 按
//     `百分比×视口边 / 当前缩放` 折算注入 CanvasScene::setScrollMargins——只管「外面留多少」，内容边界不受
//     此项限制（补白块恒完整可滚入）；0 则回到无边距观感（图像边缘即滚动极限）；
//   · zoomStep/zoomMin/zoomMax：作用于工具栏 / 菜单 / 移动按钮的 zoomIn/zoomOut 与倍率钳制区间
//     （滚轮步进属桌面设备语义，留在 DesktopInputAdapter，不在此开放）；
//   · newCanvasWidth/Height：「新建画布」对话框初值 + 每次确定回写（记住上次），不改历史。
//   · changed() 在任何"需即时联动生效"的值变更后发出；rememberNewCanvasSize 只落盘不发（无画布联动）。
// ============================================================================
#pragma once

#include <QObject>

#include "util/gui_nums.h"   // 出厂默认与钳制区间（nums::app）：与外观对话框 setRange 共用一条真相源

namespace idc::gui {

// ---------------------------------------------------------------------------
// CanvasPrefs：画布交互偏好单例（进程内唯一，首次访问惰性从 QSettings 载入）。
// ---------------------------------------------------------------------------
class CanvasPrefs : public QObject {
    Q_OBJECT
public:
    static CanvasPrefs& instance();

    // 选区边/中心吸附开关（默认开）——运行时可切换。
    bool snapEnabled() const { return snapEnabled_; }
    void setSnapEnabled(bool on);

    // 手柄/命中区屏幕基准像素：0 = 跟随平台默认，>0 显式覆盖（4–64）。
    int handleScreenPx() const { return handleScreenPx_; }
    void setHandleScreenPx(int px);
    // 取实际生效基准：显式覆盖优先，否则回落传入的平台默认值。
    qreal handleScreenPxOr(const qreal platformDefault) const {
        return handleScreenPx_ > 0 ? static_cast<qreal>(handleScreenPx_) : platformDefault;
    }

    // 画布内容外面的滚动余量（**视口各边百分比**，0–50；0 = 不留余量）：视图滚动条范围 = 内容边界 + 外圈此值，
    // 横向取视口宽、纵向取视口高（故左右与上下可不等量），随窗口同比而**与图幅、缩放档无关**
    //（换算见 CanvasView::updateScrollMargins；视口尺寸变化时需重推，见 CanvasView::resizeEvent）。
    int scrollMarginPercent() const { return scrollMarginPercent_; }
    void setScrollMarginPercent(int percent);

    // 缩放步进（百分比，zoomIn/Out 的每档倍率增量；默认 25 → ×1.25 / ÷1.25）。
    int zoomStepPercent() const { return zoomStepPercent_; }
    void setZoomStepPercent(int percent);

    // 缩放倍率允许区间（百分比）：下限 1–9999、上限 (下限+1)–10000。
    int zoomMinPercent() const { return zoomMinPercent_; }
    int zoomMaxPercent() const { return zoomMaxPercent_; }
    void setZoomMinPercent(int percent);
    void setZoomMaxPercent(int percent);
    qreal zoomMinFactor() const { return zoomMinPercent_ / 100.0; }
    qreal zoomMaxFactor() const { return zoomMaxPercent_ / 100.0; }

    // 新建画布默认尺寸（对话框初值 + 记住上次）；仅落盘，不触发画布联动。
    int newCanvasWidth() const { return newCanvasWidth_; }
    int newCanvasHeight() const { return newCanvasHeight_; }
    void rememberNewCanvasSize(int w, int h);

    // 恢复出厂交互偏好（清 QSettings canvas/ 组 + 内存回缺省）。
    void restoreDefaults();

signals:
    // 任一"需即时联动生效"的值变更后发出：CanvasView 借此重算手柄尺寸与滚动余量、钳制缩放并回设吸附。
    void changed();

private:
    explicit CanvasPrefs(QObject* parent = nullptr);
    void touched();   // 变更后：落盘 + 发 changed()。
    void save() const;

    bool snapEnabled_ = nums::app::kPrefSnapDefault;
    int handleScreenPx_ = nums::app::kPrefHandlePxDefault;   // 0 = 跟随平台默认
    int scrollMarginPercent_ = nums::app::kPrefScrollMarginPercentDefault;   // 内容外面的滚动余量（视口各边百分比，0 = 不留）
    int zoomStepPercent_ = nums::app::kPrefZoomStepPercentDefault;
    int zoomMinPercent_ = nums::app::kPrefZoomMinPercentDefault;
    int zoomMaxPercent_ = nums::app::kPrefZoomMaxPercentDefault;
    int newCanvasWidth_ = nums::app::kPrefNewWidthPxDefault;
    int newCanvasHeight_ = nums::app::kPrefNewHeightPxDefault;
};

} // namespace idc::gui
