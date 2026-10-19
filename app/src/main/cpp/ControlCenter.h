#pragma once

// M-09 控制中心进出场（拖拽 + 遮罩 + 模糊 + 缩放 + 液体拉伸）
//
// 逐字移植自 Kyant0/AndroidLiquidGlass 的
//   app/src/commonMain/kotlin/com/kyant/backdrop/catalog/destinations/ControlCenterContent.kt
// （443 行，本机源码逐行核对；`docs/AMDL_MOTION_CATALOG.md` M-09，编目难度标「困难」）。
// 编目称它是整份代码里最有价值的单文件。
//
// ★★ 最关键的结构 —— **这里有两条进度动画，不是一条**（源码 :84-94）：
//
//     enterProgressAnimation : 带速度、展开/收起用不对称弹簧（:167-178）
//     safeEnterProgressAnimation : 普通 spring(1, 300, 0.01)（:179-184）
//
//   然后**不同的量读不同的那条**：
//     · 读 raw `progress`（= enter 的过拖饱和值）：translationY(:110)、scaleX/Y(:112-113)、
//       边缘内缩(:120-121)、行距(:141)  —— 都是**几何**
//     · 读 `safeProgress`：alpha(:111)、遮罩(:189)、模糊(:195-199)  —— 都是**观感**
//
//   为什么必须这样：raw 那条在展开时是 ζ=0.5 的欠阻尼弹簧，会**过冲**。
//   如果让 alpha / 遮罩跟着它过冲，松手瞬间遮罩会"冲过 1 再弹回来"，
//   看起来像闪一下。safe 那条是临界阻尼，单调收敛。
//
// 其余参数（全部源码核对）：
//   dimColor          = Black @ 0.4                             :65
//   maxDragHeight     = 1000f                                  :95
//   translationY      = -48dp * (1 - progress)                 :110
//   alpha             = EaseIn(safeProgress)                   :111
//   scaleX/Y         /= 1 + 0.1 * max(progress - 1, 0)         :112-113
//   边缘内缩           = 24dp*progress, 48dp*progress            :120-121
//   行距              = 16dp * max(progress - 1, 0)             :141
//   展开弹簧           = spring(ζ=0.5, k=300, thr=0.5/maxDragHeight) + 初速 v/maxDragHeight  :172,176
//   收起弹簧           = spring(ζ=1.0, k=300, thr=0.01)          :174
//   safe 弹簧          = spring(ζ=1.0, k=300, thr=0.01)          :182
//   模糊              = 4dp * safeProgress                      :197
//
// 过拖饱和（:85-94）用的是 ProgressConverter（= sign(x)·(1 - e^{-|x|})）：
//   progress < 0  → ProgressConverter(progress)      负向最多到 -1
//   0..1          → 原值
//   progress > 1  → 1 + ProgressConverter(progress-1) 过拖最多到 2

#include <algorithm>
#include <cmath>

#include "Motion.h"

namespace aurora {

inline constexpr float kControlCenterMaxDragHeight = 1000.0f;
inline constexpr float kControlCenterScrimAlpha = 0.4f;      // 源码 :65
inline constexpr float kControlCenterTranslateDp = 48.0f;    // 源码 :110
inline constexpr float kControlCenterOverscrollScale = 0.1f; // 源码 :112-113
inline constexpr float kControlCenterEdge1Dp = 24.0f;        // 源码 :120
inline constexpr float kControlCenterEdge2Dp = 48.0f;        // 源码 :121
inline constexpr float kControlCenterRowSpacingDp = 16.0f;   // 源码 :141
inline constexpr float kControlCenterBlurDp = 4.0f;          // 源码 :197
inline constexpr float kControlCenterExpandDamping = 0.5f;   // 源码 :172
inline constexpr float kControlCenterCollapseDamping = 1.0f;  // 源码 :174
inline constexpr float kControlCenterStiffness = 300.0f;     // 源码 :172/174/182

// 过拖饱和（源码 :85-94）
inline float SaturateProgress(float raw) {
    if (raw < 0.0f) return ProgressConverter(raw);
    if (raw <= 1.0f) return raw;
    return 1.0f + ProgressConverter(raw - 1.0f);
}

// 每种量读哪条进度 —— 见文件头说明，别混用
struct ControlCenterVisual final {
    float translationY = 0.0f;   // raw
    float alpha = 0.0f;          // safe
    float scaleDivisor = 1.0f;   // raw（用法：scaleX /= scaleDivisor）
    float edge1Px = 0.0f;        // raw
    float edge2Px = 0.0f;        // raw
    float rowSpacingPx = 0.0f;   // raw
    float scrimAlpha = 0.0f;     // safe
    float blurPx = 0.0f;         // safe
};

// pxPerDp：1dp 多少像素。rawProgress / safeProgress 分别是两条动画的当前值。
inline ControlCenterVisual ComputeControlCenterVisual(float rawProgress, float safeProgress,
                                                      float pxPerDp) {
    ControlCenterVisual v;
    const float over = std::max(rawProgress - 1.0f, 0.0f);

    // ---- 读 raw 的几何量 ----
    v.translationY = -kControlCenterTranslateDp * pxPerDp * (1.0f - rawProgress);
    v.scaleDivisor = 1.0f + kControlCenterOverscrollScale * over;
    v.edge1Px = kControlCenterEdge1Dp * pxPerDp * rawProgress;
    v.edge2Px = kControlCenterEdge2Dp * pxPerDp * rawProgress;
    v.rowSpacingPx = kControlCenterRowSpacingDp * pxPerDp * over;

    // ---- 读 safe 的观感量 ----
    v.alpha = EaseIn(safeProgress);
    v.scrimAlpha = kControlCenterScrimAlpha * safeProgress;
    v.blurPx = kControlCenterBlurDp * pxPerDp * safeProgress;
    return v;
}

// 松手时的目标进度（源码 :162-166）
//   速度 < 0 → 收起(0)；速度 > 0 → 展开(1)；速度 == 0 → 看当前位置是否过半
inline float ControlCenterReleaseTarget(float velocity, float currentProgress) {
    if (velocity < 0.0f) return 0.0f;
    if (velocity > 0.0f) return 1.0f;
    return currentProgress < 0.5f ? 0.0f : 1.0f;
}

// ===========================================================================
// 控制中心面板的**几何布局** —— 从 M-09 的观感量算出面板最终落在哪、多大、多透明
//
// ★ 这一段刻意做成**纯函数**（不碰 ImGui、不碰 ytbl、不碰全局状态），
//   这样可以在 PC 上离线严格验证（tests/ControlCenterTest.cpp 第 10 组）。
//   MotionLab.h 里的绘制只负责把这里算出来的数喂给 lgx::drawPanel()。
//
// 之所以值得单独拎出来测：面板的位置/尺寸在收起态是**在屏幕外上方**的
// （translationY 为负），一旦符号写反，收起动画会变成「往下飞出去」，
// 而那种错误在真机上只表现为「看着有点怪」，很难一眼看出是符号问题。
// ===========================================================================
struct ControlCenterLayout final {
    float left = 0.0f;
    float top = 0.0f;
    float right = 0.0f;
    float bottom = 0.0f;
    float alpha = 0.0f;        // 面板整体透明度
    float scrimAlpha = 0.0f;   // 遮罩浓度
    float blurPx = 0.0f;       // 背景模糊半径
    float rowSpacingPx = 0.0f; // 过拖时才撑开的行距
};

// stageLeft/stageTop : 舞台左上角（屏幕坐标）
// stageHeight        : 舞台高度（拖拽位移按它归一化）
// panelWidth/Height  : 面板的设计尺寸（未缩放）
// topPaddingPx       : 面板距舞台上沿的初始留白
inline ControlCenterLayout ComputeControlCenterLayout(float rawProgress, float safeProgress,
                                                      float pxPerDp, float stageLeft,
                                                      float stageTop, float stageWidth,
                                                      float stageHeight, float panelWidth,
                                                      float panelHeight, float topPaddingPx) {
    ControlCenterLayout out;
    const ControlCenterVisual v =
        ComputeControlCenterVisual(rawProgress, safeProgress, pxPerDp);

    const float cx = stageLeft + stageWidth * 0.5f;
    // ★ translationY 在收起态是负的（-48dp），所以 top 会跑到舞台上方 → 屏幕外
    const float top = stageTop + topPaddingPx + v.translationY;
    const float halfW = panelWidth * 0.5f / v.scaleDivisor;  // 过拖时 scaleDivisor>1 ⇒ 变小
    const float halfH = panelHeight * 0.5f / v.scaleDivisor;
    const float cy = top + panelHeight * 0.5f;

    out.left = cx - halfW;
    out.right = cx + halfW;
    out.top = cy - halfH;
    out.bottom = cy + halfH;
    out.alpha = v.alpha;
    out.scrimAlpha = v.scrimAlpha;
    out.blurPx = v.blurPx;
    out.rowSpacingPx = v.rowSpacingPx;
    return out;
}

// 展开/收起的不对称弹簧（源码 :169-177）
//   ★ 展开（target = 1）用 ζ=0.5 —— **会过冲**，这是"弹出来"的手感来源
//   ★ 收起（target = 0）用 ζ=1.0 —— 不过冲，干脆利落地退场
inline Spring MakeControlCenterSpring(float targetProgress, float maxDragHeight) {
    if (targetProgress > 0.5f) {
        Spring s = Spring::FromStiffness(0.0f, kControlCenterExpandDamping, kControlCenterStiffness);
        s.setThreshold(0.5f / maxDragHeight);
        return s;
    }
    Spring s =
        Spring::FromStiffness(0.0f, kControlCenterCollapseDamping, kControlCenterStiffness);
    s.setThreshold(0.01f);
    return s;
}

// safe 那条永远是普通临界阻尼弹簧（源码 :179-184）
inline Spring MakeControlCenterSafeSpring() {
    Spring s =
        Spring::FromStiffness(1.0f, kControlCenterCollapseDamping, kControlCenterStiffness);
    s.setThreshold(0.01f);
    return s;
}

// 进出场状态机：两条进度一起推。
class ControlCenterState final {
public:
    ControlCenterState() {
        enter_ = MakeControlCenterSpring(1.0f, kControlCenterMaxDragHeight);
        enter_.snap(1.0f);
        safe_ = MakeControlCenterSafeSpring();
        safe_.snap(1.0f);
    }

    // 两条动画的当前值
    float enterProgress() const { return enter_.value(); }
    float safeProgress() const { return safe_.value(); }
    // 过拖饱和后的几何进度
    float progress() const { return SaturateProgress(enter_.value()); }

    // 拖拽中直接改（源码 :86-94 = derivedStateOf，几何上等于把动画值直接写进去）
    void setDragProgress(float raw) {
        enter_.snap(raw);
    }

    // 两条一起瞬移到位。用于「重置」这类不该有动画的场合。
    // 注意：只 snap enter_ 会让 safe 慢慢追（正是拖拽时想要的），
    // 但重置时两条都应立刻到位。
    void snapTo(float v) {
        enter_.snap(v);
        safe_.snap(v);
    }

    // 松手（源码 :162-184）
    // velocity 单位是"进度/秒"（源码里已经除以 maxDragHeight 归一化过）
    void release(float velocity, float maxDragHeight = kControlCenterMaxDragHeight) {
        const float current = enter_.value();
        const float target = ControlCenterReleaseTarget(velocity, current);

        // 换 spec **并保留当前值与初速** —— 对应源码的
        // `enterProgressAnimation.animateTo(target, spec, velocity / maxDragHeight)`：
        // 目标、阻尼参数、初速三者同时给。
        enter_ = MakeControlCenterSpring(target, maxDragHeight);
        enter_.snap(current);          // 注意 snap() 会把 target 也设成 current、速度清零
        enter_.setTarget(target);      // ⇒ 必须在这之后再设目标
        enter_.setVelocity(velocity);  // ⇒ 以及初速
        safe_.setTarget(target);
    }

    void tick(float deltaSeconds) {
        enter_.tick(deltaSeconds);
        safe_.tick(deltaSeconds);
    }

    bool atRest() const { return enter_.atRest() && safe_.atRest(); }

private:
    Spring enter_{};
    Spring safe_{};
};

}  // namespace aurora
