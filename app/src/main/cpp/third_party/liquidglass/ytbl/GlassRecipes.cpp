/*
 * Copyright 2026 默檐
 * SPDX-License-Identifier: Apache-2.0
 *
 * ★ Aurora 增量：组件级配方的实现（见 GlassRecipes.h 顶部的理由）。
 *
 * 这里只做"参数 → PanelStyle"的纯计算，不触碰 GL、不读组件内部状态。
 * 所有默认值都从 Tune 的字段默认值来，因此 Tune 不动 ⇒ 结果与原实现一致。
 */
#include "GlassRecipes.h"

#include "GlassInternal.h"

#include <math.h>

namespace lgx {
namespace detail {
namespace {

// float[4]（0..1）→ ytbl 的 ARGB 整数（ytbl 的 DropShadow/InsetShadow 收的是整数色）
unsigned int ToArgbFloats(const float color[4]) {
    const unsigned int a = (unsigned int)(saturate(color[3]) * 255.0f + 0.5f);
    const unsigned int r = (unsigned int)(saturate(color[0]) * 255.0f + 0.5f);
    const unsigned int g = (unsigned int)(saturate(color[1]) * 255.0f + 0.5f);
    const unsigned int b = (unsigned int)(saturate(color[2]) * 255.0f + 0.5f);
    return (a << 24) | (r << 16) | (g << 8) | b;
}

}  // namespace

unsigned int EdgePresetTintForMode(int mode) {
    switch (mode) {
        case 2: return EdgeLight::ambient(config::Render::EDGE_AMBIENT_ALPHA).color;
        case 3: return EdgeLight::plain().color;
        case 4: return EdgeLight::contour().color;
        default: return EdgeLight::gradient().color;
    }
}

void EdgePresetValuesForMode(int mode, float* widthDp, float* softnessDp, float* alpha,
                             float* angleDegrees, float* falloff) {
    float width = 0.5f;
    float softness = 0.25f;
    float alphaValue = 1.0f;
    float angle = 45.0f;
    float fall = 1.0f;
    switch (mode) {
        case 2:
            width = config::Render::EDGE_AMBIENT_WIDTH;
            softness = config::Render::EDGE_AMBIENT_SOFTNESS;
            alphaValue = config::Render::EDGE_AMBIENT_ALPHA;
            break;
        case 3:
            width = config::Render::EDGE_PLAIN_WIDTH;
            softness = config::Render::EDGE_PLAIN_SOFTNESS;
            alphaValue = config::Render::EDGE_PLAIN_ALPHA;
            break;
        case 4:
            width = config::Render::EDGE_CONTOUR_WIDTH;
            softness = config::Render::EDGE_CONTOUR_SOFTNESS;
            alphaValue = config::Render::EDGE_CONTOUR_ALPHA;
            angle = config::Render::EDGE_CONTOUR_ANGLE;
            fall = config::Render::EDGE_CONTOUR_FALLOFF;
            break;
        default: break;
    }
    if (widthDp != nullptr) *widthDp = width;
    if (softnessDp != nullptr) *softnessDp = softness;
    if (alpha != nullptr) *alpha = alphaValue;
    if (angleDegrees != nullptr) *angleDegrees = angle;
    if (falloff != nullptr) *falloff = fall;
}

EdgeLight EdgeLightFromTune(const EdgeTune& tune, float animatedAlpha, bool* enabled) {
    const int mode = tune.mode;
    if (enabled != nullptr) *enabled = mode != 5;
    if (mode == 5) {
        return EdgeLight::plain(0u);
    }
    if (mode == 0) {
        // 跟随组件的按压动画：色/形都按调用方给的语义走（由各组件自己构造），
        // 这里只负责把增益乘上去 —— 增益 1.0 时与原实现逐位相同。
        EdgeLight light = EdgeLight::gradient();
        light.alpha = animatedAlpha * tune.alpha;
        return light;
    }
    const EdgeMode modeValue = mode == 2 ? EdgeMode::Ambient
                             : mode == 3 ? EdgeMode::Plain
                             : mode == 4 ? EdgeMode::Contour
                                         : EdgeMode::Gradient;
    const unsigned int resolved = tune.usePresetTint ? EdgePresetTintForMode(mode)
                                                     : ToArgbFloats(tune.tint);
    return EdgeLight(modeValue, resolved, tune.angleDegrees, tune.falloff, tune.widthDp,
                     tune.softnessDp, tune.alpha);
}

// -----------------------------------------------------------------------------
// Toggle 圆钮
//
// 与原实现（Toggle.cpp draw() 里那段 PanelStyle）逐项对应：
//   colorBoost=false; blur = lerp(floor, glass, 1-press);
//   lensHeight = TOGGLE_LENS_HEIGHT * press; lensAmount = TOGGLE_LENS_AMOUNT * press;
//   depthAmount=0; darkGuard=TOGGLE_DARK_GUARD; spectral=1;
//   backdropScaleX/Y = lerp(min,max,press); backdropBlend=1;
//   surface = tintKnob(), alpha = 1-press; edge = ambient(press);
//   shadowOn=false; insetOn=true (TOGGLE_INNER_DP*max(press, REST_DP), REST_ALPHA+(1-REST_ALPHA)*press)
// -----------------------------------------------------------------------------
PanelStyle ToggleThumbStyle(const ToggleTune& tune, float value, float press,
                            const ToggleTrackVisual& track) {
    (void)value;
    const float p = saturate(press * tune.lensGain);
    PanelStyle style;
    style.corners = Corners::capsule();
    style.optics.colorBoost = false;
    // ★ 端点语义：press=0 用 blurRestDp、press=1 用 blurPressDp。
    //   （原实现写成 lerp(FLOOR, GLASS, 1-p)，等价但读起来容易把两端颠倒 ——
    //     离线回归抓到过一次，所以这里改成最直白的 lerp(rest, press, p)。）
    style.optics.blurPx = px(lerp(tune.blurRestDp, tune.blurPressDp, p));
    style.optics.lensHeightPx = px(tune.lensHeightPressDp) * p;
    style.optics.lensAmountPx = px(tune.lensAmountPressDp) * p;
    style.optics.depthAmount = 0.0f;
    style.optics.darkGuard = tune.darkGuard;
    style.optics.spectral = tune.spectralOn ? 1.0f : 0.0f;
    style.backdropScaleX = lerp(tune.backdropScaleXMin, tune.backdropScaleXMax, p);
    style.backdropScaleY = lerp(tune.backdropScaleYMin, tune.backdropScaleYMax, p);
    style.backdropBlend = tune.backdropBlend;
    const Rgba knob = tintKnob();
    style.surface = Rgba(knob.r, knob.g, knob.b,
                         saturate(saturate(1.0f - p) * tune.surfaceAlphaGain));
    style.surfaceMode = tune.surfaceMode == 2 ? SurfaceMode::Overlay
                      : tune.surfaceMode == 1 ? SurfaceMode::Hue
                                              : SurfaceMode::None;
    // 边光：mode==0 时等价于原来的 EdgeLight::ambient(press)
    if (tune.edge.mode == 0) {
        style.edge = EdgeLight::ambient(p);
        style.edge.alpha *= tune.edge.alpha;
        style.edgeOn = true;
    } else {
        bool on = true;
        style.edge = EdgeLightFromTune(tune.edge, p, &on);
        style.edgeOn = on;
    }
    style.shadowOn = false;
    style.insetOn = true;
    style.inset = InsetShadow(tune.insetRadiusDp
                                  * fmaxf(p, tune.insetRestRadius),
                              saturate(tune.insetRestAlpha
                                       + (1.0f - tune.insetRestAlpha) * p));
    style.trackOn = true;
    style.trackBox = track.box;
    style.trackRadius = track.radius;
    style.trackColor = rgbaOf(track.color);
    return style;
}

// -----------------------------------------------------------------------------
// Slider 圆钮
// -----------------------------------------------------------------------------
PanelStyle SliderThumbStyle(const SliderTune& tune, float press) {
    const float p = saturate(press * tune.lensGain);
    PanelStyle style;
    style.corners = Corners::capsule();
    style.optics.colorBoost = false;
    // ★ 端点语义：press=0 用 blurRestDp、press=1 用 blurPressDp。
    //   （原实现写成 lerp(FLOOR, GLASS, 1-p)，等价但读起来容易把两端颠倒 ——
    //     离线回归抓到过一次，所以这里改成最直白的 lerp(rest, press, p)。）
    style.optics.blurPx = px(lerp(tune.blurRestDp, tune.blurPressDp, p));
    style.optics.lensHeightPx = px(tune.lensHeightPressDp) * p;
    style.optics.lensAmountPx = px(tune.lensAmountPressDp) * p;
    style.optics.depthAmount = tune.depthAmount;
    style.optics.darkGuard = tune.darkGuard;
    style.optics.spectral = tune.spectralOn ? tune.dispersion : 0.0f;
    const Rgba knob = tintKnob();
    style.surface = Rgba(knob.r, knob.g, knob.b,
                         saturate(saturate(1.0f - p) * tune.surfaceAlphaGain));
    style.surfaceMode = tune.surfaceMode == 2 ? SurfaceMode::Overlay
                      : tune.surfaceMode == 1 ? SurfaceMode::Hue
                                              : SurfaceMode::None;
    if (tune.edge.mode == 0) {
        // 与原实现等价：EdgeLight::ambient(1.0)
        style.edge = EdgeLight::ambient(saturate(tune.edgeAlphaGain));
        style.edgeOn = true;
    } else {
        bool on = true;
        style.edge = EdgeLightFromTune(tune.edge, p, &on);
        style.edgeOn = on;
    }
    style.shadowOn = true;
    style.shadow = DropShadow(tune.shadowRadiusDp, 0.0f,
                              tune.shadowRadiusDp * tune.shadowOffsetYRatio,
                              config::Render::SHADOW_TINT_SOFT, tune.shadowAlpha);
    style.insetOn = tune.insetOnPress && p > config::Render::ALPHA_MIN;
    style.inset = InsetShadow(tune.insetRadiusDp * p, p);
    style.backdropOn = tune.backdropOn;
    style.backdropScaleX = lerp(tune.backdropScaleXMin, tune.backdropScaleXMax, p);
    style.backdropScaleY = lerp(tune.backdropScaleYMin, tune.backdropScaleYMax, p);
    style.backdropBlend = tune.backdropBlend;
    return style;
}

SliderTrackVisual SliderTrackLayout(const SliderTune& tune, const Box& box, float progress,
                                    unsigned int neutralArgb, unsigned int accentArgb) {
    SliderTrackVisual out;
    const float width = box.width();
    const float height = box.height();
    const float trackHeight = px(tune.trackHeightDp);
    const float trackTop = box.top + (height - trackHeight) * 0.5f;

    out.rail = Box(box.left, trackTop, box.left + width, trackTop + trackHeight);
    out.railRadius = trackHeight * tune.trackRadiusRatio;
    out.railColor = neutralArgb;

    const float fillWidth = width * saturate(progress);
    out.fillVisible = fillWidth > 0.5f;
    const float fillRadius =
        (fillWidth < trackHeight ? fillWidth : trackHeight) * tune.fillRadiusRatio;
    out.fill = Box(out.rail.left, out.rail.top, out.rail.left + fillWidth, out.rail.bottom);
    out.fillRadius = fillRadius;
    out.fillColor = accentArgb;
    return out;
}

// -----------------------------------------------------------------------------
// PillButton
//
// 与原 buttonStyle() 逐项对应。★ 默认 Tune 走的是**完全相同**的分支：
//   edge.mode == 0 ⇒ contourEdge ? Contour(常量) : Gradient()
//   这一步在下面显式写出来，不依赖任何"恰好相等"的巧合。
// -----------------------------------------------------------------------------
PanelStyle PillButtonStyle(const PillButton& button, float press, float fade) {
    const PillTune& tune = button.tune;
    PanelStyle style;
    style.corners = tune.capsule
        ? Corners::capsule()
        : Corners(px(tune.cornerTlDp), px(tune.cornerTrDp), px(tune.cornerBrDp),
                  px(tune.cornerBlDp));
    style.optics.colorBoost = tune.colorBoost;
    style.optics.blurPx = px(button.blurDp);
    style.optics.lensHeightPx = px(button.lensHeightDp);
    style.optics.lensAmountPx = px(button.lensAmountDp);
    style.optics.depthAmount = tune.depthAmount;
    style.optics.darkGuard = tune.darkGuard;
    style.optics.brightness = tune.brightness;
    style.optics.contrast = tune.contrast;
    style.optics.saturation = tune.saturation;
    style.optics.opacityOn = tune.opacityOn;
    style.optics.opacity = tune.opacity;
    style.optics.spectral =
        button.spectral > config::Render::SPECTRAL_MIN ? button.spectral : 0.0f;

    if (button.contourEdge) {
        const float alpha = saturate(tune.contourAlpha + tune.contourPressGain * press);
        style.edge = EdgeLight::contour(config::Render::EDGE_CONTOUR_ANGLE,
                                        config::Render::EDGE_CONTOUR_FALLOFF,
                                        tintEdgeArgb(config::Render::EDGE_TINT_CONTOUR));
        style.edge.alpha = alpha;
        style.edge.widthDp = tune.contourWidthDp;
        style.edge.softnessDp = tune.contourSoftnessDp;
        if (tune.edge.mode != 0) {
            bool on = true;
            style.edge = EdgeLightFromTune(tune.edge, alpha, &on);
        } else {
            style.edge.alpha = saturate(style.edge.alpha * tune.edge.alpha);
        }
    } else {
        style.edge = EdgeLight::gradient();
        if (tune.edge.mode != 0) {
            bool on = true;
            style.edge = EdgeLightFromTune(tune.edge, press, &on);
        } else {
            style.edge.alpha = saturate(style.edge.alpha * tune.edge.alpha);
        }
    }
    style.edgeOn = tune.edge.mode != 5;
    style.shadowOn = button.shadowOn;
    if (button.shadowOn) {
        style.shadow = DropShadow(tune.shadowRadiusDp, tune.shadowOffsetXDp,
                                  tune.shadowOffsetYDp, ToArgbFloats(tune.shadowTint),
                                  tune.shadowAlpha);
    }
    style.insetOn = tune.insetOn;
    if (tune.insetOn) {
        style.inset = InsetShadow(tune.insetRadiusDp, tune.insetOffsetXDp, tune.insetOffsetYDp,
                                  ToArgbFloats(tune.insetTint), tune.insetAlpha);
    }
    style.alpha = fade;
    style.fallback = Rgba(tune.fallbackR, tune.fallbackG, tune.fallbackB, tune.fallbackA);
    style.backdropOn = tune.backdropOn;
    style.opaqueBackdrop = tune.opaqueBackdrop;
    style.backdropScaleX = tune.backdropScaleX;
    style.backdropScaleY = tune.backdropScaleY;
    style.backdropBlend = tune.backdropBlend;

    if (button.tinted) {
        style.surface = Rgba(button.tint.r, button.tint.g, button.tint.b,
                             button.tint.a * fade);
        style.surfaceMode = SurfaceMode::Hue;
    } else if (button.surfaced) {
        style.surface = Rgba(button.surface.r, button.surface.g, button.surface.b,
                             button.surface.a * fade);
        style.surfaceMode = SurfaceMode::Overlay;
    } else {
        const Rgba sheet = tintButtonSheet();
        style.surface = Rgba(sheet.r, sheet.g, sheet.b, sheet.a * fade);
        style.surfaceMode = SurfaceMode::Overlay;
    }
    if (tune.surfaceModeOverride == 0) style.surfaceMode = SurfaceMode::None;
    else if (tune.surfaceModeOverride == 1) style.surfaceMode = SurfaceMode::Hue;
    else if (tune.surfaceModeOverride == 2) style.surfaceMode = SurfaceMode::Overlay;
    return style;
}

// -----------------------------------------------------------------------------
// TabStrip（底部导航）
// -----------------------------------------------------------------------------
PanelStyle TabNavigationBackgroundStyle(const TabStripTune& tune, unsigned int containerArgb,
                                        float haloLevel, float haloX, float haloY,
                                        float panelWidth, float panelHeight) {
    PanelStyle style;
    style.corners = Corners::capsule();
    style.optics.colorBoost = tune.colorBoost;
    style.optics.blurPx = px(tune.blurDp);
    style.optics.lensHeightPx = px(tune.lensHeightDp);
    style.optics.lensAmountPx = px(tune.lensAmountDp);
    style.optics.depthAmount = tune.depthAmount;
    style.surface = rgbaOf(containerArgb);
    style.surfaceMode = SurfaceMode::Overlay;
    style.edge = EdgeLight::gradient();
    style.edgeOn = true;
    style.shadowOn = tune.shadowOn;
    if (tune.shadowOn) {
        style.shadow = DropShadow(tune.shadowRadiusDp, 0.0f, tune.shadowOffsetYDp,
                                  ToArgbFloats(tune.shadowTint), tune.shadowAlpha);
    }
    if (haloLevel > config::Render::ALPHA_MIN) {
        style.glow.on = true;
        style.glow.progress = haloLevel;
        style.glow.x = clampRange(haloX, 0.0f, panelWidth);
        style.glow.y = clampRange(haloY, 0.0f, panelHeight);
    }
    return style;
}

PanelStyle TabNavigationIndicatorStyle(const TabStripTune& tune, float press, bool light) {
    const float idleAlpha = tune.idleAlpha * (1.0f - press);
    const float pressAlpha = tune.pressAlpha * press;
    const float baseAlpha = idleAlpha * (1.0f - pressAlpha);
    const float totalAlpha = pressAlpha + baseAlpha;
    const float luma = light ? 0.0f
        : (totalAlpha > config::Motion::TAB_ALPHA_EPSILON ? baseAlpha / totalAlpha : 0.0f);

    PanelStyle style;
    style.corners = Corners::capsule();
    style.optics.colorBoost = false;
    style.optics.blurPx = 0.0f;
    style.optics.lensHeightPx = px(tune.pressLensHeightDp) * press;
    style.optics.lensAmountPx = px(tune.pressLensAmountDp) * press;
    style.optics.depthAmount = 0.0f;
    style.optics.spectral = tune.spectral;
    style.surface = Rgba(luma, luma, luma, totalAlpha);
    style.surfaceMode = SurfaceMode::Overlay;
    style.edge = EdgeLight::gradient();
    style.edge.color = config::Render::EDGE_TINT;
    style.edge.alpha = press;
    style.edgeOn = press > config::Render::ALPHA_MIN;
    style.shadowOn = true;
    style.shadow = DropShadow(tune.indicatorShadowDp, 0.0f, tune.indicatorShadowDp / 6.0f,
                              rgba(tune.indicatorShadowAlpha * press, 0x000000u), 1.0f);
    style.insetOn = press > config::Render::ALPHA_MIN;
    style.inset = InsetShadow(px(tune.pressInnerDp) * press, press);
    return style;
}

}  // namespace detail
}  // namespace lgx
