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

struct Slider::State {
    detail::TrackMotion motion;
    bool dragging = false;
    bool dragged = false;
    bool seeded = false;
    float observed = 0.5f;
    float pressX = 0.0f;
    float lastX = 0.0f;

    State()
        : motion(0.5f, 0.0f, 1.0f, config::Motion::DRAG_VISIBLE, 1.0f,
                 config::Motion::DRAG_SCALE) {}
};

Slider::Slider() : state_(new State()) {}

Slider::~Slider() { delete state_; }

Slider::State& Slider::state() { return *state_; }

const Slider::State& Slider::state() const { return *state_; }

void Slider::setRange(float from, float to) {
    State& s = state();
    const float oldSpan = maxValue - minValue;
    const float oldProgress = oldSpan != 0.0f
        ? saturate((s.motion.value() - minValue) / oldSpan) : 0.0f;
    const bool remapped = from != minValue || to != maxValue;
    minValue = from;
    maxValue = to;
    s.motion.setRange(from, to);
    value = clampRange(value, from, to);
    if (remapped) {
        const float mapped = from + (to - from) * oldProgress;
        value = mapped;
        s.motion.snapValue(mapped);
    }
}

float Slider::animatedValue() const { return state().motion.value(); }

float Slider::displayValue() const {
    return minValue + (maxValue - minValue) * saturate(state().motion.progress());
}

bool Slider::busy() const { return state().motion.moving(); }

bool Slider::draw(const Box& box) {
    State& s = state();
    // ★ Aurora 增量：弹簧/按压缩放可热调（默认 = 原编译期常量）
    s.motion.setValueSpring(tune.valueDamping, tune.valueStiffness);
    s.motion.step(detail::frameDelta());

    if (!s.seeded) {
        s.seeded = true;
        s.observed = value;
        s.motion.snapValue(value);
    } else if (!s.dragging && value != s.observed) {
        s.observed = value;
        s.motion.animateTo(value);
    }

    bool remapped = false;
    const float width = box.width();
    const float height = box.height();
    if (width <= 0.0f || height <= 0.0f) return false;

    // ★ Aurora 增量：尺寸从 Tune 读（默认 = config::Layout::SLIDER_*）
    //   轨道的高度/圆角由下面的 SliderTrackLayout 统一算（那是唯一的轨道几何来源）
    const float thumbWidth = px(tune.thumbWidthDp);
    const float thumbHeight = px(tune.thumbHeightDp);
    s.motion.setPressedScale(tune.pressedScale);
    const float thumbTop = box.top + (height - thumbHeight) * 0.5f;

    const PointerProbe probe = detail::probeBox(box, this, true);
    const PointerState& pointer = detail::pointerRef();

    if (probe.activated) {
        const float startProgress = saturate(s.motion.progress());
        const float rawStart = -thumbWidth * 0.5f + width * startProgress;
        const float thumbLeft = clampRange(rawStart, -thumbWidth * tune.thumbEdgeMin,
                                           width - thumbWidth * tune.thumbEdgeMax);
        s.dragging = true;
        s.dragged = false;
        s.lastX = pointer.x;
        s.motion.press();
        const bool outside = pointer.x < box.left + thumbLeft
                          || pointer.x > box.left + thumbLeft + thumbWidth
                          || pointer.y < thumbTop
                          || pointer.y > thumbTop + thumbHeight;
        if (outside) {
            const float span = width > 1.0f ? width : 1.0f;
            const float progress = clampRange((pointer.x - box.left) / span, 0.0f, 1.0f);
            value = clampRange(minValue + (maxValue - minValue) * progress,
                               minValue, maxValue);
            s.motion.updateValue(value);
            remapped = true;
        }
    }
    if (s.dragging && (probe.held || pointer.down)) {
        const float step = pointer.x - s.lastX;
        s.lastX = pointer.x;
        if (step != 0.0f) {
            s.dragged = true;
            remapped = true;
        }
        const float span = width > 1.0f ? width : 1.0f;
        const float next = s.motion.targetValue()
                         + (maxValue - minValue) * (step / span);
        value = clampRange(next, minValue, maxValue);
        s.observed = value;
        s.motion.updateValue(value);
    }
    if (s.dragging && !pointer.down) {
        s.dragging = false;
        s.motion.release();
    }

    const float progress = saturate(s.motion.progress());
    const float press = s.motion.pressProgress();
    const unsigned int accent = (accentColor == config::Layout::SLIDER_ACCENT)
        ? detail::tintMixRgb(config::Layout::SLIDER_ACCENT,
                              config::Layout::SLIDER_ACCENT_LIGHT)
        : accentColor;
    const unsigned int neutral = (trackColor == config::Layout::SLIDER_TRACK)
        ? detail::tintTrackArgb() : trackColor;

    const float raw = -thumbWidth * 0.5f + width * progress;
    const float thumbShift = clampRange(raw, -thumbWidth * tune.thumbEdgeMin,
                                        width - thumbWidth * tune.thumbEdgeMax);
    const float velocity = s.motion.speed() * tune.velocityGain;
    const float clamp = config::Motion::DRAG_STRETCH_CLAMP;
    const float scaleX = s.motion.scaleX()
        / (1.0f - clampRange(velocity * config::Motion::DRAG_STRETCH_X, -clamp, clamp));
    const float scaleY = s.motion.scaleY()
        * (1.0f - clampRange(velocity * config::Motion::DRAG_STRETCH_Y, -clamp, clamp));
    const float drawWidth = thumbWidth * scaleX;
    const float drawHeight = thumbHeight * scaleY;
    const float thumbLeft = box.left + thumbShift - (drawWidth - thumbWidth) * 0.5f;
    const float thumbY = thumbTop + (thumbHeight - drawHeight) * 0.5f;
    const Box thumb(thumbLeft, thumbY, thumbLeft + drawWidth, thumbY + drawHeight);

    // 轨道几何抽成纯函数（离线回归可逐字段验证）：默认值与原来写死的完全一致
    const detail::SliderTrackVisual track =
        detail::SliderTrackLayout(tune, box, progress, neutral, accent);
    detail::fillRound(track.rail, track.railRadius, track.railColor);
    if (track.fillVisible) {
        detail::fillRound(track.fill, track.fillRadius, track.fillColor);
    }

    PanelStyle style = detail::SliderThumbStyle(tune, press);

    const int panel = submitPanel(style, thumb);
    if (panel >= 0) {
        detail::panelCapture(panel);
        detail::panelPaint(panel);
    }
    return remapped;
}

}
