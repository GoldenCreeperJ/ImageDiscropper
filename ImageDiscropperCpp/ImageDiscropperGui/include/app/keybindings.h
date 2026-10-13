// ============================================================================
// 文件：app/keybindings.h
// 作用：KeyBinding 数据层——把散落各处的快捷键（菜单 QAction 的组合键 + 画布单键路由）收进
//       一张**命令注册表**（单一真相源），支持用户自定义、QSettings 持久化、冲突检测与恢复默认。
// 分块依据：
//   - 与 Palette / CanvasPrefs / UiPrefs 同属「应用级偏好单例」一族：不进 §7 EngineConfig、不进
//     DocHistory 撤销栈（改键不构成可撤销的一步）；持久化各走独立 QSettings 组（shortcuts/），互不耦合；
//   - 只做「键 ↔ 命令」的数据与生效，不感知菜单/对话框如何呈现——命令名复用 menu 组 TextRef，
//     界面装配（外观对话框快捷键 Tab）与事件分派（KeyRouter）各自取本层，二者都不再各存一份键表。
// 设计取舍（双应用路径，务必理解）：
//   · 组合键类命令（Kind::Action）→ 生效即 `QAction::setShortcut(effective)`：Qt 原生处理，编辑时**强制含 Ctrl/Alt**
//     以杜绝裸字母/纯 Shift 抢占文本输入（如 Shift+A 吞大写）；
//   · 单键类命令（Kind::Route）→ **不注册为 QAction/QShortcut**（窗口级 shortcut 会先于焦点控件与
//     CanvasView::keyPressEvent 吞键、令「绘制态 Esc 收笔」永久失效），仍由 KeyRouter 走 keyPressEvent 分派，
//     只是把「按 Qt::Key_x 硬编码」改为「按本表 effective 匹配 key+modifiers → 命中回 id」。focus 守卫与
//     Esc 上游收笔顺序完全保持（见 app/controllers/key_router.cpp）。
//   · 每条命令只编辑其 **primary 序列**；DeleteAnnotation 的默认携 `Del`/`Backspace` 两个 alternate，
//     未自定义前二者皆生效，自定义后由用户完全拥有。
// 说明：
//   · defaultSeq 以 QKeySequence::StandardKey 构造（未覆盖时保持平台原生，如 mac Cmd）；覆盖值以
//     PortableText 落盘（mac 下 Ctrl 记号不回映射 Cmd，Windows 主目标，接受）；
//   · effective 为空 = 该命令已解绑（Action 类清 shortcut，Route 类不再匹配）；
//   · changed(CommandId) 在任一生效键变更后发出：菜单 QAction 即时 setShortcut、编辑器单元格即时刷新。
// ============================================================================
#pragma once

#include <QKeySequence>
#include <QObject>
#include <QString>

#include <optional>
#include <vector>

#include "i18n/gui_i18n.h"   // i18n::TextRef（命令显示名复用 menu 组常量）

class QAction;

namespace idc::gui {

// 可自定义的命令集：既有 8 组合键（Action）+ 7 画布单键（Route），共 15 条。
// 枚举序即注册表序与编辑器行序；新增命令须同步注册表默认键与显示 TextRef（见 .cpp）。
enum class CommandId {
    New, Open, Export, Undo, Redo, ZoomIn, ZoomOut, Fit,                  // Action：组合键
    ModeL1, ModeL2, ModeL3, PolarityKeep, PolarityRemove,                 // Route：画布单键
    ClearSelection, DeleteAnnotation,
    Count
};

// 应用路径：Action → QAction::setShortcut；Route → KeyRouter 表驱动分派。
enum class KeyKind { Action, Route };

// 一条命令的静态定义（注册表元素）：id + 类 + 默认键 + 可译显示名。深/浅、序列与默认仅此一处成文。
struct CommandDef {
    CommandId id;
    KeyKind kind;
    QKeySequence defaultSeq;
    i18n::TextRef display;
};

// ---------------------------------------------------------------------------
// KeyBindings：快捷键绑定单例（进程内唯一，首次访问惰性从 QSettings shortcuts/ 载入）。
// ---------------------------------------------------------------------------
class KeyBindings : public QObject {
    Q_OBJECT
public:
    static KeyBindings& instance();

    // 注册表全量（按 CommandId 枚举序），供外观对话框逐行建编辑单元。
    static const std::vector<CommandDef>& registry();

    // 当前生效键：有覆盖取覆盖（含空串=已解绑），否则取注册表默认。
    QKeySequence effective(CommandId id) const;

    static QKeySequence defaultSequence(CommandId id);
    static KeyKind kindOf(CommandId id) ;
    static i18n::TextRef displayOf(CommandId id) ;
    bool isCustomized(CommandId id) const;

    // 组合键类命令注册其 QAction 句柄（菜单装配期调用）：存句柄并立即按 effective 应用 shortcut。
    void registerAction(CommandId id, QAction* action) const;

    // 改写某命令的绑定：写覆盖 + 落盘 + 若 Action 类即时 setShortcut + 发 changed(id)。
    // seq 为空串 = 解绑该命令。
    void setSequence(CommandId id, const QKeySequence& seq);
    void resetOne(CommandId id);   // 清除覆盖，回落默认（落盘 remove + 重放 + changed）。
    void resetAll();               // 全部回落默认。

    // 把一次按键事件（key + modifiers）匹配到某条 Route 命令；命中返回其 id，否则 nullopt。
    // 逐位比对每条 Route 生效序列的各 alternate 的 key 与 modifiers（不做 int 合成，避开 Qt6 QKeyCombination→int 弃用）；
    // DeleteAnnotation 未自定义时额外接受 Backspace（默认自带）。
    std::optional<CommandId> matchRoute(int key, Qt::KeyboardModifiers mods) const;

    // 冲突检测：seq 的 primary 是否已被 exclude 之外的其它命令（任一年 alternate）占用。
    std::optional<CommandId> conflictOf(const QKeySequence& seq, CommandId exclude) const;

    // 命令的稳定小写 id 串（QSettings 键 / 调试用；非译文，不随语言变）。
    static QString idKey(CommandId id);

signals:
    // 任一命令生效键变更后发出：编辑器据此刷新单元格；Action 类命令的 QAction 亦据此热更新。
    void changed(CommandId id);

private:
    explicit KeyBindings(QObject* parent = nullptr);

    void load();
    void saveOne(CommandId id) const;   // 写当前覆盖（含空串）到 shortcuts/ 组。
    static void removeOne(CommandId id) ; // 从 shortcuts/ 组移除该键（回落默认）。
    void applyToAction(CommandId id) const;   // Action 类命令即时 setShortcut(effective)。

    std::vector<QKeySequence> override_;   // 按 (int)CommandId 索引的覆盖值（未覆盖时为空占位）。
    std::vector<bool> customized_;         // 对应位是否用户显式覆盖过（含覆盖为空=解绑）。
};

} // namespace idc::gui
