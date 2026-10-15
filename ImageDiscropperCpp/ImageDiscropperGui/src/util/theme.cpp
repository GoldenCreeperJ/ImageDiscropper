// ============================================================================
// 文件：src/util/theme.cpp
// 作用：theme 命名空间纯函数集实现——Mode ↔ 串互转、深浅解析、QPalette 构造、Fusion 强制 + 灌
//       palette + 递归 re-polish 应用（见同名头说明）。
// 分块依据：fromString/toString 是纯映射；resolveDark 是唯一读平台/全局状态的查询；
//           buildPalette 把深/浅两张基线色表集中于此一处；apply 是唯一改全局状态的动作点。
// 说明：色表只在 buildPalette 一处成文（浅、深各一段），头文件不复制数值以免两处漂移。
//       re-polish 用递归 unpolish/polish 而非 setStyle(style())——后者对同一风格指针早退、不触发
//       重抛光（Qt QGuiApplicationPrivate::setStyle: style==当前即 return），显式抛光才保证
//       QSS 里 palette(...) 令牌按新调色板重解析。
// ============================================================================
#include "util/theme.h"

#include <QApplication>
#include <QColor>
#include <QGuiApplication>
#include <QPainter>
#include <QProxyStyle>
#include <QStyle>
#include <QStyleFactory>
#include <QStyleHints>
#include <QStyleOptionButton>
#include <QWidget>

namespace idc::gui::theme {

Mode fromString(const QString& value) {
    if (value.compare(QLatin1String("light"), Qt::CaseInsensitive) == 0) return Mode::Light;
    if (value.compare(QLatin1String("dark"), Qt::CaseInsensitive) == 0) return Mode::Dark;
    return Mode::System;   // "system" 与认不出的写法一律跟随系统。
}

QString toString(const Mode mode) {
    switch (mode) {
        case Mode::Light: return QStringLiteral("light");
        case Mode::Dark:  return QStringLiteral("dark");
        case Mode::System: break;
    }
    return QStringLiteral("system");
}

bool resolveDark(const Mode mode) {
    switch (mode) {
        case Mode::Light: return false;
        case Mode::Dark:  return true;
        case Mode::System: break;
    }
    // System：查平台配色方案（Qt≥6.5）；Unknown（个别平台/无提示）回退取当前调色板 Window 明度。
    if (const QStyleHints* hints = QGuiApplication::styleHints()) {
        const Qt::ColorScheme scheme = hints->colorScheme();
        if (scheme == Qt::ColorScheme::Dark) return true;
        if (scheme == Qt::ColorScheme::Light) return false;
    }
    return QGuiApplication::palette().color(QPalette::Window).lightness() < 128;
}

// 深色基线：标准 Fusion-dark 配方；浅色基线：贴近 Qt 默认浅色的显式成表（不依赖平台当前 palette，
// 免「从深色切回浅色时残留上一套」）。两表都在此一处，改动集中。
QPalette buildPalette(const bool dark) {
    QPalette p;
    // 派生shade 助手：以基准色明度偏移近似（正＝变亮、负＝变暗）。
    const auto shade = [](QColor c, const int d) {
        c.setHsl(c.hue(), c.saturation(), qBound(0, c.lightness() + d, 255));
        return c;
    };
    if (dark) {
        constexpr QColor window(0x35, 0x35, 0x35);
        // Base 取明显暗于 Window：Fusion 复选框/单选框指示格的**边框 = window.darker(140)**、
        // **填充 = Base**。若 Base ≈ 边框色，未勾选的空格与菜单底几乎无差 → 看着「不可见」。
        // 拉开 Base 与 Window 的明度差后，空格既有凹陷暗底、又显出比填充亮的边框，勾选/未勾选都清楚。
        constexpr QColor base(0x1a, 0x1a, 0x1a);
        constexpr QColor text(0xe0, 0xe0, 0xe0);
        constexpr QColor button(0x35, 0x35, 0x35);
        constexpr QColor highlight(0x2a, 0x5f, 0xa3);
        constexpr QColor midText(0x9a, 0x9a, 0x9a);
        p.setColor(QPalette::Window, window);
        p.setColor(QPalette::Base, base);
        p.setColor(QPalette::AlternateBase, shade(window, 8));
        p.setColor(QPalette::ToolTipBase, QColor(0x2b, 0x2b, 0x2b));
        p.setColor(QPalette::ToolTipText, text);
        p.setColor(QPalette::Text, text);
        p.setColor(QPalette::Button, button);
        p.setColor(QPalette::ButtonText, text);
        p.setColor(QPalette::WindowText, text);
        p.setColor(QPalette::BrightText, QColor(0xff, 0x55, 0x55));
        p.setColor(QPalette::Link, QColor(0x7c, 0xb3, 0xff));
        p.setColor(QPalette::LinkVisited, QColor(0xc7, 0x9a, 0xff));
        p.setColor(QPalette::Highlight, highlight);
        p.setColor(QPalette::HighlightedText, QColor(0xff, 0xff, 0xff));
        p.setColor(QPalette::PlaceholderText, midText);
        // mid/light/dark/midlight/shadow 组（边框、凹陷、滚动条槽等由这些角色派生）。
        p.setColor(QPalette::Light, shade(window, 20));
        p.setColor(QPalette::Midlight, shade(window, 12));
        p.setColor(QPalette::Mid, shade(window, -12));
        p.setColor(QPalette::Dark, shade(window, -24));
        p.setColor(QPalette::Shadow, QColor(0x16, 0x16, 0x16));
        // 禁用态：文字取更暗的灰，底随 Window（不单独设底，保留继承）。
        constexpr QColor disabledText(0x6e, 0x6e, 0x6e);
        p.setColor(QPalette::Disabled, QPalette::Text, disabledText);
        p.setColor(QPalette::Disabled, QPalette::WindowText, disabledText);
        p.setColor(QPalette::Disabled, QPalette::ButtonText, disabledText);
        p.setColor(QPalette::Disabled, QPalette::Highlight, shade(highlight, -30));
        p.setColor(QPalette::Disabled, QPalette::HighlightedText, midText);
    } else {
        // Window 回到标准浅灰 #f0f0f0（浅色 chrome 基线）。菜单复选框的可辨度不再靠压深 Window 来拾高——
        // 那会连带把整片面板染灰且对菜单无效（菜单底色走 Base）。改由下方 CheckboxOutlineStyle 在风格层接管描边。
        constexpr QColor window(0xf0, 0xf0, 0xf0);
        constexpr QColor base(0xff, 0xff, 0xff);
        constexpr QColor text(0x00, 0x00, 0x00);
        constexpr QColor button(0xf0, 0xf0, 0xf0);
        constexpr QColor highlight(0x30, 0x8c, 0xc6);
        constexpr QColor midText(0x66, 0x66, 0x66);
        p.setColor(QPalette::Window, window);
        p.setColor(QPalette::Base, base);
        p.setColor(QPalette::AlternateBase, QColor(0xf7, 0xf7, 0xf7));
        p.setColor(QPalette::ToolTipBase, QColor(0xff, 0xff, 0xdc));
        p.setColor(QPalette::ToolTipText, text);
        p.setColor(QPalette::Text, text);
        p.setColor(QPalette::Button, button);
        p.setColor(QPalette::ButtonText, text);
        p.setColor(QPalette::WindowText, text);
        p.setColor(QPalette::BrightText, Qt::red);
        p.setColor(QPalette::Link, QColor(0x00, 0x00, 0xff));
        p.setColor(QPalette::LinkVisited, QColor(0x55, 0x1a, 0x8f));
        p.setColor(QPalette::Highlight, highlight);
        p.setColor(QPalette::HighlightedText, Qt::white);
        p.setColor(QPalette::PlaceholderText, midText);
        p.setColor(QPalette::Light, QColor(0xff, 0xff, 0xff));
        p.setColor(QPalette::Midlight, QColor(0xf5, 0xf5, 0xf5));
        p.setColor(QPalette::Mid, QColor(0xa0, 0xa0, 0xa0));
        p.setColor(QPalette::Dark, QColor(0x74, 0x74, 0x74));
        p.setColor(QPalette::Shadow, QColor(0x00, 0x00, 0x00));
        constexpr QColor disabledText(0xa0, 0xa0, 0xa0);
        p.setColor(QPalette::Disabled, QPalette::Text, disabledText);
        p.setColor(QPalette::Disabled, QPalette::WindowText, disabledText);
        p.setColor(QPalette::Disabled, QPalette::ButtonText, disabledText);
        p.setColor(QPalette::Disabled, QPalette::Highlight, shade(highlight, -60));
        p.setColor(QPalette::Disabled, QPalette::HighlightedText, midText);
    }
    return p;
}

namespace {

// Fusion 复选框指示格的描边恒等于 window.darker(140).lighter(110)（见 qfusionstyle.cpp：outline 取自
// option->palette，PE_IndicatorCheckBox 用 outline.lighter(110) 画框），而**指示格填充与菜单底色同为 Base
// 角色** → 二者永远同色，未勾选的空格只剩这条随 Window 走的细描边可辨。浅色下 Base=白、菜单=白，把 Window
// 压深来加深描边会连带把整片浅色 chrome 染灰，且对菜单底色无效。故在风格层单独接管 PE_IndicatorCheckBox：
// 把传入 option 的 Window 固定成中性灰 #808080，使描边（=window.darker(140).lighter(110)≈#646464）在深/浅两色下
// 都与 Base 填充拉开稳定对比；勾选标记、单选框及其余元素一律交回 Fusion 原样绘制（保持零副作用）。
class CheckboxOutlineStyle final : public QProxyStyle {
public:
    explicit CheckboxOutlineStyle(QStyle* base) : QProxyStyle(base) {}

    void drawPrimitive(const PrimitiveElement element, const QStyleOption* option, QPainter* painter,
                       const QWidget* widget) const override {
        if (element == PE_IndicatorCheckBox) {
            if (const auto* src = qstyleoption_cast<const QStyleOptionButton*>(option)) {
                QStyleOptionButton adjusted = *src;
                adjusted.palette.setColor(QPalette::Window, QColor(0x80, 0x80, 0x80));
                QProxyStyle::drawPrimitive(element, &adjusted, painter, widget);
                return;
            }
        }
        QProxyStyle::drawPrimitive(element, option, painter, widget);
    }
};

// 递归 re-polish 单个控件及其全部子控件：unpolish→polish 让 QSS 的 palette(...) 令牌按新调色板
// 重解析；末尾 update() 触发重绘（覆盖任何在绘制期缓存了调色板派生值的自定义绘制）。
void repolishWidget(QWidget* w) {
    if (QStyle* st = w->style()) {
        st->unpolish(w);
        st->polish(w);
    }
    for (const auto children = w->children(); QObject* child : children) {
        if (auto* cw = qobject_cast<QWidget*>(child)) repolishWidget(cw);
    }
    w->update();
}

} // namespace

void apply(const Mode mode) {
    if (const auto* app = qobject_cast<QApplication*>(QCoreApplication::instance()); !app) return;   // 无 QApplication（纯 CLI / 尚未构造）：静默跳过，不触全局状态。
    // 强制 Fusion 仅首次（包一层 CheckboxOutlineStyle 专治菜单复选框描边）：之后换调色板不再重建风格
    // （重抛光由下方递归完成）。QProxyStyle 接管传入的 Fusion 实例所有权，setStyle 再接管 proxy 所有权。
    static bool s_fusionApplied = false;
    if (!s_fusionApplied) {
        QApplication::setStyle(new CheckboxOutlineStyle(QStyleFactory::create(QStringLiteral("Fusion"))));
        s_fusionApplied = true;
    }
    QApplication::setPalette(buildPalette(resolveDark(mode)));
    // 全量 re-polish：palette 变更已传播到各控件，但 QSS palette(...) 令牌须重抛光才重解析。
    for (const auto tops = QApplication::topLevelWidgets(); QWidget* top : tops) repolishWidget(top);
}

} // namespace idc::gui::theme
