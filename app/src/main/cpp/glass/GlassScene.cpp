// =============================================================================
//  场景参数实现 —— 默认值必须**逐个等于**原实现里的字面量/常量。
//
//  每一条都标了出处（文件:行 或 常量名）。改动这里的任何默认值都等于改变默认
//  外观，必须先确认那是有意的 —— tests/GlassRecipeTest.cpp 会盯住其中一部分。
// =============================================================================
#include "glass/GlassScene.h"

#include "glass/GlassRegistry.h"
#include "third_party/liquidglass/ytbl/GlassRecipes.h"

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

// -----------------------------------------------------------------------------
// 组件命名（注册表 ID 与分页标题都从这里来，避免两处各写一份字符串）
// -----------------------------------------------------------------------------
const char* GlassScene::ButtonId(int index) {
    switch (index) {
        case 0: return "secondary_window.pill_button_1";
        case 1: return "secondary_window.pill_button_2";
        case 2: return "secondary_window.pill_button_3";
        case 3: return "secondary_window.pill_button_4";
        // ★ index 4 = 「打开液态弹窗」触发按钮：它**不注册进调试器**
        //   （用户要求：弹窗相关不提供专属调试入口），所以没有对应的目标 ID。
        default: return "secondary_window.pill_button_x";
    }
}

const char* GlassScene::ButtonName(int index) {
    switch (index) {
        case 0: return "按钮 1 · 主按钮";
        case 1: return "按钮 2 · 着色";
        case 2: return "按钮 3 · 表面色";
        case 3: return "按钮 4 · 小按钮";
        default: return "按钮";
    }
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

    // ===== 液态玻璃卡片（DrawContents 的 cardStyle + MakeCardPanel）=====
    card.cornerTlDp = 24.0f;            // Corners::rounded(px(24))
    card.cornerTrDp = 24.0f;
    card.cornerBrDp = 24.0f;
    card.cornerBlDp = 24.0f;
    card.colorBoost = true;             // style.optics.colorBoost = true
    card.blurDp = 10.0f;                // s_BlurDp
    card.blurAppliedDp = 10.0f;
    card.lensHeightDp = 12.0f;          // s_LensHeightDp
    card.lensAmountDp = 24.0f;          // s_LensAmountDp
    card.depthAmount = 1.0f;
    card.spectralOn = true;             // s_Chromatic
    card.dispersion = 0.75f;            // s_CardDispersion
    card.edge.mode = 1;                 // MakeChipEdge(1)
    card.edge.widthDp = 1.5f;
    card.edge.softnessDp = 0.75f;
    card.insetOn = true;                // InsetShadow(6, 0, 6, rgba(0.15, black), 1.0)
    card.insetRadiusDp = 6.0f;
    card.insetXDp = 0.0f;
    card.insetYDp = 6.0f;
    card.insetColor[0] = 0.0f;
    card.insetColor[1] = 0.0f;
    card.insetColor[2] = 0.0f;
    card.insetColor[3] = 0.14901961f;   // 0x26 / 255
    card.shadowOn = false;

    // ===== 6 枚胶囊芯片（DrawGlassChip + MakeChipTune / MakeChipEdge）=====
    //  形状行三枚：光学与边光共用 s_ChipTune / s_ChipShapeEdge，圆角各自独立
    //  边光行三枚：光学共用 s_ChipTune，边光各自 gradient / ambient / plain
    for (int i = 0; i < kChipCount; ++i) {
        PanelTune& chip = chips[i];
        chip.colorBoost = true;
        chip.blurDp = 2.0f;             // lgx::px(2.0f)
        chip.blurAppliedDp = 2.0f;
        chip.lensHeightDp = 8.0f;       // lgx::px(8.0f)
        chip.lensAmountDp = 16.0f;      // lgx::px(16.0f)
        chip.spectralOn = false;        // 原来根本不设 spectral ⇒ 0
        chip.dispersion = 0.70f;
        chip.depthAmount = 0.0f;
        chip.edge.widthDp = 1.5f;       // MakeChipEdge 的覆盖值
        chip.edge.softnessDp = 0.75f;
        chip.shadowOn = false;
        chip.insetOn = false;
    }
    chips[0].capsule = true;            // 原 s_ChipShape1Capsule = true
    chips[0].cornerTlDp = chips[0].cornerTrDp = chips[0].cornerBrDp = chips[0].cornerBlDp = 16.0f;
    chips[1].cornerTlDp = chips[1].cornerTrDp = chips[1].cornerBrDp = chips[1].cornerBlDp = 16.0f;
    chips[2].cornerTlDp = 24.0f;        // Corners(px(24), px(8), px(24), px(8))
    chips[2].cornerTrDp = 8.0f;
    chips[2].cornerBrDp = 24.0f;
    chips[2].cornerBlDp = 8.0f;
    for (int i = 3; i < kChipCount; ++i) {
        chips[i].cornerTlDp = chips[i].cornerTrDp = chips[i].cornerBrDp =
            chips[i].cornerBlDp = 16.0f;   // s_ChipEdgeRadiusDp
        chips[i].edge.mode = i - 2;        // 1=Gradient 2=Ambient 3=Plain
    }
    chips[4].edge.alpha = lgx::config::Render::EDGE_AMBIENT_ALPHA;   // 0.38
    for (int i = 0; i < 3; ++i) chips[i].edge.mode = 1;              // s_ChipShapeEdge

    // ===== 玻璃实验场（DrawContents 的 playgroundStyle）=====
    playground.colorBoost = true;
    playground.blurDp = 0.0f;
    playground.blurAppliedDp = 0.0f;
    playground.depthAmount = 1.0f;
    playground.spectralOn = true;
    playground.dispersion = 0.90f;
    playground.edgeOn = true;
    playground.edge.mode = 3;           // EdgeLight::plain()
    playground.edge.widthDp = lgx::config::Render::EDGE_PLAIN_WIDTH;
    playground.edge.softnessDp = lgx::config::Render::EDGE_PLAIN_SOFTNESS;
    playground.edge.alpha = lgx::config::Render::EDGE_PLAIN_ALPHA;
    playground.insetOn = true;          // InsetShadow(6, 0, 6, rgba(0.15, black), 1.0)
    playground.insetRadiusDp = 6.0f;
    playground.insetXDp = 0.0f;
    playground.insetYDp = 6.0f;
    playground.insetColor[3] = 0.14901961f;
    playground.shadowOn = false;

    // ===== 液态按键（原 ApplyPillButtonTune 给五枚共用的一组光学）=====
    //  ★ 现在**每枚一份**：结构上不可能出现"调按钮 1 改了按钮 2"。
    for (int i = 0; i < kButtonCount; ++i) {
        lgx::PillButton& button = buttons[i];
        button.blurDp = lgx::config::Layout::BUTTON_BLUR_DP;                 // 2
        button.lensHeightDp = lgx::config::Layout::BUTTON_LENS_HEIGHT_DP;    // 12
        button.lensAmountDp = lgx::config::Layout::BUTTON_LENS_AMOUNT_DP;    // 24
        button.spectral = 0.0f;
        button.textSizeSp = lgx::config::Layout::BUTTON_TEXT_SP;             // 16
        button.alphaScale = 1.0f;
        button.shadowOn = true;
        button.contourEdge = false;
        button.glassOn = true;
        button.textOn = true;
    }
    buttons[1].tinted = true;           // 第二枚是「着色」
    buttons[2].surfaced = true;         // 第三枚是「表面色」
    dialogTrigger.blurDp = lgx::config::Layout::BUTTON_BLUR_DP;
    dialogTrigger.lensHeightDp = lgx::config::Layout::BUTTON_LENS_HEIGHT_DP;
    dialogTrigger.lensAmountDp = lgx::config::Layout::BUTTON_LENS_AMOUNT_DP;
    dialogTrigger.textSizeSp = lgx::config::Layout::BUTTON_TEXT_SP;
    dialogTrigger.shadowOn = true;

    // ===== 液态开关（三枚各自一份 tune）=====
    for (int i = 0; i < kToggleCount; ++i) {
        toggles[i].accentColor = ArgbFromFloats(stage.toggleAccent[i]);
        toggles[i].trackColor = ArgbFromFloats(stage.toggleTrack);
    }
    toggles[0].on = stage.toggleFirstOn;
    toggles[1].on = stage.toggleSecondOn;
    toggles[2].on = stage.toggleThirdOn;

    // ===== 液态滑块 =====
    for (int i = 0; i < kSliderCount; ++i) {
        sliders[i].accentColor = ArgbFromFloats(stage.sliderAccentDark);
        sliders[i].trackColor = ArgbFromFloats(stage.sliderTrackColor);
    }
    sliders[1].setRange(0.0f, stage.sliderSecondMax);

    // ===== 导航栏 =====
    tabs.accentColor = ArgbFromFloats(stage.tabsAccentDark);
    tabs.containerColor = ArgbFromFloats(stage.tabsContainerDark);
    tabs.iconColor = ArgbFromFloats(stage.tabsIconColor);
    tabs.iconSizeDp = stage.tabsIconSizeDp;
    tabs.labelSizeDp = stage.tabsLabelSizeDp;
    tabs.tabCount = stage.tabsCount;

    // ===== 弹出菜单（PopMenu 的公开字段）=====
    menu.setOpen(false, false);
    menu.cursorRow = 1;
    menu.count = 5;

    // ===== 弹窗 =====
    modal.glassBlurDp = lgx::config::Layout::DIALOG_GLASS_BLUR_DP;
}

GlassScene& Scene() {
    static GlassScene scene;
    return scene;
}

}  // namespace glass
}  // namespace aurora
