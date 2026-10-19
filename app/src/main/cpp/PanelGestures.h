#pragma once

// ===========================================================================
// 二级窗口的手势内核（纯逻辑，零 ImGui 依赖 ⇒ 可在 PC 上离线单测）
// ===========================================================================
// 需求（用户原话归纳）：
//   ① 顶部标签栏按住滑动 ⇒ 拖动整个窗口（任意方向），标签的点击切换必须保留
//      ⇒ 用**触摸移动阈值**区分"点击"与"拖动"；
//   ② 页面空白处上下滑动 ⇒ 滚动内容（跟手 + 惯性 + 边界夹紧，不要滚动条）；
//   ③ 组件（开关/进度条/按钮）保持自身行为，**不能被页面滚动抢走触摸**；
//   ④ 一次手势开始后**不再改归属**（不在拖动/滚动/组件之间反复横跳）。
//
// 这里只放"判定与数学"：阈值、归属优先级、滚动夹紧、惯性衰减。
// 真正的 ImGui 接线在 secondary_imgui.cpp（那边才知道 hover 到了哪个组件）。

#include <cmath>

namespace aurora {

// 手势归属。None = 还没定；Component = 交给组件自己处理。
enum class PanelGesture { None, Component, WindowDrag, Scroll };

// 触摸移动阈值（逻辑像素）。手指比鼠标抖，给得比 ImGui 默认的 6px 宽。
inline float PanelGestureThreshold(bool touchLike) { return touchLike ? 18.0f : 10.0f; }

// 是否已经超过阈值（用平方比较，省一次开方）。
inline bool PanelGestureExceeds(float dx, float dy, float threshold) {
    return dx * dx + dy * dy >= threshold * threshold;
}

// 归属优先级：**按下时**决定一次，之后不再改。
//   dragHandle : 落在专用拖动条（二级窗口**没有标题栏**了，用顶部一条看不见的把手）
//   onComponent: 落在可交互组件上（开关/进度条/按钮/标签）
//   inContent  : 落在窗口内容里（滚动）
//   otherwise  : 窗口外
//
// ★★ 顺序是 **组件优先于拖动条**，这一点与"有标题栏"时相反：
//   标题栏时代拖动把手是独立的一条、不和内容重叠，所以拖动优先没问题；
//   现在标题栏删掉了、把手就压在可滚动内容的最上面一条上，
//   若拖动优先，滑到那一条的进度条/开关就会被窗口拖动抢走触摸。
//   ⇒ 组件先赢；组件之外才是拖动条；再之外才是滚动。
inline PanelGesture ResolvePanelGesture(bool dragHandle, bool onComponent, bool inContent) {
    if (onComponent) return PanelGesture::Component;
    if (dragHandle) return PanelGesture::WindowDrag;
    if (inContent) return PanelGesture::Scroll;
    return PanelGesture::None;
}

// 滚动位置夹紧：到顶/到底就停住，**不允许越界继续跑**。
inline float ClampPanelScroll(float value, float maxScroll) {
    if (maxScroll <= 0.0f) return 0.0f;   // 内容比窗口短 ⇒ 永远停在顶部
    if (value < 0.0f) return 0.0f;
    if (value > maxScroll) return maxScroll;
    return value;
}

// 松手后的惯性。速度单位：像素/秒（屏幕像素）。
struct PanelScrollInertia {
    static constexpr float kMinVelocity = 24.0f;    // 低于它就当停了
    static constexpr float kDecayPerSecond = 6.0f;  // e^(-6t)：约 0.4s 衰减到 5%

    float velocity = 0.0f;

    bool Active() const { return velocity != 0.0f; }
    void Start(float pixelsPerSecond) { velocity = pixelsPerSecond; }
    void Stop() { velocity = 0.0f; }

    // 推进 dt 秒，返回这一帧应该滚动的位移（已经夹紧、已经考虑撞边界）。
    // ★ 撞到边界 ⇒ **立刻清零速度**，不产生"越界继续跑"的回弹位移。
    float Step(float dt, float scroll, float maxScroll) {
        if (velocity == 0.0f || dt <= 0.0f) return 0.0f;
        if (dt > 0.1f) dt = 0.1f;                    // 掉帧保护：不让一帧跳很远
        const float next = scroll + velocity * dt;
        const float clamped = ClampPanelScroll(next, maxScroll);
        if (clamped != next) {
            velocity = 0.0f;
            return clamped - scroll;
        }
        velocity *= std::exp(-kDecayPerSecond * dt);
        if (std::fabs(velocity) < kMinVelocity) velocity = 0.0f;
        return clamped - scroll;
    }
};

// 采样手指速度（用于松手后的惯性）。dt 很小时不更新，避免除零放大噪声。
inline float UpdatePanelVelocity(float previous, float deltaPixels, float dt) {
    if (dt <= 0.0f || dt > 0.2f) return previous;
    const float instant = deltaPixels / dt;
    // 一阶低通：既跟得上快速滑动，又不会被单帧抖动带飞。
    return previous * 0.6f + instant * 0.4f;
}

}  // namespace aurora
