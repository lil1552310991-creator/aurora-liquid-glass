#pragma once

#include <cmath>   // std::fabs

// ===========================================================================
//  输入法弹层避让（adjustPan 语义）—— 纯逻辑，零 ImGui / 零 Android 依赖
// ===========================================================================
//  为什么需要它：我们的覆盖层是 SurfaceFlinger 里 `mRoot` 的兄弟层，z 序在
//  WMS 整棵子树（**含系统输入法弹层**）之上。所以键盘弹出来之后：
//    · 视觉上：我们的玻璃画在键盘之上，键盘被半透明玻璃压住/发灰；
//    · 触摸上：落在我们触摸拦截区里的点会先被我们接走 ⇒ 键盘按键点不动。
//  触摸那一半由 Renderer 把键盘矩形从拦截区里**挖掉**解决；
//  视觉这一半就是这里：把与键盘重叠的窗口整体**上移**，键盘让出来。
//
//  纯逻辑放在这里是为了能离线断言"什么时候移、移多少、顶边不许跑出屏幕、
//  键盘收起后必须回到原位"——这些用手点屏幕验不出边界情况。
// ===========================================================================

namespace aurora {

// 键盘不可见时传 imeTop <= 0（或 > 屏幕高）⇒ 位移恒为 0。
inline bool ImeVisibleAt(float imeTop, float screenHeight) {
    return imeTop > 0.0f && (screenHeight <= 0.0f || imeTop < screenHeight);
}

/**
 * 算出"当前应该施加多少上移"。
 *
 *   windowTopWithShift  : 窗口**当前**实际位置（已经含上一次施加的位移）
 *   windowHeight        : 窗口高度
 *   appliedShift        : 上一次施加的位移（>0 表示已经上移了这么多）
 *   imeTop              : 键盘顶边（<=0 表示键盘不可见）
 *   screenTop           : 屏幕可用区顶边（一般 0；刘海/安全区可以传正数）
 *
 * 返回新的位移（>=0）。**必须**用"当前实际位置 + 已施加位移"先还原出窗口的
 * 逻辑位置，否则每帧都会在上一次的结果上再叠一次位移 —— 窗口会一路飞出屏幕。
 */
inline float ComputeImeAvoidShift(float windowTopWithShift, float windowHeight,
                                  float appliedShift, float imeTop, float screenTop) {
    if (!ImeVisibleAt(imeTop, 0.0f)) return 0.0f;
    if (!(windowHeight > 0.0f)) return 0.0f;
    // 非有限值一律当作"没有位移"：宿主拿到的可能是还没填过的默认值 / 坏数据，
    // 让它变成 NaN 传进 SetNextWindowPos 会把整个窗口布局搞坏（比不避让严重得多）。
    if (!(windowTopWithShift == windowTopWithShift) || std::fabs(windowTopWithShift) > 1.0e7f) {
        return 0.0f;
    }
    if (!(appliedShift == appliedShift) || std::fabs(appliedShift) > 1.0e7f) appliedShift = 0.0f;
    if (!(screenTop == screenTop)) screenTop = 0.0f;
    const float baseTop = windowTopWithShift + appliedShift;   // 还原逻辑位置
    const float baseBottom = baseTop + windowHeight;
    if (!(baseBottom == baseBottom)) return 0.0f;
    if (baseBottom <= imeTop) return 0.0f;                     // 本来就不压键盘
    float want = baseBottom - imeTop;
    const float maxShift = baseTop - screenTop;                // 顶边不许跑出屏幕
    if (want > maxShift) want = maxShift > 0.0f ? maxShift : 0.0f;
    if (!(want == want) || want < 0.0f) want = 0.0f;
    return want;
}

/** 施加位移后，本帧应该 SetNextWindowPos 的 y。 */
inline float ImeAvoidWindowY(float windowTopWithShift, float appliedShift, float wantShift) {
    return (windowTopWithShift + appliedShift) - wantShift;
}

}  // namespace aurora
