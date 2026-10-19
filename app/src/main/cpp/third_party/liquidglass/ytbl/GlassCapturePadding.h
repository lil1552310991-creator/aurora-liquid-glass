#pragma once

// ===========================================================================
//  采样区留白的**纯逻辑**（零 GL / 零 ImGui / 零 Android 依赖）
//
//  为什么单独抽出来：留白决定"每块玻璃每帧要拷贝 / 合成 / 模糊多大一块"，
//  是本项目最大的单项 GPU 成本。它又最容易写错 —— 写小了边缘的折射/模糊会
//  取到面板外的空白，写大了整块采样区成倍变大。抽成纯函数后可以在 PC 上
//  用断言把"为什么这么小仍然够用"固定下来（tests/CapturePaddingTest.cpp）。
//
//  三件需要向外取样的事，逐项算：
//
//   ① 模糊（gather）：可分离核每向 12 抽头、步长 0.25σ ⇒ 触达 3σ；
//      σ = blurRadius / √3 ⇒ 3σ ≈ 1.73×blurRadius。取 2× 留余量。
//      降采样时每个抽头还做 2×2 盒式预滤波，最多再向外半像素。
//
//   ② 折射 / 色散：`sampleCoord = vLocal + lensShift·lensDir`，
//      `lensShift = lensCurve(lensT)·(−lensAmount)` **恒为负**，
//      `lensDir` 是圆角矩形 SDF 的外法线 ⇒ 位移**永远朝面板内部**。
//      所以普通面板（非液滴、非背景拉伸）**不需要**为折射留白。
//      旧实现无条件把 `lensAmount + lensAmount×spectral` 加进来，
//      对 760×1500 的窗口等于把采样面积多放大 ~40%（见测试里的数值）。
//
//   ③ 背景拉伸：`sceneLayer` 用 `moved = center + (pixel − center)/scale`
//      取样，scale<1 会把采样点推到面板外 ⇒ 只有这类面板按
//      "半对角线 × (1/scale − 1)" 留白。
//
//   ④ 液滴融合（`liquid`）：合并场的法线在颈部可能沿面板边缘指向外侧，
//      保守地保留整份折射留白。
// ===========================================================================

#include <cmath>

namespace lgx {
namespace detail {

struct CapturePadSpec {
    float basePx = 3.0f;
    float maxPx = 640.0f;
    // 模糊半径（已乘 uniformScale、已按上限夹紧后传入）
    float blurPx = 0.0f;
    // gather 可用时取 CAPTURE_PAD_BLUR_GAIN，否则取 SAFE（更大的保守值）
    float blurGain = 2.0f;
    // 折射量（已乘 uniformScale）
    float lensAmountPx = 0.0f;
    float spectral = 0.0f;
    // 液滴融合（合并场）或"恢复旧行为"的 A/B 开关
    bool liquid = false;
    bool legacyLensPad = false;
    // 背景拉伸
    float backdropScaleX = 1.0f;
    float backdropScaleY = 1.0f;
    float backdropBlend = 0.0f;
    float boxWidth = 0.0f;
    float boxHeight = 0.0f;
};

inline float ClampRange(float value, float low, float high) {
    if (!(value == value)) return low;   // NaN 安全
    return value < low ? low : (value > high ? high : value);
}

// 模糊核在面板外需要覆盖的像素数。σ = r/√3，核触达 3σ，外加降采样盒式的半像素。
inline float BlurReachPx(float blurPx) {
    return 3.0f * (blurPx * 0.57735f) + 1.0f;
}

inline float CapturePaddingPx(const CapturePadSpec& spec) {
    const float blur = ClampRange(spec.blurPx, 0.0f, spec.maxPx);
    float lateral = 0.0f;
    if (spec.legacyLensPad || spec.liquid) {
        const float amount = ClampRange(spec.lensAmountPx, 0.0f, spec.maxPx);
        const float spectral = ClampRange(spec.spectral, 0.0f, 2.0f);
        lateral += amount + (spectral > 0.0f ? amount * spectral : 0.0f);
    }
    const float minScale = spec.backdropScaleX < spec.backdropScaleY
                               ? spec.backdropScaleX : spec.backdropScaleY;
    if (spec.backdropBlend > 0.0f && minScale > 0.001f && minScale < 1.0f) {
        const float halfDiagonal =
            0.5f * std::sqrt(spec.boxWidth * spec.boxWidth + spec.boxHeight * spec.boxHeight);
        lateral += halfDiagonal * (1.0f / minScale - 1.0f);
    }
    return ClampRange(spec.basePx + blur * spec.blurGain + lateral,
                      spec.basePx, spec.maxPx);
}

}  // namespace detail
}  // namespace lgx
