// ============================================================================
// 文件：app/controllers/key_router.h
// 作用：KeyRouter——主窗口单键快捷键分派（1/2/3 切模式、K/R 切极性、Esc 清选区、
//       Delete/Backspace 删除选中标注）。MainWindow 只保留事件入口转发（仍须为 QMainWindow 虚函数），
//       分派逻辑与焦点守卫独立成件。
// 分块依据：快捷键是独立关注点（键表 + 焦点守卫），与装配壳/控制器无耦合；
//       MainWindow 事件入口仅保留一处转发（main_window.cpp 的 keyPressEvent，CONTRIBUTING.md「分层纪律」）。
// 说明：用 keyPressEvent 而非 QShortcut，数值输入时天然不触发（NFR-5，理由见
//       main_window.h 对 keyPressEvent 的说明）；本类为 MainWindow 的 friend（私有槽与部件直达）。
// ============================================================================
#pragma once

class QKeyEvent;

namespace idc::gui {

class MainWindow;

class KeyRouter {
public:
    // 处理单键快捷键；返回 true 表示已消费（事件已 accept，调用方直接返回）。
    static bool handleKeyPress(MainWindow* w, QKeyEvent* event);

    // 焦点是否在数值/文本输入控件上（用于放行单键快捷键）。
    static bool focusInTextInput();
};

} // namespace idc::gui
