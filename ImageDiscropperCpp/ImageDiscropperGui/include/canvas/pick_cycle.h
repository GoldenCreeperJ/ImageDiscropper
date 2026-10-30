// ============================================================================
// 文件：canvas/pick_cycle.h
// 作用：同点连击「循环选层」的共享状态机——重叠图元在同一处反复单击时沿叠放栈逐个下探选中
//       （到底回顶），换点或命中栈变化则复位到栈顶。标注拾取与图片图层拾取两处的同点循环逻辑共用本实现。
// 分块依据：纯值类型状态（上次命中点 + 上次命中栈 + 栈内游标 + 同点容差判定），不含渲染与命中查询——
//           命中栈由调用方算好传入（标注＝Core hitTestAll，图层＝场景 layerStackAt），本类只管循环游标推进。
// 说明：零 Qt、零外部依赖，位置以 (x, y) 传入、id 以模板参数化（标注 std::size_t / 图层 int），
//       故可被零-Qt 的 model 层与 Qt 的 canvas 层同时包含（CONTRIBUTING.md「分层纪律」）。
// ============================================================================
#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <optional>
#include <vector>

namespace idc::gui {

// PickCycle：同点连击循环选层状态机。三段语义（仿连击选层）：
//   · resolveOnPress（按下）：当前选中若仍在本次命中栈内 → 保持它（拖动跟随选中项、不因按下换选）；
//       否则取栈顶。**纯静态、不读写循环态**——游标与基准点只在 advanceOnClick 更新，否则一次点击
//       （按下 pickAt + 释放 cycleAt）会推进两位。
//   · advanceOnClick（无拖动点击的释放）：与上次命中同点（容差内）且命中栈未变 → 游标沿栈 +1（到底回顶）；
//       换点 / 栈变化 → 游标复位 0。记录本次命中点与栈作下次比较基准。空栈 → 清循环态并返回 nullopt。
//   · advanceFromTarget（同上，但按下已由场景路由出目标 id）：同点同栈且游标已指向 target 才下探，
//       否则把游标对齐到 target 所在栈位（本次仅定位）——外部选中的下层目标不会被弹回栈顶（细则见该方法）。
//   · reset：清循环态（拖动提交 / 退出拾取时调用）——拖拽不推进循环，其后同点单击应从无历史重新起算。
template <typename IdT>
class PickCycle {
public:
    // 按下解析：stack（顶在前）内若含 currentSelected → 返回该 id（保持选中）；否则返回 stack.front()。
    //   空栈返回 nullopt。不触碰循环态（游标/基准点只在 advanceOnClick 更新）。
    static std::optional<IdT> resolveOnPress(const std::vector<IdT>& stack,
                                             const std::optional<IdT>& currentSelected) {
        if (stack.empty()) return std::nullopt;
        if (currentSelected
            && std::find(stack.begin(), stack.end(), *currentSelected) != stack.end())
            return currentSelected;   // 命中栈内保持选中：拖动跟随选中项。
        return stack.front();         // 选中不在栈内 / 未选中 → 取栈顶。
    }

    // 无拖动点击的释放：按同点连击规则推进游标并返回应选中 id；空栈清态返回 nullopt。
    //   tol：判定「近似同点」的场景坐标容差——**必须由调用方按当前缩放折算后传入**（屏幕基准
    //   nums::canvas::kPickSamePointTolScreenPx / scale，见 CanvasView::updateHandleSize 的下发）。
    //   写成固定场景像素会让容差随缩放反向变化：缩小时手指抖动即被判「换点」、游标恒复位。
    std::optional<IdT> advanceOnClick(const double x, const double y,
                                      const std::vector<IdT>& stack, const double tol) {
        if (stack.empty()) { reset(); return std::nullopt; }
        const bool sameSpot = hasLast_
                              && std::hypot(x - lastX_, y - lastY_) <= tol
                              && lastStack_ == stack;
        cursor_ = sameSpot ? (cursor_ + 1) % stack.size() : 0;
        hasLast_ = true;
        lastX_ = x;
        lastY_ = y;
        lastStack_ = stack;
        return stack[cursor_];
    }

    void reset() { hasLast_ = false; lastX_ = 0.0; lastY_ = 0.0; lastStack_.clear(); cursor_ = 0; }

    // 无拖动点击的释放（按下阶段场景已定出目标 target 的版本）——**判据只看状态，不看 target 的来历**：
    //   同点同栈 **且游标已指向 target** → 连击链继续：游标沿栈 +1（到底回顶）；
    //   否则（换点 / 栈变化 / target 不在游标处——含外部选中）→ 游标**对齐到 target 所在栈位**、本次仅定位。
    //   必需性：从列表选中的下层矩形上单击若一律复位到游标 0，会被弹回栈顶（等于又换选到别的框）。
    //   已知局限：外部选中的目标**恰落在游标位**（同点同栈）时与「连击链继续」在状态上不可区分，本击仍按
    //   下探处理；要区分只能从外部选中路径重置循环态，那会打断画布→面板的回灌链，取舍后接受此语义。
    //   target 不在栈内（栈已变）→ 对齐栈顶。空栈 → 清循环态并返回 nullopt。
    std::optional<IdT> advanceFromTarget(const IdT& target, const double x, const double y,
                                         const std::vector<IdT>& stack, const double tol) {
        if (stack.empty()) { reset(); return std::nullopt; }
        const auto pos = std::find(stack.begin(), stack.end(), target);
        const std::size_t targetPos = pos == stack.end()
            ? 0
            : static_cast<std::size_t>(std::distance(stack.begin(), pos));
        const bool sameSpot = hasLast_
                              && std::hypot(x - lastX_, y - lastY_) <= tol
                              && lastStack_ == stack;
        cursor_ = sameSpot && targetPos == cursor_ ? (cursor_ + 1) % stack.size() : targetPos;
        hasLast_ = true;
        lastX_ = x;
        lastY_ = y;
        lastStack_ = stack;
        return stack[cursor_];
    }

private:
    bool hasLast_{false};        // 是否已记基准点（未记时首个 advanceOnClick 复位到栈顶、不推进）。
    double lastX_{0.0};          // 上次命中点（同点容差判定基准）。
    double lastY_{0.0};
    std::vector<IdT> lastStack_; // 上次命中栈（栈变化即视为换目标，复位游标）。
    std::size_t cursor_{0};      // 栈内游标（= 当前选中在栈中的下标）。
};

} // namespace idc::gui
