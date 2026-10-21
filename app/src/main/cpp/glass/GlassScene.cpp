// =============================================================================
//  场景参数实现 —— 默认值必须**逐个等于**原实现里的字面量/常量。
//
//  每一条都标了出处（文件:行 或 常量名）。改动这里的任何默认值都等于改变默认
//  外观，必须先确认那是有意的 —— tests/GlassRecipeTest.cpp 会盯住其中一部分。
// =============================================================================
#include "../AuroraLog.h"
#include "glass/GlassScene.h"
#if defined(__ANDROID__)
#include <android/log.h>
#endif

#include "glass/GlassRegistry.h"
#if defined(__ANDROID__)
#include <android/log.h>
#endif
#include "third_party/liquidglass/ytbl/GlassRecipes.h"
#if defined(__ANDROID__)
#include <android/log.h>
#endif

#include <cmath>
#include <cstdio>

namespace aurora {
namespace glass {
namespace {

unsigned int ByteOf(float value) {
    const float clamped = value < 0.0f ? 0.0f : (value > 1.0f ? 1.0f : value);
    return static_cast<unsigned int>(clamped * 255.0f + 0.5f);
}

unsigned int ArgbFromFloats(const float color[4]) {
    return (ByteOf(color[3]) << 24) | (ByteOf(color[0]) << 16) | (ByteOf(color[1]) << 8) |
           ByteOf(color[2]);
}

Rgba RgbaFromFloats(const float color[4]) {
    return Rgba(color[0], color[1], color[2], color[3]);
}

lgx::EdgeMode EdgeModeOf(int mode) {
    switch (mode) {
        case 2: return lgx::EdgeMode::Ambient;
        case 3: return lgx::EdgeMode::Plain;
        case 4: return lgx::EdgeMode::Contour;
        default: return lgx::EdgeMode::Gradient;
    }
}

lgx::SurfaceMode SurfaceModeOf(int mode) {
    switch (mode) {
        case 1: return lgx::SurfaceMode::Hue;
        case 2: return lgx::SurfaceMode::Overlay;
        default: return lgx::SurfaceMode::None;
    }
}

}  // namespace

// -----------------------------------------------------------------------------
// EdgeTune → lgx::EdgeLight（唯一转换点，卡片的 6 枚芯片与 4 个面板都走它）
// -----------------------------------------------------------------------------
lgx::EdgeLight EdgeLightFromSceneTune(const EdgeTune& tune) {
    const int mode = tune.mode == 0 ? 1 : tune.mode;   // 0 在这里等同 Gradient
    const unsigned int tint = tune.usePresetTint
        ? lgx::detail::EdgePresetTintForMode(mode)
        : ArgbFromFloats(tune.tint);
    return EdgeLight(EdgeModeOf(mode), tint, tune.angleDegrees, tune.falloff, tune.widthDp,
                     tune.softnessDp, tune.alpha);
}

// ★ 关于 blurPx：这里读 tune.blurDp（编辑值），不再读 tune.blurAppliedDp。
//
//   原本的设计是"UI 改 blurDp、渲染读 blurAppliedDp，由 Registry 的 Deferred
//   机制在 Commit() 时同步"。但主题菜单（secondary_imgui）里的滑块是直接写
//   &scene.card.blurDp 的 —— 它不经过 Registry，于是 blurAppliedDp 永远停在
//   初始化时的 10.0，拖动滑块完全没有反应（用户实测）。
//
//   改读编辑值后两条路径都正确：
//     · 调试面板：Registry 写 blurDp，这里当帧就读到新值（比原来更快一帧）；
//     · 主题菜单：滑块写 blurDp，这里同样当帧读到。
//   glRunGather 内部对"模糊半径没变"有跳过判断，所以不会因此每帧重建模糊纹理。
PanelStyle BuildPanelStyle(const PanelTune& tune) {
#if defined(__ANDROID__)
    // 参数追踪探针（默认关闭，AURORA_PARAM_TRACE=1 开启）。
    // 排查"控件调了没反应"时用它确认值有没有流到样式构建这一层；
    // 常态开启会刷满 logcat 并拖慢帧率，所以默认不跑。
    static const bool paramTrace = [] {
        const char* value = std::getenv("AURORA_PARAM_TRACE");
        return value != nullptr && value[0] == '1';
    }();
    if (paramTrace) {
        static float lastBlur = -999.0f;
        static float lastLensH = -999.0f;
        static float lastLensA = -999.0f;
        static float lastCorner = -999.0f;
        if (tune.blurDp != lastBlur || tune.lensHeightDp != lastLensH ||
            tune.lensAmountDp != lastLensA || tune.cornerTlDp != lastCorner) {
            lastBlur = tune.blurDp;
            lastLensH = tune.lensHeightDp;
            lastLensA = tune.lensAmountDp;
            lastCorner = tune.cornerTlDp;
            AURORA_LOG(ANDROID_LOG_INFO, "AuroraParam",
                                "TUNE blur=%.3f lensH=%.3f lensA=%.3f cornerTl=%.3f spectral=%.3f",
                                static_cast<double>(tune.blurDp),
                                static_cast<double>(tune.lensHeightDp),
                                static_cast<double>(tune.lensAmountDp),
                                static_cast<double>(tune.cornerTlDp),
                                static_cast<double>(tune.spectralOn ? tune.dispersion : 0.0f));
        }
    }
#endif
    PanelStyle style;
    style.corners = tune.corners();
    style.optics.colorBoost = tune.colorBoost;
    style.optics.blurPx = lgx::px(tune.blurDp);          // 直接读编辑值（见下方说明）
    style.optics.lensHeightPx = lgx::px(tune.lensHeightDp);
    style.optics.lensAmountPx = lgx::px(tune.lensAmountDp);
    style.optics.depthAmount = tune.depthAmount;
    style.optics.darkGuard = tune.darkGuard;
    style.optics.spectral = tune.spectralOn ? tune.dispersion : 0.0f;
    style.optics.brightness = tune.brightness;
    style.optics.contrast = tune.contrast;
    style.optics.saturation = tune.saturation;
    style.optics.opacityOn = tune.opacityOn;
    style.optics.opacity = tune.opacity;
    style.alpha = tune.alpha;
    if (tune.surfaceOn) {
        style.surface = RgbaFromFloats(tune.surface);
        style.surfaceMode = SurfaceModeOf(tune.surfaceMode);
    } else {
        style.surfaceMode = lgx::SurfaceMode::None;
    }
    style.edgeOn = tune.edgeOn && tune.edge.mode != 5;
    style.edge = EdgeLightFromSceneTune(tune.edge);
    style.shadowOn = tune.shadowOn;
    if (tune.shadowOn) {
        style.shadow = lgx::DropShadow(tune.shadowRadiusDp, tune.shadowXDp, tune.shadowYDp,
                                       ArgbFromFloats(tune.shadowColor), tune.shadowAlpha);
    }
    style.insetOn = tune.insetOn;
    if (tune.insetOn) {
        style.inset = lgx::InsetShadow(tune.insetRadiusDp, tune.insetXDp, tune.insetYDp,
                                       ArgbFromFloats(tune.insetColor), tune.insetAlpha);
    }
    style.backdropOn = tune.backdropOn;
    style.opaqueBackdrop = tune.opaqueBackdrop;
    style.backdropScaleX = tune.backdropScaleX;
    style.backdropScaleY = tune.backdropScaleY;
    style.backdropBlend = tune.backdropBlend;
    style.fallback = RgbaFromFloats(tune.fallback);
    return style;
}

PanelStyle BuildWindowStyle(const WindowTune& tune) {
#if defined(__ANDROID__)
    // 整窗口参数追踪（同一开关 AURORA_PARAM_TRACE）。
    {
        static const bool windowTrace = [] {
            const char* value = std::getenv("AURORA_PARAM_TRACE");
            return value != nullptr && value[0] == '1';
        }();
        if (windowTrace) {
            static float lastCorner = -999.0f;
            static float lastBlur = -999.0f;
            static float lastLensH = -999.0f;
            if (tune.cornerDp != lastCorner || tune.blurDp != lastBlur ||
                tune.lensHeightDp != lastLensH) {
                lastCorner = tune.cornerDp;
                lastBlur = tune.blurDp;
                lastLensH = tune.lensHeightDp;
                AURORA_LOG(ANDROID_LOG_INFO, "AuroraParam",
                                    "WINDOW corner=%.3f blur=%.3f lensH=%.3f glass=%d",
                                    static_cast<double>(tune.cornerDp),
                                    static_cast<double>(tune.blurDp),
                                    static_cast<double>(tune.lensHeightDp),
                                    tune.glassEnabled ? 1 : 0);
            }
        }
    }
#endif
    PanelStyle style;
    style.corners = Corners::rounded(lgx::px(tune.cornerDp));
    style.optics.colorBoost = tune.colorBoost;
    style.optics.blurPx = lgx::px(tune.blurDp);          // 直接读编辑值（见下方说明）
    style.optics.lensHeightPx = lgx::px(tune.lensHeightDp);
    style.optics.lensAmountPx = lgx::px(tune.lensAmountDp);
    style.optics.depthAmount = tune.depthOn ? 1.0f : 0.0f;
    style.optics.spectral = tune.chromaticOn ? tune.dispersion : 0.0f;
    style.optics.brightness = tune.brightness;
    style.optics.contrast = tune.contrast;
    style.optics.saturation = tune.saturation;
    // 表面色的 alpha 由「透明度」决定：开关关闭时取 0（= 最透明）
    const float surfaceAlpha = tune.opacityOn ? tune.opacity : 0.0f;
    const bool light = lgx::glassIsLight();
    const float* surface = light ? tune.surfaceLight : tune.surfaceDark;
    style.surface = Rgba(surface[0], surface[1], surface[2], surfaceAlpha);
    style.surfaceMode = SurfaceModeOf(tune.surfaceMode);
    style.edgeOn = tune.edgeOn && tune.edge.mode != 5;
    style.edge = EdgeLightFromSceneTune(tune.edge);
    style.shadowOn = tune.shadowOn;
    if (tune.shadowOn) {
        style.shadow = lgx::DropShadow(tune.shadowRadiusDp, tune.shadowOffsetXDp,
                                       tune.shadowOffsetYDp, 0xFF000000u, tune.shadowAlpha);
    }
    if (tune.insetAlpha > 0.0f) {
        style.insetOn = true;
        style.inset = lgx::InsetShadow(tune.insetRadiusDp, tune.insetAlpha);
    }
    style.backdropOn = tune.backdropOn;
    style.fallback = RgbaFromFloats(tune.fallback);
    style.alpha = tune.alpha;
    return style;
}


const char* SecondaryTargetId(const char* suffix) {
    // 注意：返回的是静态缓冲区，只在"注册时立刻使用"的场景下安全（本项目的用法）
    static char buffer[8][160];
    static int slot = 0;
    slot = (slot + 1) % 8;
    std::snprintf(buffer[slot], sizeof(buffer[slot]), "secondary_window.%s", suffix);
    return buffer[slot];
}

// -----------------------------------------------------------------------------
// 场景构造：所有默认值在这里逐条对齐原实现
// -----------------------------------------------------------------------------
GlassScene::GlassScene() {
    // ===== 整窗口（secondary_imgui.cpp Draw() 里那段 windowStyle）=====
    window.glassEnabled = true;
    window.cornerDp = 28.0f;            // kWindowCornerDp（secondary_imgui.cpp:42）
    window.blurDp = 10.0f;              // s_WindowBlurDp
    window.blurAppliedDp = 10.0f;
    window.lensHeightDp = 18.0f;        // s_WindowLensHeightDp
    window.lensAmountDp = 30.0f;        // s_WindowLensAmountDp
    window.dispersion = 0.70f;          // s_WindowDispersion
    window.chromaticOn = true;          // s_WindowChromatic
    window.depthOn = true;              // s_WindowDepth
    window.colorBoost = true;
    window.opacityOn = false;           // s_WindowOpacityEnabled
    window.opacity = 0.28f;             // s_WindowOpacity
    window.surfaceLight[0] = 0.96f;     // 原：glassIsLight() ? (0.96,0.97,0.99) : ...
    window.surfaceLight[1] = 0.97f;
    window.surfaceLight[2] = 0.99f;
    window.surfaceDark[0] = 0.10f;      // 原：(0.10,0.11,0.14)
    window.surfaceDark[1] = 0.11f;
    window.surfaceDark[2] = 0.14f;
    window.surfaceMode = 2;             // SurfaceMode::Overlay
    window.edgeOn = true;
    window.edge.mode = 1;               // EdgeLight::gradient()
    window.shadowOn = true;
    window.shadowRadiusDp = 24.0f;      // DropShadow(24.0f, …)
    window.shadowOffsetYDp = 24.0f / lgx::config::Render::SHADOW_OFFSET_DIVISOR;  // = 4
    window.shadowAlpha = 0.14117647f;   // 36/255（原 (255*0.14+0.5) 写进 tint 的 alpha 字节）
    window.backdropOn = true;

    // ===== 液态玻璃卡片 / 芯片 / 实验场 / 按钮 / 开关 / 滑块 / 导航 / 菜单 =====
    // ★ 这些组件的默认参数已随组件删除（见 GlassScene 结构的说明）。

    // ---- 组件演示参数已随组件一起删除 ----
    //
    //   这里原本依次初始化：6 枚胶囊芯片（含 3 条条纹配色）、玻璃实验场、
    //   5 枚 PillButton 的光学与尺寸、3 枚 Toggle、2 根 Slider、导航栏 TabStrip、
    //   弹出菜单 PopMenu。它们都属于「c++ 液态玻璃展示源码」的移植，已按用户要求
    //   删除 —— 保留这段说明是为了让后来者知道这些字段**曾经存在过**，
    //   而不是以为漏写了初始化。

}

GlassScene& Scene() {
    static GlassScene scene;
    return scene;
}

}  // namespace glass
}  // namespace aurora
