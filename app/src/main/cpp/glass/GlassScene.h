// =============================================================================
//  Liquid Glass Inspector —— 场景参数（二级窗口里所有玻璃对象的"真实存储"）
//
//  为什么要有这一层：
//   ① 调试工作台要绑定到**渲染实现真正读的那个字段**。如果这些参数继续以
//      static 变量的形式埋在 secondary_imgui.cpp（3000+ 行、依赖 ImGui）里，
//      参数绑定层就没法在宿主机上编译，也就没法做"改了参数 → 渲染输入真的变了"
//      的离线回归。
//   ② 每个组件必须有**自己的一份**参数。原来 5 枚 PillButton 共用一组 static
//      变量（s_ButtonBlurDp 等），"调按钮 1 会改到按钮 2"是结构性的，不是 bug。
//      改成每实例一份之后，这个问题从结构上消失。
//
//  本文件**不依赖 ImGui / Android / GL**，只用 ytbl 的公开类型 ⇒ 可进离线回归。
// =============================================================================
#pragma once

#include "GlassRegistry.h"

#include "third_party/liquidglass/ytbl/GlassUI.h"

namespace aurora {
namespace glass {

using lgx::Box;
using lgx::Corners;
using lgx::EdgeLight;
using lgx::EdgeTune;
using lgx::PillTune;
using lgx::SliderTune;
using lgx::TabStripTune;
using lgx::ToggleTune;
using lgx::PanelStyle;
using lgx::Rgba;

// -----------------------------------------------------------------------------
// 通用面板配方（卡片 / 6 枚芯片 / 实验场）
//
//  与 secondary_imgui.cpp 里原来的 PanelTune 相比有两处刻意的改动：
//    · 去掉了 kTune* 分块掩码 —— 每个目标有**自己完整的一份**，
//      不再需要"这次只应用形状不应用光学"这种共享变量时代的补丁。
//    · 光学四项（blur/lens/lensAmount/dispersion）并进来，
//      原来它们单独放在 s_BlurDp 之类的变量里、和这里重复。
//  默认值全部来自原实现里那些字面量，逐个写在 GlassScene.cpp 的构造里。
// -----------------------------------------------------------------------------
struct PanelTune {
    bool capsule = false;
    float cornerTlDp = 24.0f;
    float cornerTrDp = 24.0f;
    float cornerBrDp = 24.0f;
    float cornerBlDp = 24.0f;

    bool colorBoost = true;
    float blurDp = 0.0f;
    // ★ 帧边界生效的"生效值"：渲染端读的是它，调试 UI 改的是 blurDp。
    //   为什么模糊单独走这条路：它会改变 gather pass 的采样步长/目标尺寸，
    //   是这套渲染里唯一真正"贵"的参数。高频拖动时只把最新值应用一次。
    float blurAppliedDp = 0.0f;
    float lensHeightDp = 0.0f;
    float lensAmountDp = 0.0f;
    float depthAmount = 0.0f;
    float darkGuard = 0.0f;
    bool spectralOn = false;
    float dispersion = 0.70f;

    float brightness = 0.0f;
    float contrast = 1.0f;
    float saturation = 1.0f;
    bool opacityOn = false;
    float opacity = 1.0f;
    float alpha = 1.0f;

    bool surfaceOn = false;
    float surface[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    int surfaceMode = 0;   // 0=None 1=Hue 2=Overlay

    bool edgeOn = true;
    EdgeTune edge;         // edge.mode 默认 1 = Gradient（见 GlassScene.cpp 的初始化）

    bool shadowOn = false;
    float shadowRadiusDp = 24.0f;
    float shadowXDp = 0.0f;
    float shadowYDp = 4.0f;
    float shadowColor[4] = {0.0f, 0.0f, 0.0f, 0.1f};
    float shadowAlpha = 1.0f;

    bool insetOn = false;
    float insetRadiusDp = 24.0f;
    float insetXDp = 0.0f;
    float insetYDp = 24.0f;
    float insetColor[4] = {0.0f, 0.0f, 0.0f, 0.14901961f};
    float insetAlpha = 1.0f;

    bool backdropOn = true;
    bool opaqueBackdrop = false;
    float backdropScaleX = 1.0f;
    float backdropScaleY = 1.0f;
    float backdropBlend = 0.0f;

    float fallback[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    // ★ true = 兜底色跟随主题（窗口/卡片取 ImGui 的页面底色，芯片取
    //   frostedFallbackColor()）。此时下面那个颜色参数由渲染端每帧覆盖，
    //   工作台会把它标成"跟随主题中"并禁用编辑 —— 不做会被每帧冲掉的假控件。
    bool fallbackFollowTheme = true;

    Corners corners() const {
        if (capsule) return Corners::capsule();
        return Corners(lgx::px(cornerTlDp), lgx::px(cornerTrDp), lgx::px(cornerBrDp),
                       lgx::px(cornerBlDp));
    }
};

// 把 PanelTune 写进 PanelStyle。★ 这是**唯一**的转换点：
// 只要它是对的，就不可能存在"滑块动了但渲染没变"。
PanelStyle BuildPanelStyle(const PanelTune& tune);

// EdgeTune → lgx::EdgeLight（唯一转换点）
lgx::EdgeLight EdgeLightFromSceneTune(const EdgeTune& tune);

// -----------------------------------------------------------------------------
// 整窗口（二级窗口的外壳玻璃）
// -----------------------------------------------------------------------------
struct WindowTune {
    bool glassEnabled = true;

    // 几何
    float cornerDp = 28.0f;         // 原 secondary_imgui.cpp 的 kWindowCornerDp

    // 折射 / 模糊
    float blurDp = 10.0f;
    float blurAppliedDp = 10.0f;    // 帧边界生效值（见 PanelTune::blurAppliedDp 的说明）
    float lensHeightDp = 18.0f;
    float lensAmountDp = 30.0f;
    float dispersion = 0.70f;
    bool chromaticOn = true;
    bool depthOn = true;

    // 色彩
    bool colorBoost = true;
    float brightness = 0.0f;
    float contrast = 1.0f;
    float saturation = 1.0f;

    // 透明度（原实现在开关关闭时取 0 = 最透明）
    bool opacityOn = false;
    float opacity = 0.28f;

    // 表面（浅色/深色两套，按主题自动切换 —— 与原实现一致）
    float surfaceLight[4] = {0.96f, 0.97f, 0.99f, 0.0f};
    float surfaceDark[4] = {0.10f, 0.11f, 0.14f, 0.0f};
    int surfaceMode = 2;            // Overlay
    float fallback[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    bool fallbackFollowTheme = true;   // 见 PanelTune 的说明（原实现取 ImGui 页面底色）

    // 边光
    bool edgeOn = true;
    EdgeTune edge;

    // 外阴影 / 内阴影
    bool shadowOn = true;
    float shadowRadiusDp = 24.0f;
    float shadowOffsetXDp = 0.0f;
    float shadowOffsetYDp = 4.0f;
    float shadowAlpha = 0.14f;      // 原实现 255*0.14 写进 tint 的 alpha 字节
    float insetAlpha = 0.0f;        // 常态不开（缩放激活反馈才加）
    float insetRadiusDp = 24.0f;

    // 背景采样
    bool backdropOn = true;

    // 面板整体 alpha
    float alpha = 1.0f;
};

PanelStyle BuildWindowStyle(const WindowTune& tune);

// -----------------------------------------------------------------------------
// 交互尺寸（按钮 / 滑块 / 导航栏的外框）—— 这些是 layout，不是玻璃配方
// -----------------------------------------------------------------------------
struct StageTune {
    // 5 枚 PillButton 的尺寸（索引见 GlassScene::kButtonCount）：
    //   0 液态玻璃按钮(160) / 1 着色(140) / 2 表面色(140) / 3 +(48) / 4 打开液态弹窗(180)
    //   ★ 第 5 枚（"打开液态弹窗"）是**用户明确要求保留**的 Demo 组件（见本轮任务书 §三.A）：
    //     它曾被我上一轮误判为"擅自新增"而删除 —— 那是错的，用户要它回来。
    //     它与卡片区那个 `dialogTrigger` 是两个独立入口（用户看到的"以前版本"就是这样）。
    //   ★ 它**不注册进调试器**（用户要求：弹窗相关不提供专属调试），见 GlassTargets.cpp。
    float buttonWidthDp[5] = {160.0f, 140.0f, 140.0f, 48.0f, 180.0f};
    float buttonHeightDp[5] = {48.0f, 48.0f, 48.0f, 48.0f, 48.0f};
    bool buttonSurfaceFollowTheme = true;
    float buttonSurfaceColor[4] = {1.0f, 1.0f, 1.0f, 0.30f};
    float buttonTintColor[4] = {0.0f, 0.478f, 1.0f, 1.0f};

    float sliderWidthFraction[2] = {1.0f, 1.0f};
    float sliderHeightDp[2] = {24.0f, 24.0f};
    float sliderScale[2] = {1.0f, 1.0f};
    float sliderSecondMax = 100.0f;
    float sliderAccentDark[4] = {0.0f, 0.5686f, 1.0f, 1.0f};
    float sliderAccentLight[4] = {0.0f, 0.5333f, 1.0f, 1.0f};
    float sliderTrackColor[4] = {0.4706f, 0.4706f, 0.5020f, 0.3608f};  // 0x5C787880
    bool toggleFirstOn = false;
    bool toggleSecondOn = false;
    bool toggleThirdOn = true;
    float toggleScale[3] = {1.0f, 1.0f, 1.0f};
    float toggleAccent[3][4] = {
        {0.1882f, 0.8196f, 0.3451f, 1.0f},
        {1.0f, 0.2627f, 0.2627f, 1.0f},
        {0.1882f, 0.8196f, 0.3451f, 1.0f}};
    float toggleTrack[4] = {0.4706f, 0.4706f, 0.5020f, 0.3608f};   // 0x5C787880

    bool tabsVisible = true;
    float tabsHeightDp = 64.0f;
    float tabsWidthFraction = 1.0f;
    float tabsScale = 1.0f;
    float tabsIconSizeDp = lgx::config::Layout::TAB_ICON_SIZE_DP;
    float tabsLabelSizeDp = lgx::config::Layout::TAB_LABEL_SIZE_DP;
    int tabsCount = 4;
    int tabsActiveIndex = 1;        // 1 基（面板显示用），组件内部 0 基
    float tabsAccentDark[4] = {0.0f, 0.5686f, 1.0f, 1.0f};
    float tabsAccentLight[4] = {0.0f, 0.5333f, 1.0f, 1.0f};
    float tabsContainerDark[4] = {0.0706f, 0.0706f, 0.0706f, 0.4f};
    float tabsContainerLight[4] = {0.9804f, 0.9804f, 0.9804f, 0.4f};
    float tabsIconColor[4] = {0.0f, 0.0f, 0.0f, 0.0f};   // 0 = 由 ytbl 决定

    float chipHeightDp = 44.0f;
    float chipGapDp = 16.0f;
    float chipEdgeRadiusDp = 16.0f;

    float playgroundRadius = 0.5f;
    float playgroundBlurDp = 0.0f;
    float playgroundHeight = 0.2f;
    float playgroundAmount = 0.2f;
    float playgroundSizeDp = 256.0f;
    float playgroundOffsetX = 0.0f;
    float playgroundOffsetY = 0.0f;
    bool playgroundSheet = true;
    float playgroundDepthAmount = 1.0f;

    int menuReach = 0;              // lgx::MenuReach::Auto
    float menuBandColor[4] = {1.0f, 1.0f, 1.0f, 0.23921569f};   // MENU_BAND = 0x3DFFFFFF

    float menuCapWidthDp = 180.0f;
    float menuCapHeightDp = 44.0f;
    float dialogButtonWidthDp = 180.0f;
    float dialogButtonHeightDp = 48.0f;
    // ★★ 恢复"上一轮调试器开发之前"的默认值：**false = 从中心玻璃球展开**
    //    （`Modal::open()`）。证据分两层，请一起看，不要只看其中一层：
    //    ① 移植版历史（= 用户实际看到的样子）：调试器开发前能拿到的**全部**二进制
    //       （10-13 14:54 before_realtime / 15:39 before_high_rate / 20:55 before_power /
    //        21:48 before_rotation / 22:28 build/recon/aurora_overlay，sha256 62377f44…）
    //       里，符号 `secondary_imgui::(anonymous namespace)::s_DialogAnimationFromButton`
    //       都落在 **.bss**（`readelf -sW` 显示 section 24 = NOBITS）⇒ 零初始化 ⇒ false；
    //       同一二进制里 `s_DialogGlassBlurDp/…WidthDp/…HeightDp` 都在 .data（有初值）。
    //    ② 上游 Demo 源码（`…\jni\secondary_imgui.cpp:38`）写的是
    //       `static bool s_DialogAnimationFromButton = true;` ⇒ **与移植版相反**。
    //       也就是说：移植时这一行就被搬成了无初值版本，**不是调试器那一轮改的**；
    //       而调试器那一轮把它显式写成了 true（= 上游字面值），于是用户看到的默认动画
    //       从"中心长出"变成了"从按钮位置展开"（位移与缩放幅度大得多）——
    //       这正是用户反馈的"弹跳幅度被明显放大"。
    //    ★ 用户的要求是"恢复上一轮调试器开发之前的动画效果" ⇒ 取移植版历史值 false。
    //    ★ 复选框（弹窗动画一/二）本身是原版就有的，不能删：
    //      docs/LIQUID_GLASS_DEMO_UI_INVENTORY.md:287 第 27 项。
    //    ★ 弹窗的**数值参数**（刚度/阻尼/时长/缩放）一个都没被改过：工作区
    //      ytbl/GlassConfig.h、Dialog.cpp 与上游源码 **SHA-256 逐字节相同**。
    bool dialogAnimationFromButton = false;

    // 内容可滚动区的行距等由 ImGui 样式决定，不在这里
};

// -----------------------------------------------------------------------------
// 场景：参数 + 组件实例
//
//  ★ 组件实例也搬到这里，是为了让"参数绑定层"能在宿主机上编译并做离线回归
//    （原来它们是 secondary_imgui.cpp 里的 static，只有 ImGui 那一侧看得见）。
// -----------------------------------------------------------------------------
struct GlassScene {
    // 5 = 按钮行原样（液态玻璃按钮 / 着色 / 表面色 / + / 打开液态弹窗）。
    //   ★ 第 5 枚是**弹窗触发按钮**，用户要求恢复（本轮任务书 §三.A）。
    //   ★ 卡片区那个"打开液态弹窗"是**独立成员** `dialogTrigger`，不在这个计数里。
    static constexpr int kButtonCount = 5;
    static constexpr int kToggleCount = 3;
    static constexpr int kSliderCount = 2;
    static constexpr int kChipCount = 6;

    WindowTune window;
    StageTune stage;
    PanelTune card;
    PanelTune chips[kChipCount];
    PanelTune playground;

    // ---- 组件实例（每个都有自己的 tune ⇒ 参数天然互不影响）----
    lgx::PillButton buttons[kButtonCount];
    lgx::PillButton dialogTrigger;
    lgx::Toggle toggles[kToggleCount];
    lgx::Slider sliders[kSliderCount];
    lgx::TabStrip tabs;
    lgx::PopMenu menu;
    lgx::Modal modal;

    // 组件名（供注册表与分页标题使用）
    static const char* ButtonId(int index);
    static const char* ButtonName(int index);

    GlassScene();
};

GlassScene& Scene();

// 目标 ID 的统一构造：secondary_window / secondary_window.pill_button_1 ...
const char* SecondaryTargetId(const char* suffix);

// 把 PanelTune 的字段登记成"调试参数"（供各目标的注册复用，避免复制粘贴）
class TargetBuilder;
void BindPanelTuneParams(TargetBuilder& builder, PanelTune& tune, const char* sourceFile);
void BindEdgeTuneParams(TargetBuilder& builder, EdgeTune& tune, const char* sourceFile);

}  // namespace glass
}  // namespace aurora
