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

struct Toggle::State {
    detail::TrackMotion motion;
    bool touched = false;
    bool dragged = false;
    bool seeded = false;
    bool observed = false;
    float pressX = 0.0f;
    float lastX = 0.0f;

    State()
        : motion(0.0f, 0.0f, 1.0f, config::Motion::DRAG_VISIBLE, 1.0f,
                 config::Motion::DRAG_SCALE) {}
};

Toggle::Toggle() : state_(new State()) {}

Toggle::~Toggle() { delete state_; }

Toggle::State& Toggle::state() { return *state_; }

const Toggle::State& Toggle::state() const { return *state_; }

void Toggle::setOn(bool value, bool withMotion) {
    State& s = state();
    on = value;
    s.observed = value;
    s.seeded = true;
    if (withMotion) {
        s.motion.animateTo(value ? 1.0f : 0.0f);
    }
    else {
        s.motion.snapValue(value ? 1.0f : 0.0f);
    }
}

bool Toggle::busy() const { return state().motion.moving(); }

bool Toggle::draw(const Box& box) {
    State& s = state();
    // ★ Aurora 增量：弹簧可热调（默认 = 原编译期常量，改它不影响默认外观）
    s.motion.setValueSpring(tune.valueDamping, tune.valueStiffness);
    s.motion.step(detail::frameDelta());

    if (!s.seeded) {
        s.seeded = true;
        s.observed = on;
        s.motion.snapValue(on ? 1.0f : 0.0f);
    } else if (on != s.observed && !s.touched) {
        s.observed = on;
        s.motion.animateTo(on ? 1.0f : 0.0f);
    }

    bool flipped = false;
    // ★ Aurora 增量：尺寸从 Tune 读（默认 = config::Layout::TOGGLE_*）
    const float trackWidth = px(tune.trackWidthDp);
    const float trackHeight = px(tune.trackHeightDp);
    const float thumbWidth = px(tune.thumbWidthDp);
    const float thumbHeight = px(tune.thumbHeightDp);
    s.motion.setPressedScale(tune.pressedScale);
    const float trackLeft = box.left;
    const float trackTop = box.top + (box.height() - trackHeight) * 0.5f;
    const float thumbTop = box.top + (box.height() - thumbHeight) * 0.5f;

    const PointerProbe probe = detail::probeBox(box, this, true);
    const PointerState& pointer = detail::pointerRef();

    if (probe.activated) {
        s.pressX = pointer.x;
        s.lastX = pointer.x;
        s.touched = true;
        s.dragged = false;
        s.motion.press();
    }
    if (s.touched && (probe.held || pointer.down)) {
        const float now = pointer.x;
        const float step = now - s.lastX;
        s.lastX = now;
        if (fabsf(now - s.pressX) > px(tune.dragSlopDp)) s.dragged = true;
        const float travel = step / px(tune.dragScaleDp);
        s.motion.updateValue(clampRange(s.motion.targetValue() + travel, 0.0f, 1.0f));
    }
    if (s.touched && !probe.held && !pointer.down) {
        s.touched = false;
        const bool next = s.dragged ? (s.motion.targetValue() >= 0.5f) : !on;
        if (next != on) {
            on = next;
            flipped = true;
        }
        s.observed = on;
        s.motion.updateValue(on ? 1.0f : 0.0f);
        s.dragged = false;
        s.motion.release();
    }

    const float value = s.motion.value();
    const float press = s.motion.pressProgress();
    const unsigned int accent = (accentColor == config::Layout::TOGGLE_ACCENT)
        ? detail::tintMixRgb(config::Layout::TOGGLE_ACCENT,
                              config::Layout::TOGGLE_ACCENT_LIGHT)
        : accentColor;
    const unsigned int neutral = (trackColor == config::Layout::TOGGLE_TRACK)
        ? detail::tintTrackArgb() : trackColor;
    const unsigned int blended = mixRgb(neutral, accent, saturate(value));

    const float padding = px(tune.paddingDp);
    const float travel = lerp(padding, padding + px(tune.travelDp), value);
    const float velocity = s.motion.speed() * tune.velocityGain;
    const float clamp = config::Motion::DRAG_STRETCH_CLAMP;
    const float scaleX = s.motion.scaleX()
        / (1.0f - clampRange(velocity * config::Motion::DRAG_STRETCH_X, -clamp, clamp));
    const float scaleY = s.motion.scaleY()
        * (1.0f - clampRange(velocity * config::Motion::DRAG_STRETCH_Y, -clamp, clamp));
    const float width = thumbWidth * scaleX;
    const float height = thumbHeight * scaleY;
    const float thumbLeft = trackLeft + travel - (width - thumbWidth) * 0.5f;
    const float thumbY = thumbTop + (thumbHeight - height) * 0.5f;
    const Box thumb(thumbLeft, thumbY, thumbLeft + width, thumbY + height);

    // ★ 配方抽到 GlassRecipes.cpp（PanelStyle 的构造在那里被离线回归逐字段验证）
    detail::ToggleTrackVisual track;
    track.box = Box(trackLeft, trackTop, trackLeft + trackWidth, trackTop + trackHeight);
    track.radius = trackHeight * tune.trackRadiusRatio;
    track.color = blended;
    PanelStyle style = detail::ToggleThumbStyle(tune, value, press, track);

    const int panel = submitPanel(style, thumb);
    if (panel >= 0) detail::panelCapture(panel);
    detail::fillRound(style.trackBox, style.trackRadius, blended);
    if (panel >= 0) detail::panelPaint(panel);
    return flipped;
}

}
