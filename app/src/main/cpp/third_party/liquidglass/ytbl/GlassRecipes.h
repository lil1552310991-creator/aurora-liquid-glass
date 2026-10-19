/*
 * Copyright 2026 默檐
 * SPDX-License-Identifier: Apache-2.0
 *
 * ------------------------------------------------------------------------
 * ★ Aurora 增量：把各组件的"玻璃配方"从 draw() 里抽成**可独立调用的函数**。
 *
 * 为什么必须抽出来（不是为了好看）：
 *   调试工作台要能证明"改了参数 → 渲染输入真的变了"。如果配方还埋在
 *   Toggle::draw() 里，验证就只能靠真机截图比对，无法进离线回归。
 *   抽成纯函数之后，"Tune → PanelStyle"这一段可以在 PC 上逐字段断言，
 *   而 "PanelStyle → PanelJob → shader uniform" 那一段本来就有回归覆盖。
 *
 * 约束：这些函数必须**只依赖传入的参数**，不读任何组件内部状态、不做 GL 调用。
 * ------------------------------------------------------------------------
 */
#pragma once

#include "GlassUI.h"

namespace lgx {
namespace detail {

// 边光预设色（随主题自适应）——mode: 1=Gradient 2=Ambient 3=Plain 4=Contour
unsigned int EdgePresetTintForMode(int mode);
// 预设数值（宽度/柔度/alpha/角度/衰减）
void EdgePresetValuesForMode(int mode, float* widthDp, float* softnessDp, float* alpha,
                             float* angleDegrees, float* falloff);
// 把 EdgeTune 变成 ytbl 的 EdgeLight。
//   animatedAlpha 只在 tune.mode == 0 时使用（"跟随组件的按压动画"），
//   此时 tune.alpha 作为**增益**乘上去。
EdgeLight EdgeLightFromTune(const EdgeTune& tune, float animatedAlpha, bool* enabled);

// ---- Toggle：圆钮（玻璃层）-------------------------------------------------
//   value/press 来自组件动画；track 相关量由调用方算好传进来（轨道是直接填充的，
//   不是面板，所以它的几何与颜色单独用一个结构表达）。
struct ToggleTrackVisual {
    Box box;
    float radius = 0.0f;
    unsigned int color = 0;
};

PanelStyle ToggleThumbStyle(const ToggleTune& tune, float value, float press,
                            const ToggleTrackVisual& track);

// ---- Slider：圆钮 + 轨道 ---------------------------------------------------
PanelStyle SliderThumbStyle(const SliderTune& tune, float press);

struct SliderTrackVisual {
    Box rail;
    float railRadius = 0.0f;
    unsigned int railColor = 0;
    Box fill;
    float fillRadius = 0.0f;
    unsigned int fillColor = 0;
    bool fillVisible = false;
};

SliderTrackVisual SliderTrackLayout(const SliderTune& tune, const Box& box, float progress,
                                    unsigned int neutralArgb, unsigned int accentArgb);

// ---- PillButton ------------------------------------------------------------
PanelStyle PillButtonStyle(const PillButton& button, float press, float fade);

// ---- TabStrip（底部导航）---------------------------------------------------
//   background 层：容器面板（blur/lens/表面/边光/阴影 + 高光斑）
PanelStyle TabNavigationBackgroundStyle(const TabStripTune& tune, unsigned int containerArgb,
                                        float haloLevel, float haloX, float haloY,
                                        float panelWidth, float panelHeight);
//   indicator 层：选中指示器
PanelStyle TabNavigationIndicatorStyle(const TabStripTune& tune, float press, bool light);

}  // namespace detail
}  // namespace lgx
