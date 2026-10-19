#pragma once

// M-14 触摸点高亮（InteractiveHighlight）—— 玻璃按钮上的「手指光斑」。
//
// 逐字移植自 Kyant0/AndroidLiquidGlass（本机源码逐行核对）：
//   app/src/commonMain/kotlin/com/kyant/backdrop/catalog/utils/InteractiveHighlight.kt（121 行）
// 规格另见 docs/AMDL_MOTION_CATALOG.md M-14。
//
// 两条 spring（源码 :27-30）：
//   pressProgress  spring(ζ=0.5, k=300, thr=0.001)
//   position       spring(ζ=0.5, k=300, thr=Offset.VisibilityThreshold = 0.01)
//
// ★★ 三个**极易写错**的点，逐一说明：
//
//   1. `smoothstep(radius, radius * 0.5, dist)` 的 **edge0 > edge1**
//      （源码 :52）。GLSL 规范里 edge0 >= edge1 是**未定义**的，但 Skia/AGSL 实现的是
//      那个三次多项式，它自然就反转了：
//          t = clamp((x - edge0) / (edge1 - edge0), 0, 1);  return t*t*(3-2t)
//      代入 edge0=r、edge1=r/2：
//          t = (dist - r) / (-r/2) = 2(r - dist)/r
//          dist = 0     → t = 2 → 夹到 1 → 强度 1
//          dist = r/2   → t = 1        → 强度 1
//          dist = 0.75r → t = 0.5      → 强度 0.5
//          dist = r     → t = 0        → 强度 0
//      ⇒ 实心核半径 = r/2 = **minDimension * 0.75**，到 **minDimension * 1.5** 归零。
//      如果按常规写法 smoothstep(0, radius, dist) 会得到**完全相反**的图（中心黑、边缘亮）。
//
//   2. 一共有**两层**，都是 `BlendMode.Plus`（加法混合），不是一层：
//        · 全矩形  White × 0.08 × progress            （源码 :66）
//        · 径向光斑 White × 0.15 × progress × intensity（源码 :72）
//      编目记的「White 0.15p + 0.08p」就是指这两层。
//
//   3. 位置的动画语义是「**拖动时瞬移、松手才弹回**」：
//        按下 :102  position.snapTo(startPosition)   —— 瞬移
//        拖动 :118  position.snapTo(change.position) —— 瞬移（**不是弹簧**）
//        松手 :108  position.animateTo(startPosition) —— 弹簧回位
//      把拖动也写成弹簧会让光斑"追不上手指"，是明显的拖影感。
//
//   另：不支持 RuntimeShader 时有降级分支 —— 只画 `White × 0.25 × progress` 全矩形（:86）。

#include <algorithm>
#include <cmath>

#include "Motion.h"

namespace aurora {

// Compose 的 Offset.VisibilityThreshold
inline constexpr float kOffsetVisibilityThreshold = 0.01f;

// 不支 RuntimeShader 时的降级透明度系数（源码 :86）
inline constexpr float kHighlightFallbackAlpha = 0.25f;
// 两层加法混合的系数（源码 :66 / :72）
inline constexpr float kHighlightAmbientAlpha = 0.08f;
inline constexpr float kHighlightSpotAlpha = 0.15f;
// 光斑半径 = 短边 × 该系数（源码 :73）
inline constexpr float kHighlightRadiusFactor = 1.5f;

// 复刻 AGSL 的 `smoothstep(radius, radius * 0.5, dist)`。
// ★ 注意参数顺序是 (edge0=radius, edge1=radius*0.5)，**反向**。
inline float HighlightIntensity(float dist, float radius) {
    if (radius <= 0.0f) return 0.0f;
    const float edge0 = radius;
    const float edge1 = radius * 0.5f;
    const float denom = edge1 - edge0;
    if (denom == 0.0f) return dist <= edge0 ? 1.0f : 0.0f;
    float t = (dist - edge0) / denom;
    t = std::clamp(t, 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

// 两层加法混合的 alpha。progress ∈ [0,1]。
struct HighlightAlphas final {
    float ambient = 0.0f;   // 全矩形层
    float spot = 0.0f;      // 径向光斑层
    float radius = 0.0f;    // 光斑半径（像素）
};

inline HighlightAlphas ComputeHighlightAlphas(float progress, float sizeWidth, float sizeHeight) {
    HighlightAlphas out;
    out.radius = std::min(sizeWidth, sizeHeight) * kHighlightRadiusFactor;
    if (progress <= 0.0f) return out;
    out.ambient = kHighlightAmbientAlpha * progress;
    out.spot = kHighlightSpotAlpha * progress;
    return out;
}

// 手指光斑的状态机（弹簧 + 位置）。
class TouchHighlight final {
public:
    TouchHighlight() { configure(); }

    void configure() {
        press_ = Spring::FromStiffness(0.0f, 0.5f, 300.0f);
        press_.setThreshold(0.001f);
        posX_ = Spring::FromStiffness(0.0f, 0.5f, 300.0f);
        posY_ = Spring::FromStiffness(0.0f, 0.5f, 300.0f);
        posX_.setThreshold(kOffsetVisibilityThreshold);
        posY_.setThreshold(kOffsetVisibilityThreshold);
    }

    float pressProgress() const { return press_.value(); }
    float positionX() const { return posX_.value(); }
    float positionY() const { return posY_.value(); }
    // 对应源码 :39 的 `positionAnimation.value - startPosition`
    float offsetX() const { return posX_.value() - startX_; }
    float offsetY() const { return posY_.value() - startY_; }
    bool active() const { return press_.value() > 0.0f; }

    // 按下（源码 :98-103）：记起点 → press→1，位置**瞬移**到起点
    void onDragStart(float x, float y) {
        startX_ = x;
        startY_ = y;
        press_.setTarget(1.0f);
        posX_.snap(x);   // snapTo，不是 animateTo
        posY_.snap(y);
    }

    // 拖动中（源码 :118）：位置**继续瞬移**，不参与弹簧
    void onDrag(float x, float y) {
        posX_.snap(x);
        posY_.snap(y);
    }

    // 松手 / 取消（源码 :105-116）：press→0，位置弹回起点
    void onDragEnd() {
        press_.setTarget(0.0f);
        posX_.setTarget(startX_);
        posY_.setTarget(startY_);
    }

    void tick(float deltaSeconds) {
        press_.tick(deltaSeconds);
        posX_.tick(deltaSeconds);
        posY_.tick(deltaSeconds);
    }

private:
    Spring press_{};
    Spring posX_{};
    Spring posY_{};
    float startX_ = 0.0f;
    float startY_ = 0.0f;
};

}  // namespace aurora
