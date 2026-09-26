// ============================================================================
// 文件：mobile/mobile_shell.h
// 作用：MobileShell——移动端顶层壳（SPEC §8.3 布局）：
//       「画布为主 + 底部工具条 + 抽屉式面板」的 QMainWindow 形态，与桌面 MainWindow 并列。
// 分块依据：
//   - 继承 MainWindow 复用其**全部**业务槽、四控制器接线与 Document 单一真相源
//     （经保护构造 MobileShellTag 跳过桌面骨架；initCore 接线序列两端逐行共用）——
//     移动端不复刻任何编排/引擎逻辑（行为同构、零行为分支的结构性保证）；
//   - 骨架装配在 mobile_shell_ui.{h,cpp} 的建造者 MobileShellUi（MainWindow 的 friend，
//     与桌面 main_window_ui 同拆分纪律），本头只暴露壳类。
// 说明：公共面与 MainWindow 相同（openImageFromPath 直接继承，main.cpp 两形态同码调用）。
//       触控精度（44pt 按钮）与无键盘补偿（步进盘/取消删除按钮/长按菜单）在此装配；
//       竖/横屏双布局：resizeEvent 按窗口宽高比翻转时仅重排
//       抽屉停靠区（不重建任何部件 → 面板状态天然无损，SPEC §8.3）。
// ============================================================================
#pragma once

#include "app/main_window.h"

class QResizeEvent;

namespace idc::gui {

// ---------------------------------------------------------------------------
// MobileShell：移动端装配壳（Android 构建的顶层窗口；桌面经独立产物 idc_gui_mobile 预览）。
// ---------------------------------------------------------------------------
class MobileShell : public MainWindow {
public:
    explicit MobileShell(QWidget* parent = nullptr);

protected:
    void resizeEvent(QResizeEvent* event) override;

private:
    // 方向枚举以窗口宽高比判定（Android 先旋转后 resize，无需平台手势）。
    enum class Orientation { Portrait, Landscape };
    /// 按当前宽高比重排抽屉：竖屏底部抽屉 / 横屏右侧抽屉；仅翻转时动作（天然防抖）。
    void applyOrientationLayout();

    Orientation orientation_{Orientation::Portrait};   // 与 buildDrawer 初始底部布局一致
};

} // namespace idc::gui
