// ============================================================================
// 文件：app/canvas_prefs.cpp
// 作用：CanvasPrefs 单例实现——QSettings（canvas/ 组）载入/落盘、各交互偏好的存取与恢复默认。
// 分块依据：持久化组名与键名取自 texts::ident（常量层），在构造/落盘/恢复默认三处成对引用；
//           出厂默认与钳制区间取 nums::app（常量层）；
//           取值器全部内联在头文件，本件只有变更路径。
// 说明：缩放区间维护「min < max」不变式——设下限越大时抬升上限、设上限越小时压低下限，
//       避免持久化出非法区间；步进/手柄各自独立钳制到合理范围。
// ============================================================================
#include "app/canvas_prefs.h"

#include <algorithm>

#include <QSettings>

#include "i18n/gui_texts.h"

namespace idc::gui {

CanvasPrefs& CanvasPrefs::instance() {
    // Meyers 单例：C++11 起线程安全；生命周期随进程（QSettings 写入在变更点即时完成）。
    static CanvasPrefs s;
    return s;
}

CanvasPrefs::CanvasPrefs(QObject* parent)
    : QObject(parent) {
    QSettings st;
    st.beginGroup(QString::fromUtf8(texts::ident::kGroupCanvas));
    snapEnabled_ = st.value(QString::fromUtf8(texts::ident::kCanvasSnap), nums::app::kPrefSnapDefault).toBool();
    handleScreenPx_ = st.value(QString::fromUtf8(texts::ident::kCanvasHandlePx), nums::app::kPrefHandlePxDefault).toInt();
    scrollMarginPercent_ = st.value(QString::fromUtf8(texts::ident::kCanvasScrollMargin), nums::app::kPrefScrollMarginPercentDefault).toInt();
    zoomStepPercent_ = st.value(QString::fromUtf8(texts::ident::kCanvasZoomStep), nums::app::kPrefZoomStepPercentDefault).toInt();
    zoomMinPercent_ = st.value(QString::fromUtf8(texts::ident::kCanvasZoomMin), nums::app::kPrefZoomMinPercentDefault).toInt();
    zoomMaxPercent_ = st.value(QString::fromUtf8(texts::ident::kCanvasZoomMax), nums::app::kPrefZoomMaxPercentDefault).toInt();
    newCanvasWidth_ = st.value(QString::fromUtf8(texts::ident::kCanvasNewWidth), nums::app::kPrefNewWidthPxDefault).toInt();
    newCanvasHeight_ = st.value(QString::fromUtf8(texts::ident::kCanvasNewHeight), nums::app::kPrefNewHeightPxDefault).toInt();
    st.endGroup();
}

void CanvasPrefs::touched() {
    save();
    emit changed();
}

void CanvasPrefs::save() const {
    QSettings st;
    st.beginGroup(QString::fromUtf8(texts::ident::kGroupCanvas));
    st.setValue(QString::fromUtf8(texts::ident::kCanvasSnap), snapEnabled_);
    st.setValue(QString::fromUtf8(texts::ident::kCanvasHandlePx), handleScreenPx_);
    st.setValue(QString::fromUtf8(texts::ident::kCanvasScrollMargin), scrollMarginPercent_);
    st.setValue(QString::fromUtf8(texts::ident::kCanvasZoomStep), zoomStepPercent_);
    st.setValue(QString::fromUtf8(texts::ident::kCanvasZoomMin), zoomMinPercent_);
    st.setValue(QString::fromUtf8(texts::ident::kCanvasZoomMax), zoomMaxPercent_);
    st.setValue(QString::fromUtf8(texts::ident::kCanvasNewWidth), newCanvasWidth_);
    st.setValue(QString::fromUtf8(texts::ident::kCanvasNewHeight), newCanvasHeight_);
    st.endGroup();
    st.sync();
}

void CanvasPrefs::setSnapEnabled(const bool on) {
    if (snapEnabled_ == on) return;
    snapEnabled_ = on;
    touched();
}

void CanvasPrefs::setHandleScreenPx(const int px) {
    // 0（跟随平台默认）或 kHandlePxMin–kHandlePxMax 显式覆盖。
    handleScreenPx_ = px <= 0 ? 0 : std::clamp(px, nums::app::kHandlePxMin, nums::app::kHandlePxMax);
    touched();
}

void CanvasPrefs::setScrollMarginPercent(const int percent) {
    // 0 = 不留余量（图像边缘即滚动极限，无边距观感）；上限 50%（两侧各 50% 就把视口吃满，再大内容无可见区）。
    scrollMarginPercent_ = std::clamp(percent, 0, nums::app::kScrollMarginPercentMax);
    touched();
}

void CanvasPrefs::setZoomStepPercent(const int percent) {
    zoomStepPercent_ = std::clamp(percent, nums::app::kZoomStepPercentMin, nums::app::kZoomStepPercentMax);
    touched();
}

void CanvasPrefs::setZoomMinPercent(const int percent) {
    zoomMinPercent_ = std::clamp(percent, nums::app::kZoomMinPercentLower, nums::app::kZoomMinPercentUpper);
    if (zoomMinPercent_ >= zoomMaxPercent_) zoomMaxPercent_ = zoomMinPercent_ + 1; // 维持 min < max
    touched();
}

void CanvasPrefs::setZoomMaxPercent(const int percent) {
    zoomMaxPercent_ = std::clamp(percent, nums::app::kZoomMaxPercentLower, nums::app::kZoomMaxPercentUpper);
    if (zoomMaxPercent_ <= zoomMinPercent_) zoomMinPercent_ = zoomMaxPercent_ - 1; // 维持 min < max
    touched();
}

void CanvasPrefs::rememberNewCanvasSize(const int w, const int h) {
    if (w <= 0 || h <= 0) return;
    if (newCanvasWidth_ == w && newCanvasHeight_ == h) return;
    newCanvasWidth_ = w;
    newCanvasHeight_ = h;
    save();   // 仅落盘：新建尺寸不影响当前画布，无需发 changed()。
}

void CanvasPrefs::restoreDefaults() {
    {
        QSettings st;
        st.beginGroup(QString::fromUtf8(texts::ident::kGroupCanvas));
        st.remove(QString::fromUtf8(texts::ident::kCanvasSnap));
        st.remove(QString::fromUtf8(texts::ident::kCanvasHandlePx));
        st.remove(QString::fromUtf8(texts::ident::kCanvasScrollMargin));
        st.remove(QString::fromUtf8(texts::ident::kCanvasZoomStep));
        st.remove(QString::fromUtf8(texts::ident::kCanvasZoomMin));
        st.remove(QString::fromUtf8(texts::ident::kCanvasZoomMax));
        st.remove(QString::fromUtf8(texts::ident::kCanvasNewWidth));
        st.remove(QString::fromUtf8(texts::ident::kCanvasNewHeight));
        st.endGroup();
        st.sync();
    }
    snapEnabled_ = nums::app::kPrefSnapDefault;
    handleScreenPx_ = nums::app::kPrefHandlePxDefault;
    scrollMarginPercent_ = nums::app::kPrefScrollMarginPercentDefault;
    zoomStepPercent_ = nums::app::kPrefZoomStepPercentDefault;
    zoomMinPercent_ = nums::app::kPrefZoomMinPercentDefault;
    zoomMaxPercent_ = nums::app::kPrefZoomMaxPercentDefault;
    newCanvasWidth_ = nums::app::kPrefNewWidthPxDefault;
    newCanvasHeight_ = nums::app::kPrefNewHeightPxDefault;
    touched();
}

} // namespace idc::gui
