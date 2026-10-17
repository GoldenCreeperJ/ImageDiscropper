// ============================================================================
// 文件：util/theme.h
// 作用：全局 chrome（面板 / 菜单 / 对话框 / 状态栏 / 滚动条等应用控件）**深浅色主题**的
//       解析与应用——**命名空间纯函数集**，无 Theme 单例（mode 存 UiPrefs，本层不持状态）。
// 分块依据：
//   - 与 Palette（画布六语义色）**正交**：Palette 管的是画布内容色、用户可自调；本层管的是
//     应用外壳控件色、随浅色 / 深色 / 跟随系统三态切换，两者互不耦合（画布色不受主题影响）；
//   - 驱动方式：强制 QApplication::setStyle("Fusion") + 灌入完整 QPalette——原生 Windows 风格
//     忽略多数 palette 角色、无法可靠强制深色；Fusion 全角色可控、跨平台一致（代价：整体观感
//     由 windows11 风格变为 Fusion，统一且可主题化）；
//   - 纯函数、非 QObject：切换点在「启动装配」与「外观对话框选定即时的模态静止点」两处命令式
//     调用（镜像语言热切换纪律，不新增信号链也可用）。
// 说明：
//   · apply() 每次重灌 palette 并**递归 re-polish 全部控件**（Fusion + QSS palette(...) 令牌
//     在 polish 期解析，运行期改 palette 不会自动重解析样式表，故须显式 unpolish/polish）；
//   · resolveDark(System) 取 QStyleHints::colorScheme()（Qt≥6.5，本仓已部署 windows11 styles 插件
//     即 6.6+）；个别平台返回 Unknown 时回退按「当前 palette Window 明度」判定，不崩不误切；
//   · 深浅基线色表只在本 .cpp 一处成文，防两处漂移；品牌强调色（选中态蓝 / 橙 / 绿 / 红）不经本层。
// ============================================================================
#pragma once

#include <QPalette>
#include <QString>

namespace idc::gui::theme {

// 主题三态：跟随系统 / 浅色 / 深色（存 UiPrefs 的 ui/themeMode，取值为下方 toString 的小写串）。
enum class Mode { System, Light, Dark };

// Mode ↔ UiPrefs 存的字符串（"system" / "light" / "dark"）：认不出的写法回退 System。
Mode fromString(const QString& value);
QString toString(Mode mode);

// 解析该 mode 当前**应为**深色与否：System 查平台配色方案（Unknown 回退取当前 Window 明度）；
// Light→false；Dark→true。纯查询，不改任何全局状态。
bool resolveDark(Mode mode);

// 按深/浅构造完整 QPalette（关键角色 + mid/light/dark/midlight/shadow 组 + disabled 一并设）。
QPalette buildPalette(bool dark);

// 应用主题：首次强制 Fusion 风格 → 灌入 buildPalette(resolveDark(mode)) → 递归 re-polish 全部控件
// （让 QSS 的 palette(...) 令牌按新调色板重解析）。须在 QApplication 构造之后调用。
void apply(Mode mode);

} // namespace idc::gui::theme
