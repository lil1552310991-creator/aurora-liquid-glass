#pragma once

// M-11 / M-12 的「液滴铺开」形变 —— 苹果液态玻璃按压时最标志性的那一下。
//
// 逐字移植自 Kyant0/AndroidLiquidGlass（本机源码逐行核对）：
//   M-11 LiquidToggle : app/src/commonMain/kotlin/com/kyant/backdrop/catalog/components/LiquidToggle.kt
//                        :75（pressedScale）:149-152（铺开）:160（模糊）:168-173（边光）:190-193（速度拉伸）
//   M-12 LiquidSlider : .../LiquidSlider.kt  :83 :158-161 :169 :177-182 :199-202
//
// 两者**只差两个数**，其余完全同构：
//
//   | 量 | M-11 Toggle | M-12 Slider |
//   |---|---|---|
//   | 铺开后 scaleY 上限 | `0.75f` | `1.0f` |
//   | 速度除数 | `50f` | `10f` |
//   | 铺开后 scaleX 上限 | `0.75f` | `1.0f` |
//   | 起始 scaleX | `2f/3f` | `2f/3f`（相同） |
//   | 起始 scaleY | `0f` | `0f`（相同） |
//   | 模糊 | `8dp * (1 - progress)` | 同 |
//   | 速度拉伸权重 | `0.75 / 0.25` | 同 |
//   | 拉伸夹取范围 | `±0.2` | 同 |
//
// ★ 目录里特别标注过的一个陷阱（`docs/KYANT_BACKDROP_PARAM_SURFACE.md` §3.2 分组 4）：
//   按下态的边光用的是 `Highlight.Ambient.width / 1.5f` 与 `blurRadius / 1.5f`
//   （源码 `LiquidToggle.kt:170-171`），**不是** `Highlight.Ambient` 的库默认值。
//   库默认是 0.5dp / 0.25dp，组件实际用 0.3333dp / 0.1667dp。
//   ytbl 的常量（`GlassConfig.h:84-85` 的 `EDGE_AMBIENT_WIDTH=0.33333334` /
//   `EDGE_AMBIENT_SOFTNESS=0.16666667`）对齐的正是后者 —— 这里照 ytbl 那一套。

#include <algorithm>
#include <cmath>

#include "Motion.h"   // EaseOut：M-13 的越界跟手曲线用它

namespace aurora {

enum class DropletKind {
    Toggle,   // M-11：库里的液体玻璃开关
    Slider,   // M-12：库里的液体玻璃滑块
};

// 按下态边光的两个常量 —— 见文件头注释，取自 ytbl，不是 Kyant 的库默认值。
inline constexpr float kEdgeAmbientWidthDp = 0.33333334f;
inline constexpr float kEdgeAmbientSoftnessDp = 0.16666667f;

inline float Lerp(float a, float b, float t) { return a + (b - a) * t; }

struct DropletMorph final {
    float scaleX = 1.0f;
    float scaleY = 1.0f;
    float blurPx = 0.0f;   // 已经乘过 pxPerDp
};

// pressProgress : M-10 的 pressProgress()，[0,1]（1 = 完全按下）
// velocity      : M-10 的 velocity()，**已经归一化过**（除以区间跨度）
// pxPerDp       : 1dp 等于多少像素（本机 density 场景下由 lgx::px 决定）
inline DropletMorph ComputeDropletMorph(DropletKind kind, float pressProgress, float velocity,
                                        float pxPerDp) {
    const bool isToggle = (kind == DropletKind::Toggle);
    const float openSpan = isToggle ? 0.75f : 1.0f;   // 两个数里的一处差异

    // ---- 铺开（源码 :150-151 / :159-160）----
    float scaleX = Lerp(2.0f / 3.0f, openSpan, pressProgress);
    float scaleY = Lerp(0.0f, openSpan, pressProgress);

    // ---- 速度拉伸（源码 :191-193 / :200-202）----
    // velocityDivisor 是两处差异里的第二处。
    const float divisor = isToggle ? 50.0f : 10.0f;
    const float v = velocity / divisor;
    // 注意源码是 `sx /= (1 - clamp(...))`，不是 `sx *= (1 - ...)`。
    // 夹取到 ±0.2 保证分母落在 [0.8, 1.2]，不会翻转或爆炸。
    scaleX /= 1.0f - std::clamp(v * 0.75f, -0.2f, 0.2f);
    scaleY *= 1.0f - std::clamp(v * 0.25f, -0.2f, 0.2f);

    // ---- 模糊（源码 :160 / :169）：按下越深越清晰 ----
    const float blurPx = 8.0f * pxPerDp * (1.0f - pressProgress);

    return DropletMorph{scaleX, scaleY, blurPx};
}

// ===========================================================================
// M-13 标签栏「选中胶囊迁移」（LiquidBottomTabs）
//
// 逐字移植自 Kyant0/AndroidLiquidGlass 的
//   app/src/commonMain/kotlin/com/kyant/backdrop/catalog/components/LiquidBottomTabs.kt
// （本机源码逐行核对；`docs/AMDL_MOTION_CATALOG.md` M-13）。
//
// ★ 编目把这一条特别标成「**弹窗形变母版**」—— 苹果液态玻璃弹窗那套
//   「胶囊从一个位置长到另一个位置」的形变，参数与结构都在这条里。
//
// 四个组成部分，各自独立：
//   1. 索引弹簧      spring(ζ=1.0, k=300, thr=0.5)         源码 :118
//   2. 选中胶囊放大  lerp(1, 1 + 16dp/胶囊宽度, progress)  源码 :176
//   3. 越界跟手位移  4dp · sign(f) · EaseOut(|f|)          源码 :90-92
//   4. 速度拉伸      velocity/10，除数与权重同 M-11/M-12   源码 :270-272
//
// 按下时的整体缩放 pressedScale = 78/56（源码 :109）—— 这是个**分数**，
// 不是常见的 1.5；78 与 56 是设计稿里的两个尺寸。
// ===========================================================================

inline constexpr float kTabsPressedScale = 78.0f / 56.0f;   // ≈1.392857，源码 :109
inline constexpr float kTabsIndexThreshold = 0.5f;          // 索引弹簧的 visibilityThreshold
inline constexpr float kTabsIndexStiffness = 300.0f;        // 源码 :118
inline constexpr float kTabsSelectedGrowDp = 16.0f;         // 选中胶囊放大量（源码 :176）
inline constexpr float kTabsOverscrollDp = 4.0f;            // 越界跟手幅度（源码 :92）
inline constexpr float kTabsPressScaleSpan = 1.2f;          // lerp(1, 1.2, pp)（源码 :192）

// 2. 选中胶囊放大（源码 :176）
//    `lerp(1f, 1f + 16f.dp.toPx() / size.width, progress)`
//    progress ∈ [0,1]，pillWidthPx 是胶囊自身宽度（不是屏幕宽）。
inline float TabsSelectedPillScale(float progress, float dp16Px, float pillWidthPx) {
    if (pillWidthPx <= 0.0f) return 1.0f;
    return Lerp(1.0f, 1.0f + dp16Px / pillWidthPx, progress);
}

// 3. 越界跟手位移（源码 :90-92）
//    fraction = clamp(offset / maxWidth, -1, 1)
//    返回：4dp · sign(fraction) · EaseOut(|fraction|)
//    ⇒ 越界越远越难拖（EaseOut 收敛到 1），且**永远不超过 4dp**。
inline float TabsOverscrollOffsetPx(float offsetPx, float widthPx, float dp4Px) {
    if (widthPx <= 0.0f) return 0.0f;
    const float fraction = std::clamp(offsetPx / widthPx, -1.0f, 1.0f);
    const float sign = (fraction < 0.0f) ? -1.0f : 1.0f;
    return dp4Px * sign * EaseOut(std::abs(fraction));
}

// 4. 速度拉伸（源码 :270-272）—— 与 M-11/M-12 同构，除数固定为 10。
inline void ApplyTabsVelocityStretch(float& scaleX, float& scaleY, float velocity) {
    const float v = velocity / 10.0f;
    scaleX /= 1.0f - std::clamp(v * 0.75f, -0.2f, 0.2f);
    scaleY *= 1.0f - std::clamp(v * 0.25f, -0.2f, 0.2f);
}

// 1. 索引弹簧的构造参数（放在这里以便面板/测试共用同一份数）
inline Spring MakeTabsIndexSpring(float initialIndex) {
    Spring s = Spring::FromStiffness(initialIndex, 1.0f, kTabsIndexStiffness);
    s.setThreshold(kTabsIndexThreshold);
    return s;
}

// ===========================================================================
// M-15 液体玻璃按钮的「液滴跟随」（LiquidButton）
//
// 逐字移植自 Kyant0/AndroidLiquidGlass 的
//   app/src/commonMain/kotlin/com/kyant/backdrop/catalog/components/LiquidButton.kt
//   （本机源码逐行核对；`docs/AMDL_MOTION_CATALOG.md` M-15）
//
// ★ 它与 M-14 是**配套**的：这里的 `offset` 就是 M-14 的
//   `interactiveHighlight.offset`（= 当前位置 − 按下位置）。所以正确顺序是
//   TouchHighlight 先跑，再把它的 offset 喂进来。
//
// 三条公式（源码 :69-86）：
//
//   1. 位移饱和 —— 这是「液滴」手感的核心：
//        translation = minDimension * tanh(0.05 * offset / minDimension)
//      在 offset=0 处导数 = **0.05**，也就是按钮只按手指位移的 5% 跟手；
//      随位移增大 tanh 饱和，**位移永远不超过短边**（不会飞出去）。
//      少了 tanh 直接线性跟手，就变成普通拖拽，没有"液体黏滞"感。
//
//   2. 按压放大：scale = lerp(1, 1 + 4dp/height, pressProgress)
//      注意分母是**按钮自身高度**，所以越矮的按钮放大比例越大。
//
//   3. 拖拽拉伸：maxDragScale = 4dp/height
//        scaleX = scale + maxDragScale * |cos(角度)*offset.x/长边| * min(w/h, 1)
//        scaleY = scale + maxDragScale * |sin(角度)*offset.y/长边| * min(h/w, 1)
//      两个 `min(..., 1)` 是**宽高比修正**：扁按钮的竖直拉伸、竖按钮的水平拉伸都会被压制。
// ===========================================================================

// 静止时的玻璃配方（源码 :60-61）
inline constexpr float kButtonBlurDp = 2.0f;
inline constexpr float kButtonLensHeightDp = 12.0f;
inline constexpr float kButtonLensAmountDp = 24.0f;
// 位移饱和的初始导数（源码 :72）
inline constexpr float kButtonDragDerivative = 0.05f;
// 按压放大与拖拽拉伸都用 4dp（源码 :69 / :77）
inline constexpr float kButtonMorphDp = 4.0f;
// 表面着色的第二层透明度（源码 :94）
inline constexpr float kButtonTintOverlayAlpha = 0.75f;

// 位移饱和（源码 :74-75）。offset 是相对按下点的位移，minDim 是短边。
inline float ButtonTranslation(float offset, float minDimension) {
    if (minDimension <= 0.0f) return 0.0f;
    return minDimension * std::tanh(kButtonDragDerivative * offset / minDimension);
}

struct ButtonMorph final {
    float translationX = 0.0f;
    float translationY = 0.0f;
    float scaleX = 1.0f;
    float scaleY = 1.0f;
};

// dp4Px = 4dp 对应的像素数（lgx::px(4.0f)）
inline ButtonMorph ComputeButtonMorph(float pressProgress, float offsetX, float offsetY, float width,
                                      float height, float dp4Px) {
    ButtonMorph out;
    if (width <= 0.0f || height <= 0.0f) return out;

    const float minDim = std::min(width, height);
    const float maxDim = std::max(width, height);

    // ---- 1. 位移饱和 ----
    out.translationX = ButtonTranslation(offsetX, minDim);
    out.translationY = ButtonTranslation(offsetY, minDim);

    // ---- 2. 按压放大（源码 :69）----
    const float scale = Lerp(1.0f, 1.0f + dp4Px / height, pressProgress);

    // ---- 3. 拖拽拉伸（源码 :77-86）----
    const float maxDragScale = dp4Px / height;
    const float angle = std::atan2(offsetY, offsetX);
    // 宽高比修正：扁的压竖、竖的压横
    const float widthBias = std::min(width / height, 1.0f);
    const float heightBias = std::min(height / width, 1.0f);
    out.scaleX =
        scale + maxDragScale * std::abs(std::cos(angle) * offsetX / maxDim) * widthBias;
    out.scaleY =
        scale + maxDragScale * std::abs(std::sin(angle) * offsetY / maxDim) * heightBias;

    return out;
}

}  // namespace aurora
