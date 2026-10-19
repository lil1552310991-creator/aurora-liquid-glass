#pragma once

// ===========================================================================
// 按键边沿定序器 —— 把「按下/抬起」拆成**每帧最多一个事件**
//
// 动机（详见 Renderer.cpp 里投递鼠标事件那一段的完整注释）：
//
//   ImGui 的 `io.ConfigInputTrickleEventQueue = false` 是**必须**的 ——
//   开启涓流时实测 `io.MouseDown[0]` 恒为 false，而 ytbl 组件的按压判定
//   （secondary_imgui.cpp:877 的 `probe.held = ImGui::IsItemActive()`，
//   底层是 `ButtonBehavior` 读 `io.MouseDown[0]`）完全依赖它。
//
//   但关掉涓流有一个 imgui.cpp:9468 明确写着的**代价**：
//     「trickle=false 时同一帧内的 down/up 会折叠成最终状态 ⇒ 点击丢失」
//
//   而本项目的输入源 `InputRouter` 用两个独立的边沿标志表示按下与抬起
//   （InputRouter.cpp:58 / :84），**两者在同一帧可能同时为真** ——
//   只要一次快速点击完整发生在两帧之间就会这样。而主循环里还有
//   screencap + 降采样（需求②「真实反射桌面」），帧时间本来就会抖。
//
//   所以需要一个定序器：**每帧最多放行一个事件，DOWN 优先，同帧到达的 UP
//   顺延到下一帧**。这样从结构上保证 down 与 up 永不共享同一帧，
//   涓流的代价被完全补掉，而它的好处（不丢点击）我们不需要——
//   因为我们已经自己保证了顺序。
//
// ★ 这个类刻意做成纯逻辑、零依赖，好在 PC 上离线单测
//   （tests/MouseEdgeSequencerTest.cpp）。这类「顺序保证」的代码出错时
//   表现是「偶尔点不中」，真机上极难复现，正是最该单测的类型。
// ===========================================================================

namespace aurora {

class ButtonEdgeSequencer final {
public:
    enum class Out {
        None,
        Down,
        Up,
    };

    // 每帧调用一次，传入本帧看到的两个边沿标志，返回本帧应当投递的事件。
    Out step(bool pressed, bool released) {
        if (pressed) {
            // DOWN 优先。同帧到达的 UP 记下来，下一帧再发 —— 绝不丢。
            pendingUp_ = released;
            return Out::Down;
        }
        if (pendingUp_ || released) {
            pendingUp_ = false;
            return Out::Up;
        }
        return Out::None;
    }

    // 是否有被顺延的抬起还没发出去
    bool hasPendingUp() const { return pendingUp_; }

    // 宿主重建输入通道时用：丢掉等待中的抬起，避免它跨上下文泄漏。
    void reset() { pendingUp_ = false; }

private:
    bool pendingUp_ = false;
};

}  // namespace aurora
