/*
 * Copyright 2026 默檐
 * SPDX-License-Identifier: Apache-2.0
 *
 * 本项目（LiquidGlass-Cpp）是基于 Kyant 项目的 C++ 独立移植与二次开发。
 * 项目开源地址：[https://github.com/SilentEaves/LiquidGlass-Cpp.git]
 *
 * ==================== 声明 ====================
 * 本项目由 默檐 于 2026 年独立完成 C++ 移植与二次修改。
 * 具体修改包括：将原项目（Kotlin）逻辑重写为 C++。
 * 本项目同样遵守并应用 Apache License, Version 2.0 协议。
 * ====================================================
 *
 * ------------------------------------------------------------------------
 * 第三方开源项目声明：
 * 本文件的核心代码逻辑移植自 AndroidLiquidGlass 项目，来源 Kyant 的液态玻璃项目。
 *
 * 原项目版权：Copyright 2025 Kyant
 * 原项目地址：https://github.com/Kyant0/AndroidLiquidGlass
 * 原项目许可证：Apache License, Version 2.0
 * 原项目许可证链接：http://www.apache.org/licenses/LICENSE-2.0
 * ------------------------------------------------------------------------
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#include "GlassInternal.h"
#include "GlassRecipes.h"

#include <math.h>

namespace lgx {

struct PillButton::State {
    detail::GlowSpring glow;
    bool pressed = false;
};

PillButton::PillButton() : state_(new State()) {}

PillButton::~PillButton() { delete state_; }

PillButton::State& PillButton::state() { return *state_; }

const PillButton::State& PillButton::state() const { return *state_; }

void PillButton::resetGlow() {
    State& s = state();
    s.glow.abort();
    s.pressed = false;
}

bool PillButton::glowVisible() const { return state().glow.visible(); }

float PillButton::glowProgress() const { return state().glow.progress(); }

bool PillButton::busy() const { return state().glow.running(); }

namespace {

// 配方本体已抽到 GlassRecipes.cpp（detail::PillButtonStyle）——
// 抽出去是为了让"参数 → PanelStyle"能在宿主机上被逐字段断言。
PanelStyle buttonStyle(const PillButton& button, float press, float fade) {
    return detail::PillButtonStyle(button, press, fade);
}

}

bool PillButton::draw(const Box& box) {
    State& s = state();
    const float width = box.width();
    const float height = box.height();
    if (width <= 0.0f || height <= 0.0f) return false;

    // ★ Aurora 增量：交互弹簧可热调（默认值与原来的编译期常量一致）
    s.glow.setStrengthSpring(tune.glowDamping, tune.glowStiffness);
    s.glow.setPointerSpring(tune.pointerDamping, tune.pointerStiffness);
    s.glow.step(detail::frameDelta());

    bool clicked = false;
    const PointerProbe probe = detail::probeBox(box, this, interactive);
    if (interactive) {
        const PointerState& pointer = detail::pointerRef();
        if (probe.held) {
            const float localX = pointer.x - box.left;
            const float localY = pointer.y - box.top;
            if (s.pressed) {
                s.glow.move(localX, localY);
            }
            else {
                s.glow.press(localX, localY);
            }
        } else if (s.pressed) {
            s.glow.release();
        }
        if (probe.clicked) {
            clicked = true;
            ++clicks;
        }
    } else if (s.pressed) {
        s.glow.release();
    }
    s.pressed = probe.held;

    const float press = s.glow.progress();
    const float fade = clampRange(alphaScale, 0.0f, 1.0f);
    const float visual = clampRange(visualScale, 0.0f, config::Layout::BUTTON_VISUAL_SCALE_MAX);
    const float rise = px(config::Layout::BUTTON_GLOW_RISE_DP);

    const float safeHeight = height > 1.0f ? height : 1.0f;
    const float scale = lerp(1.0f, 1.0f + rise / safeHeight, press);
    const float maxOffset = width < height ? width : height;
    const float safeOffset = maxOffset > 0.0f ? maxOffset : 1.0f;
    const float offsetX = s.glow.offsetX();
    const float offsetY = s.glow.offsetY();
    const float shiftX = maxOffset
        * tanhf(config::Layout::BUTTON_DRAG_TANH * offsetX / safeOffset);
    const float shiftY = maxOffset
        * tanhf(config::Layout::BUTTON_DRAG_TANH * offsetY / safeOffset);

    const float dragGain = rise / safeHeight;
    const float angle = atan2f(offsetY, offsetX);
    const float longSide = width > height ? width : height;
    const float scaleX = scale + dragGain * fabsf(cosf(angle) * offsetX / longSide)
                       * fminf(width / height, 1.0f);
    const float scaleY = scale + dragGain * fabsf(sinf(angle) * offsetY / longSide)
                       * fminf(height / width, 1.0f);

    const Point center = box.center();
    const float halfWidth = width * 0.5f * scaleX * visual;
    const float halfHeight = height * 0.5f * scaleY * visual;
    const Box drawBox(center.x - halfWidth + shiftX, center.y - halfHeight + shiftY,
                      center.x + halfWidth + shiftX, center.y + halfHeight + shiftY);

    PanelStyle style = buttonStyle(*this, press, fade);
    if (s.glow.visible()) {
        style.glow.on = true;
        style.glow.progress = saturate(press);
        style.glow.x = clampRange(s.glow.pointerX(), 0.0f, width);
        style.glow.y = clampRange(s.glow.pointerY(), 0.0f, height);
    }

    if (glassOn && fade > config::Render::ALPHA_MIN && visual > config::Render::ALPHA_MIN) {
        const int panel = submitPanel(style, drawBox);
        if (panel >= 0) {
            detail::panelCapture(panel);
            detail::panelPaint(panel);
        }
    }

    if (!textOn) return clicked;
    const float textFade = saturate(fade * visual);
    if (textFade <= 0.0f) return clicked;
    const float fontSize = textSizeSp * detail::densityScale() * visual;
    if (fontSize < 1.0f) return clicked;

    unsigned int contentColor = tinted
        ? 0xFFFFFFFFu : detail::tintTextRgb();
    if (textColor != 0u) contentColor = textColor;
    const unsigned int textCol = rgba(textFade, contentColor & 0x00FFFFFFu);

    const char* caption = label != nullptr ? label : "";
    const TextSize span = detail::textMetrics(caption, fontSize);
    const float centerX = drawBox.centerX();
    const float centerY = drawBox.centerY();
    const bool hasCaption = caption[0] != 0;

    if (icon != nullptr) {
        const float iconPx = fontSize * config::Layout::BUTTON_ICON_SCALE;
        const float gap = hasCaption ? fontSize * config::Layout::BUTTON_ICON_GAP : 0.0f;
        const float blockWidth = iconPx + gap + span.width;
        const float iconX = centerX - blockWidth * 0.5f + iconPx * 0.5f;
        detail::paintIcon(icon, Point(iconX, centerY), iconPx, textCol, iconTag);
        if (hasCaption) {
            detail::drawTextAt(Point(iconX + iconPx * 0.5f + gap, centerY - span.height * 0.5f),
                               fontSize, textCol, caption, 0.0f);
        }
    } else if (hasCaption) {
        detail::drawTextAt(Point(centerX - span.width * 0.5f, centerY - span.height * 0.5f),
                           fontSize, textCol, caption, 0.0f);
    }
    return clicked;
}

}
