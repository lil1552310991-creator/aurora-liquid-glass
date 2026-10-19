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
#include "GlassContext.h"
#include "GlassCapturePadding.h"
#include "PanelEfficiencyStats.h"

#include <math.h>
#include <cmath>
#include <cstdlib>
#include <cstring>

namespace lgx {
namespace detail {
namespace {

PanelJob g_jobs[config::Render::PANEL_JOB_LIMIT];
int g_jobCount = 0;

int g_panelJob[config::Render::PANEL_JOB_LIMIT] = {};
int g_panelShade[config::Render::PANEL_JOB_LIMIT] = {};
int g_panelCount = 0;

// ★ Aurora 增量：宿主设置的"内容可视区"圆角遮罩（lgx::setContentMask）。
//   提交面板时快照进 job ⇒ 延迟绘制时不受后续改动影响。
static float g_contentMaskRect[4] = {0.0f, 0.0f, 0.0f, 0.0f};
static float g_contentMaskRadius = -1.0f;
static float g_contentMaskViewportH = 0.0f;


unsigned int g_backdropTexture = 0;
int g_backdropWidth = 0;
int g_backdropHeight = 0;

bool panelCullingEnabled() {
    static const bool enabled = [] {
        const char* value = std::getenv("AURORA_PANEL_CULL");
        return value == nullptr || value[0] != '0' || value[1] != '\0';
    }();
    return enabled;
}

bool jobIntersectsClip(const PanelJob& job, const Box* clip) {
    if (!panelCullingEnabled()) return true;
    // The shader draws exactly job.frame, including any animated stretch. The
    // separate shadow job already carries its own expanded/offset frame.
    // Round the frame outwards to keep even a partially covered edge pixel.
    if (!std::isfinite(job.frame.left) || !std::isfinite(job.frame.top) ||
        !std::isfinite(job.frame.right) || !std::isfinite(job.frame.bottom) ||
        !std::isfinite(job.viewW) || !std::isfinite(job.viewH)) return true;
    const float frameLeft = floorf(job.frame.left);
    const float frameTop = floorf(job.frame.top);
    const float frameRight = ceilf(job.frame.right);
    const float frameBottom = ceilf(job.frame.bottom);
    float left = 0.0f;
    float top = 0.0f;
    float right = floorf(job.viewW);
    float bottom = floorf(job.viewH);
    if (clip != nullptr) {
        const float scaleX = pixelScaleXOf();
        const float scaleY = pixelScaleYOf();
        if (!std::isfinite(clip->left) || !std::isfinite(clip->top) ||
            !std::isfinite(clip->right) || !std::isfinite(clip->bottom) ||
            !std::isfinite(scaleX) || !std::isfinite(scaleY)) return true;
        // Match Context.cpp::applyClipScissor's outward rounding and viewport
        // clamp instead of using the unscaled UI clip rectangle.
        left = fmaxf(left, floorf(clip->left * scaleX));
        top = fmaxf(top, floorf(clip->top * scaleY));
        right = fminf(right, ceilf(clip->right * scaleX));
        bottom = fminf(bottom, ceilf(clip->bottom * scaleY));
    }
    return right > left && bottom > top && frameRight > left &&
           frameBottom > top && frameLeft < right && frameTop < bottom;
}

float alphaOf(unsigned int color) {
    return (float)((color >> 24) & 0xFFu) / config::COLOR_CHANNEL;
}

float channelOf(unsigned int color, int shift) {
    return (float)((color >> shift) & 0xFFu) / config::COLOR_CHANNEL;
}

bool backdropReady() {
    return backdropEnabledFlag() && g_backdropTexture != 0 && g_backdropWidth > 0
        && g_backdropHeight > 0;
}

template <typename T>
void snapshotScalar(std::vector<std::uint8_t>& bytes, const T& value) {
    const auto offset = bytes.size();
    bytes.resize(offset + sizeof(T));
    std::memcpy(bytes.data() + offset, &value, sizeof(T));
}

void snapshotColor(std::vector<std::uint8_t>& bytes, const Rgba& color) {
    snapshotScalar(bytes, color.r);
    snapshotScalar(bytes, color.g);
    snapshotScalar(bytes, color.b);
    snapshotScalar(bytes, color.a);
}

void snapshotJob(std::vector<std::uint8_t>& bytes, const PanelJob& job) {
    // Explicit fields avoid struct padding and omit callback-written outputs.
    snapshotScalar(bytes, job.frame.left);
    snapshotScalar(bytes, job.frame.top);
    snapshotScalar(bytes, job.frame.right);
    snapshotScalar(bytes, job.frame.bottom);
    snapshotScalar(bytes, job.viewW);
    snapshotScalar(bytes, job.viewH);
    snapshotScalar(bytes, job.pixelScaleX);
    snapshotScalar(bytes, job.pixelScaleY);
    snapshotScalar(bytes, job.sampleOriginX);
    snapshotScalar(bytes, job.sampleOriginY);
    snapshotScalar(bytes, job.sampleSizeX);
    snapshotScalar(bytes, job.sampleSizeY);
    snapshotScalar(bytes, job.sampleStrideX);
    snapshotScalar(bytes, job.sampleStrideY);
    snapshotScalar(bytes, job.corners.topLeft);
    snapshotScalar(bytes, job.corners.topRight);
    snapshotScalar(bytes, job.corners.bottomRight);
    snapshotScalar(bytes, job.corners.bottomLeft);
    for (float value : job.dropRect) snapshotScalar(bytes, value);
    snapshotScalar(bytes, job.dropRound);
    for (float value : job.feedRect) snapshotScalar(bytes, value);
    snapshotScalar(bytes, job.feedRound);
    snapshotScalar(bytes, job.feedMerge);
    for (float value : job.strandRect) snapshotScalar(bytes, value);
    snapshotScalar(bytes, job.strandRound);
    snapshotScalar(bytes, job.strandMerge);
    snapshotScalar(bytes, job.lensHeight);
    snapshotScalar(bytes, job.lensAmount);
    snapshotScalar(bytes, job.lensDepth);
    snapshotScalar(bytes, job.darkGuard);
    snapshotScalar(bytes, job.spectral);
    snapshotScalar(bytes, job.blurRadius);
    snapshotScalar(bytes, job.panelOpacity);
    for (float value : job.toneCtrl) snapshotScalar(bytes, value);
    snapshotScalar(bytes, job.toneBoost);
    snapshotColor(bytes, job.refractTint);
    snapshotColor(bytes, job.surface);
    snapshotScalar(bytes, job.surfaceMode);
    snapshotScalar(bytes, job.edgeMode);
    snapshotColor(bytes, job.edgeTint);
    snapshotScalar(bytes, job.edgeTintAlpha);
    snapshotScalar(bytes, job.edgeAngle);
    snapshotScalar(bytes, job.edgeFalloff);
    snapshotScalar(bytes, job.edgeAlpha);
    snapshotScalar(bytes, job.edgeWidth);
    snapshotScalar(bytes, job.edgeBlur);
    snapshotColor(bytes, job.insetTint);
    snapshotScalar(bytes, job.insetShiftX);
    snapshotScalar(bytes, job.insetShiftY);
    snapshotScalar(bytes, job.insetReach);
    snapshotScalar(bytes, job.insetAlpha);
    snapshotScalar(bytes, job.glowOn);
    snapshotScalar(bytes, job.glowX);
    snapshotScalar(bytes, job.glowY);
    snapshotScalar(bytes, job.glowReach);
    snapshotScalar(bytes, job.glowLevel);
    snapshotScalar(bytes, job.sceneOn);
    snapshotScalar(bytes, job.solidBackdrop);
    snapshotScalar(bytes, job.stretchX);
    snapshotScalar(bytes, job.stretchY);
    snapshotScalar(bytes, job.stretchMix);
    snapshotColor(bytes, job.absentTint);
    snapshotScalar(bytes, job.trackOn);
    for (float value : job.trackRect) snapshotScalar(bytes, value);
    snapshotScalar(bytes, job.trackRadius);
    snapshotColor(bytes, job.trackTint);
    snapshotScalar(bytes, job.shadeMode);
    snapshotColor(bytes, job.shadeTint);
    snapshotScalar(bytes, job.shadeShiftX);
    snapshotScalar(bytes, job.shadeShiftY);
    snapshotScalar(bytes, job.shadeReach);
    snapshotScalar(bytes, job.shadeBodyW);
    snapshotScalar(bytes, job.shadeBodyH);
    snapshotScalar(bytes, job.shadeIndex);
}

float edgeModeValue(const EdgeLight& edge, bool enabled) {
    if (!enabled || edge.widthDp <= 0.0f) return 0.0f;
    if (edge.mode == EdgeMode::Plain) return 3.0f;
    if (edge.mode == EdgeMode::Ambient) return 2.0f;
    if (edge.mode == EdgeMode::Contour) return 4.0f;
    return 1.0f;
}

float blurCeiling() { return px(config::Render::BLUR_RADIUS_MAX_DP); }

// 采样区留白（像素）。留白决定"每块玻璃每帧要拷贝/合成/模糊多大一块"，是最大的
// 单项 GPU 成本，所以这里逐项算，不再一律把折射量也算进去。
//
// 三个真实需要向外取样的来源：
//   ① 模糊：可分离核每向 12 抽头 × 0.25σ 步长 ⇒ 触达约 3σ = 1.73×blur，
//      取 2×blur 留余量（CAPTURE_PAD_BLUR_GAIN）。
//   ② 折射/色散：`sampleCoord = vLocal + lensShift·lensDir`，其中
//      `lensShift = lensCurve(lensT)·(−lensAmount)` **恒为负**，而 `lensDir`
//      是 SDF 的外法线 ⇒ 位移**永远朝面板内部**。面板内像素因此不需要任何
//      额外留白（旧实现无条件把 lensAmount + lensAmount×spectral 加进去，
//      对 760×1500 的窗口等于多拷贝 ~1.4 倍面积）。
//   ③ 背景拉伸（`backdropScaleX/Y < 1` + `backdropBlend > 0`）：`sceneLayer`
//      用 `moved = center + (pixel − center) / scale` 取样，scale<1 会把采样
//      点推到面板外 ⇒ 只有这种面板才需要按 "半对角线 × (1/scale − 1)" 留白。
//   ④ 液滴融合（`style.liquid`）：合并场的法线在颈部可能沿面板边缘指向外侧，
//      保持原实现的整份折射留白（这部分面板很少，按原样最稳）。
//
// 环境变量 `AURORA_CAPTURE_PAD_LENS=1` 恢复旧的"无条件加折射留白"，用于真机
// A/B 对照（默认关闭）。
// 采样区留白的全部数学在 GlassCapturePadding.h（纯逻辑，进宿主回归）。
// 这里只负责把 style / box / 缩放折算成它的输入。
bool legacyLensPadding() {
    static const bool value = [] {
        const char* raw = std::getenv("AURORA_CAPTURE_PAD_LENS");
        return raw != nullptr && raw[0] == '1';
    }();
    return value;
}

float capturePadding(const PanelStyle& style, const Box& box, float uniformScale) {
    const Optics& optics = style.optics;
    const float blurCap = blurCeiling();
    CapturePadSpec spec;
    spec.basePx = config::Render::CAPTURE_PAD_BASE;
    spec.maxPx = config::Render::CAPTURE_PAD_MAX;
    spec.blurPx = clampRange(clampRange(optics.blurPx, 0.0f, blurCap)
                             * uniformScale, 0.0f, blurCap);
    spec.blurGain = glGatherReady() ? config::Render::CAPTURE_PAD_BLUR_GAIN
                                    : config::Render::CAPTURE_PAD_BLUR_GAIN_SAFE;
    spec.lensAmountPx = clampRange(optics.lensAmountPx, 0.0f, config::Render::LENS_REACH_MAX)
                      * uniformScale;
    spec.spectral = optics.spectral;
    spec.liquid = style.liquid;
    spec.legacyLensPad = legacyLensPadding();
    spec.backdropScaleX = style.backdropScaleX;
    spec.backdropScaleY = style.backdropScaleY;
    spec.backdropBlend = style.backdropBlend;
    spec.boxWidth = box.width();
    spec.boxHeight = box.height();
    return CapturePaddingPx(spec);
}

// 把当前的内容遮罩快照进 job（每个面板提交时调用一次）。
void snapshotContentMask(PanelJob& job) {
    job.maskRect[0] = g_contentMaskRect[0];
    job.maskRect[1] = g_contentMaskRect[1];
    job.maskRect[2] = g_contentMaskRect[2];
    job.maskRect[3] = g_contentMaskRect[3];
    job.maskRadius = g_contentMaskRadius;
    job.maskViewportH = g_contentMaskViewportH;
}

void applyCorners(PanelJob& job, const Corners& corners, float boxWidth, float boxHeight,
                  float uniformScale) {
    const float shorter = boxWidth < boxHeight ? boxWidth : boxHeight;
    const float limit = shorter > 0.0f ? shorter * 0.5f : 0.0f;
    job.corners.topLeft = clampRange(corners.topLeft, 0.0f, limit) * uniformScale;
    job.corners.topRight = clampRange(corners.topRight, 0.0f, limit) * uniformScale;
    job.corners.bottomRight = clampRange(corners.bottomRight, 0.0f, limit) * uniformScale;
    job.corners.bottomLeft = clampRange(corners.bottomLeft, 0.0f, limit) * uniformScale;
}

void applyEdge(PanelJob& job, const PanelStyle& style, float uniformScale) {
    const EdgeLight& edge = style.edge;
    job.edgeMode = edgeModeValue(edge, style.edgeOn);
    if (job.edgeMode <= 0.0f) {
        job.edgeWidth = 0.0f;
        job.edgeBlur = 0.0f;
        job.edgeAlpha = 0.0f;
    }
    else {
        job.edgeTint = rgbaOf(edge.color);
        job.edgeTintAlpha = alphaOf(edge.color);
        const bool directed = edge.mode == EdgeMode::Gradient || edge.mode == EdgeMode::Contour;
        job.edgeAngle = (directed ? edge.angleDegrees : 45.0f) * config::DEGREES_TO_RADIANS;
        job.edgeFalloff = directed ? edge.falloff : 1.0f;
        job.edgeAlpha = edge.alpha * style.alpha;
        float widthPx = edge.widthDp * densityScale() * uniformScale;
        const float shorter = job.frame.width() < job.frame.height()
            ? job.frame.width() : job.frame.height();
        const float widthLimit = shorter * 0.5f;
        if (widthPx > widthLimit) widthPx = widthLimit;
        job.edgeWidth = ceilf(widthPx);
        job.edgeBlur = edge.softnessDp * densityScale() * uniformScale;
    }
}

void applyInset(PanelJob& job, const PanelStyle& style, float uniformScale) {
    if (!style.insetOn) {
        job.insetAlpha = 0.0f;
    }
    else {
        const InsetShadow& inset = style.inset;
        job.insetTint = rgbaOf(inset.color);
        job.insetShiftX = inset.offsetXDp * densityScale() * job.pixelScaleX;
        job.insetShiftY = inset.offsetYDp * densityScale() * job.pixelScaleY;
        job.insetReach = inset.radiusDp * densityScale() * uniformScale;
        job.insetAlpha = inset.alpha * style.alpha * alphaOf(inset.color);
    }
}

void applyLiquid(PanelJob& job, const PanelStyle& style, float uniformScale) {
    if (!style.liquid) {
        job.dropRect[2] = -1.0f;
        job.dropRect[3] = -1.0f;
        job.feedRect[2] = -1.0f;
        job.feedRect[3] = -1.0f;
        job.strandRect[2] = -1.0f;
        job.strandRect[3] = -1.0f;
    }
    else {
        job.dropRect[0] = style.dropCenter.x * job.pixelScaleX;
        job.dropRect[1] = style.dropCenter.y * job.pixelScaleY;
        job.dropRect[2] = style.dropHalf.x * uniformScale;
        job.dropRect[3] = style.dropHalf.y * uniformScale;
        job.dropRound = style.dropRadius * uniformScale;
        job.feedRect[0] = style.feedCenter.x * job.pixelScaleX;
        job.feedRect[1] = style.feedCenter.y * job.pixelScaleY;
        job.feedRect[2] = style.feedHalf.x * uniformScale;
        job.feedRect[3] = style.feedHalf.y * uniformScale;
        job.feedRound = style.feedRadius * uniformScale;
        job.feedMerge = style.feedBlend * uniformScale;
        job.strandRect[0] = style.strandCenter.x * job.pixelScaleX;
        job.strandRect[1] = style.strandCenter.y * job.pixelScaleY;
        job.strandRect[2] = style.strandHalf.x * uniformScale;
        job.strandRect[3] = style.strandHalf.y * uniformScale;
        job.strandRound = style.strandRadius * uniformScale;
        job.strandMerge = style.strandBlend * uniformScale;
    }
}

void applyTrack(PanelJob& job, const PanelStyle& style, float uniformScale) {
    if (!style.trackOn) {
        job.trackOn = 0.0f;
    }
    else {
        job.trackOn = 1.0f;
        job.trackRect[0] = style.trackBox.left * job.pixelScaleX - job.frame.left;
        job.trackRect[1] = style.trackBox.top * job.pixelScaleY - job.frame.top;
        job.trackRect[2] = style.trackBox.right * job.pixelScaleX - job.frame.left;
        job.trackRect[3] = style.trackBox.bottom * job.pixelScaleY - job.frame.top;
        job.trackRadius = style.trackRadius * uniformScale;
        job.trackTint = style.trackColor;
    }
}

void applyGlow(PanelJob& job, const PanelStyle& style) {
    if (!style.glow.on || style.glow.progress <= config::Render::GLOW_LEVEL_MIN) {
        job.glowOn = 0.0f;
    }
    else {
        job.glowOn = 1.0f;
        job.glowX = style.glow.x * job.pixelScaleX;
        job.glowY = style.glow.y * job.pixelScaleY;
        const float shorter = job.frame.width() < job.frame.height()
            ? job.frame.width() : job.frame.height();
        job.glowReach = shorter * config::Render::GLOW_REACH_SCALE;
        job.glowLevel = style.glow.progress;
    }
}

int appendJob(const PanelStyle& style, const Box& box, float pad) {
    if (g_jobCount >= config::Render::PANEL_JOB_LIMIT) return -1;
    const float scaleX = pixelScaleXOf();
    const float scaleY = pixelScaleYOf();
    const float left = box.left * scaleX;
    const float top = box.top * scaleY;
    const float right = box.right * scaleX;
    const float bottom = box.bottom * scaleY;
    if (right - left <= 0.0f || bottom - top <= 0.0f) return -1;

    const int jobIndex = g_jobCount;
    PanelJob& job = g_jobs[jobIndex];
    job = PanelJob();
    job.frame = Box(left, top, right, bottom);
    job.viewW = viewBounds().width() * scaleX;
    job.viewH = viewBounds().height() * scaleY;
    job.pixelScaleX = scaleX;
    job.pixelScaleY = scaleY;

    const float captureLeft = floorf(left - pad);
    const float captureTop = floorf(top - pad);
    job.sampleOriginX = captureLeft;
    job.sampleOriginY = captureTop;
    job.sampleSizeX = ceilf(right + pad) - captureLeft;
    job.sampleSizeY = ceilf(bottom + pad) - captureTop;
    job.sampleStrideX = job.sampleSizeX;
    job.sampleStrideY = job.sampleSizeY;

    const float uniformScale = uniformPixelScale();
    const float blurCap = blurCeiling();
    applyCorners(job, style.corners, box.width(), box.height(), uniformScale);
    snapshotContentMask(job);

    const Optics& optics = style.optics;
    job.lensHeight = clampRange(optics.lensHeightPx, 0.0f, config::Render::LENS_REACH_MAX)
                   * uniformScale;
    job.lensAmount = clampRange(optics.lensAmountPx, 0.0f, config::Render::LENS_REACH_MAX)
                   * uniformScale;
    job.lensDepth = optics.depthAmount;
    job.darkGuard = optics.darkGuard * config::Render::DARK_GUARD_FLOOR;
    job.spectral = clampRange(optics.spectral, 0.0f, config::Render::SPECTRAL_MAX);
    job.blurRadius = clampRange(clampRange(optics.blurPx, 0.0f, blurCap)
                                * uniformScale, 0.0f, blurCap);
    job.panelOpacity = clampRange(optics.opacityOn ? optics.opacity : 1.0f, 0.0f,
                                  config::Render::PANEL_OPACITY_MAX);
    job.panelOpacity = clampRange(job.panelOpacity * clampRange(style.alpha, 0.0f, 1.0f), 0.0f,
                                  config::Render::PANEL_OPACITY_MAX);
    job.toneCtrl[0] = clampRange(optics.brightness + tintBrightness(), -1.0f, 1.0f);
    job.toneCtrl[1] = clampRange(optics.contrast * tintContrast(), 0.0f, 2.0f);
    job.toneCtrl[2] = clampRange(optics.saturation * tintSaturation(), 0.0f, 3.0f);
    job.toneBoost = optics.colorBoost ? 1.0f : 0.0f;
    job.refractTint = tintRefract();

    job.surface = Rgba(clampRange(style.surface.r, 0.0f, 1.0f),
                       clampRange(style.surface.g, 0.0f, 1.0f),
                       clampRange(style.surface.b, 0.0f, 1.0f),
                       clampRange(style.surface.a, 0.0f, 1.0f));
    job.surfaceMode = style.surfaceMode == SurfaceMode::Overlay ? 2.0f
                    : (style.surfaceMode == SurfaceMode::Hue ? 1.0f : 0.0f);
    job.sceneOn = (style.backdropOn && backdropEnabledFlag()) ? 1.0f : 0.0f;
    job.solidBackdrop = style.opaqueBackdrop ? 1.0f : 0.0f;
    job.stretchX = clampRange(style.backdropScaleX, 0.0f, 2.0f);
    job.stretchY = clampRange(style.backdropScaleY, config::Render::BACKDROP_SCALE_MIN, 2.0f);
    job.stretchMix = clampRange(style.backdropBlend, 0.0f, 1.0f);
    job.absentTint = Rgba(clampRange(style.fallback.r, 0.0f, 1.0f),
                          clampRange(style.fallback.g, 0.0f, 1.0f),
                          clampRange(style.fallback.b, 0.0f, 1.0f),
                          clampRange(style.fallback.a, 0.0f, 1.0f));

    applyEdge(job, style, uniformScale);
    applyInset(job, style, uniformScale);
    applyLiquid(job, style, uniformScale);
    applyTrack(job, style, uniformScale);
    applyGlow(job, style);
    ++g_jobCount;
    return jobIndex;
}

int appendShadeJob(const PanelStyle& style, const Box& box) {
    if (!style.shadowOn || style.shadow.alpha <= 0.0f) return -1;
    const DropShadow& shadow = style.shadow;
    const float radius = shadow.radiusDp * densityScale();
    const float offsetX = shadow.offsetXDp * densityScale();
    const float offsetY = shadow.offsetYDp * densityScale();
    const float extent = radius * config::Render::SHADOW_EXTENT_GAIN;
    const Box shadeBox(box.left - extent + offsetX, box.top - extent + offsetY,
                       box.right + extent + offsetX, box.bottom + extent + offsetY);

    PanelStyle shadeStyle;
    shadeStyle.corners = style.corners;
    shadeStyle.alpha = 1.0f;
    shadeStyle.backdropOn = false;
    shadeStyle.shadowOn = false;
    shadeStyle.edgeOn = false;
    shadeStyle.insetOn = false;
    shadeStyle.trackOn = false;
    shadeStyle.liquid = false;
    shadeStyle.surfaceMode = SurfaceMode::None;

    const int index = appendJob(shadeStyle, shadeBox, config::Render::CAPTURE_PAD_BASE);
    if (index < 0) return -1;

    PanelJob& job = g_jobs[index];
    job.shadeMode = 1.0f;
    job.shadeTint = Rgba(channelOf(shadow.color, 16), channelOf(shadow.color, 8),
                         channelOf(shadow.color, 0),
                         alphaOf(shadow.color) * shadow.alpha * style.alpha);
    job.shadeShiftX = offsetX * job.pixelScaleX;
    job.shadeShiftY = offsetY * job.pixelScaleY;
    job.shadeReach = radius * uniformPixelScale();
    job.shadeBodyW = box.width() * job.pixelScaleX;
    job.shadeBodyH = box.height() * job.pixelScaleY;
    return index;
}

void paintShade(int index, const Box& clip) {
    if (index < 0 || index >= g_jobCount) return;
    PanelJob& job = g_jobs[index];
    if (job.shadePainted) return;
    job.shadePainted = true;
    if (!jobIntersectsClip(job, &clip)) {
        aurora::panel_efficiency::culledShadows.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    const GatherInfo gather;
    glPaintPanel(job, clip, 0u, gather);
}

void captureScene(int index) {
    if (index < 0 || index >= g_jobCount) return;
    PanelJob& job = g_jobs[index];
    if (job.sampleReady || job.shadeMode > 0.5f || job.sceneOn < 0.5f) return;

    if (backdropReady()) {
        job.sampleOriginX = 0.0f;
        job.sampleOriginY = 0.0f;
        job.sampleSizeX = (float)g_backdropWidth;
        job.sampleSizeY = (float)g_backdropHeight;
        job.sampleStrideX = (float)g_backdropWidth;
        job.sampleStrideY = (float)g_backdropHeight;
        job.sampleReady = true;
    }
    else {

        const int screenW = (int)job.viewW;
        const int screenH = (int)job.viewH;
        if (screenW <= 0 || screenH <= 0) return;
        int x = (int)floorf(job.sampleOriginX);
        int y = (int)floorf(job.sampleOriginY);
        int w = (int)ceilf(job.sampleSizeX);
        int h = (int)ceilf(job.sampleSizeY);
        if (x < 0) { w += x; x = 0; }
        if (y < 0) { h += y; y = 0; }
        if (x > screenW) w = 0;
        if (y > screenH) h = 0;
        if (x + w > screenW) w = screenW - x;
        if (y + h > screenH) h = screenH - y;
        if (w <= 0 || h <= 0) return;

        float originX = 0.0f;
        float originY = 0.0f;
        float sizeX = 0.0f;
        float sizeY = 0.0f;
        float strideX = 0.0f;
        float strideY = 0.0f;
        if (!glCaptureRegion(x, y, w, h, screenW, screenH, originX, originY, sizeX, sizeY,
                             strideX, strideY)) {
        }
        else {
            job.sampleOriginX = originX;
            job.sampleOriginY = originY;
            job.sampleSizeX = sizeX;
            job.sampleSizeY = sizeY;
            job.sampleStrideX = strideX;
            job.sampleStrideY = strideY;
            job.sampleReady = true;
        }
    }
    if (job.sampleReady) {
        aurora::panel_efficiency::capturedPanels.fetch_add(1, std::memory_order_relaxed);
        aurora::panel_efficiency::capturedPixels.fetch_add(
            static_cast<std::uint64_t>(job.sampleSizeX) *
                static_cast<std::uint64_t>(job.sampleSizeY),
            std::memory_order_relaxed);
    }
}

// 诊断开关（默认关）：`AURORA_NO_GATHER=1` 跳过模糊 gather（性能归因用，画面会变）。
bool gatherDisabledForDiagnostics() {
    static const bool disabled = [] {
        const char* value = std::getenv("AURORA_NO_GATHER");
        return value != nullptr && value[0] == '1';
    }();
    return disabled;
}

void paintJob(int index, const Box& clip) {
    if (index < 0 || index >= g_jobCount) return;
    PanelJob& job = g_jobs[index];

    unsigned int sceneTexture = 0u;
    if (backdropReady()) {
        sceneTexture = g_backdropTexture;
    } else if (job.sampleReady) {
        sceneTexture = glCaptureTexture();
    }
    if (job.shadeMode <= 0.5f && sceneTexture == 0u) job.sceneOn = 0.0f;

    job.gatherActive = false;
    if (!gatherDisabledForDiagnostics()) {
    if (job.shadeMode <= 0.5f && !backdropReady() && sceneTexture != 0u
            && job.blurRadius >= config::Render::BLUR_MIN_SIGMA) {
        if (glRunGather((int)job.sampleSizeX, (int)job.sampleSizeY, sceneTexture,
                        glCaptureWidth(), glCaptureHeight(), job.blurRadius)) {
            const GatherInfo info = glGatherInfo();
            job.gatherActive = info.ready;
            job.gatherPitchX = info.pitchX;
            job.gatherPitchY = info.pitchY;
            job.gatherRows = info.rows;
            job.gatherTexelX = info.texelX;
            job.gatherTexelY = info.texelY;
            job.gatherLimitX = info.limitX;
            job.gatherLimitY = info.limitY;
        }
    }
    }

    GatherInfo gather;
    gather.ready = job.gatherActive;
    gather.pitchX = job.gatherPitchX;
    gather.pitchY = job.gatherPitchY;
    gather.rows = job.gatherRows;
    gather.texelX = job.gatherTexelX;
    gather.texelY = job.gatherTexelY;
    gather.limitX = job.gatherLimitX;
    gather.limitY = job.gatherLimitY;
    glPaintPanel(job, clip, sceneTexture, gather);
}

}

float pixelScaleXOf() {
    const float value = surfaceRef().pixelScaleX;
    return value > 0.0f ? value : 1.0f;
}

float pixelScaleYOf() {
    const float value = surfaceRef().pixelScaleY;
    return value > 0.0f ? value : 1.0f;
}

void bindBackdrop(unsigned int texture, int width, int height) {
    g_backdropTexture = texture;
    g_backdropWidth = width > 0 ? width : 0;
    g_backdropHeight = height > 0 ? height : 0;
}

void releaseBackdrop() {
    g_backdropTexture = 0;
    g_backdropWidth = 0;
    g_backdropHeight = 0;
}

void resetJobs() {
    g_jobCount = 0;
    g_panelCount = 0;
}

int submitJob(const PanelStyle& style, const Box& box) {
    if (!glReady() || g_jobCount >= config::Render::PANEL_JOB_LIMIT) return -1;
    return appendJob(style, box, capturePadding(style, box, uniformPixelScale()));
}

int registerPanel(int jobIndex, int shadeIndex) {
    if (g_panelCount >= config::Render::PANEL_JOB_LIMIT) return -1;
    const int panelIndex = g_panelCount;
    g_panelJob[panelIndex] = jobIndex;
    g_panelShade[panelIndex] = shadeIndex;
    ++g_panelCount;
    return panelIndex;
}

int submitPanelJob(const PanelStyle& style, const Box& box) {
    if (!glReady() || g_panelCount >= config::Render::PANEL_JOB_LIMIT) return -1;
    if (box.width() <= 0.0f || box.height() <= 0.0f) return -1;
    const int jobMark = g_jobCount;
    const int shadeIndex = appendShadeJob(style, box);
    const int jobIndex = submitJob(style, box);
    if (jobIndex < 0) {
        g_jobCount = jobMark;
        return -1;
    }
    g_jobs[jobIndex].shadeIndex = shadeIndex;
    const int panelIndex = registerPanel(jobIndex, shadeIndex);
    if (panelIndex < 0) {
        g_jobCount = jobMark;
        return -1;
    }
    return panelIndex;
}

void capturePanelByIndex(int index, const Box& clip, bool paintSharesClip = false) {
    if (index < 0 || index >= g_panelCount) return;
    paintShade(g_panelShade[index], clip);
    // Separate capture/paint API calls can intentionally change clip between
    // stages; only the adjacent combined entry point guarantees identical clip.
    const int jobIndex = g_panelJob[index];
    if (jobIndex >= 0 && jobIndex < g_jobCount &&
        !jobIntersectsClip(g_jobs[jobIndex], paintSharesClip ? &clip : nullptr)) {
        aurora::panel_efficiency::culledCaptures.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    captureScene(g_panelJob[index]);
}

void paintPanelByIndex(int index, const Box& clip) {
    if (index < 0 || index >= g_panelCount) return;
    paintShade(g_panelShade[index], clip);
    const int jobIndex = g_panelJob[index];
    if (jobIndex >= 0 && jobIndex < g_jobCount &&
        !jobIntersectsClip(g_jobs[jobIndex], &clip)) {
        aurora::panel_efficiency::culledPaints.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    paintJob(g_panelJob[index], clip);
}

}

int submitPanel(const PanelStyle& style, const Box& box) {
    return detail::submitPanelJob(style, box);
}

void runPanelCapture(int index, const Box& clip) {
    detail::capturePanelByIndex(index, clip);
}

void runPanelPaint(int index, const Box& clip) {
    detail::paintPanelByIndex(index, clip);
}

void runPanelCapturePaint(int index, const Box& clip) {
    detail::capturePanelByIndex(index, clip, true);
    detail::paintPanelByIndex(index, clip);
}

void appendPanelVisualSnapshot(std::vector<std::uint8_t>& bytes) {
    detail::snapshotScalar(bytes, detail::g_backdropTexture);
    detail::snapshotScalar(bytes, detail::g_backdropWidth);
    detail::snapshotScalar(bytes, detail::g_backdropHeight);
    detail::snapshotScalar(bytes, detail::backdropEnabledFlag());
    detail::snapshotScalar(bytes, detail::g_jobCount);
    for (int index = 0; index < detail::g_jobCount; ++index)
        detail::snapshotJob(bytes, detail::g_jobs[index]);
    detail::snapshotScalar(bytes, detail::g_panelCount);
    for (int index = 0; index < detail::g_panelCount; ++index) {
        detail::snapshotScalar(bytes, detail::g_panelJob[index]);
        detail::snapshotScalar(bytes, detail::g_panelShade[index]);
    }
}

}

namespace lgx {

// ★ Aurora 增量：宿主设置"内容可视区"的圆角遮罩（见 GlassUI.h 的说明）。
// 定义放在这里（而不是上面的匿名命名空间里）—— 那样会是内部链接，外部链接不到。
void setContentMask(float cx, float cy, float halfW, float halfH, float radius, float viewportH) {
    detail::g_contentMaskRect[0] = cx;
    detail::g_contentMaskRect[1] = cy;
    detail::g_contentMaskRect[2] = halfW;
    detail::g_contentMaskRect[3] = halfH;
    detail::g_contentMaskRadius = radius;
    detail::g_contentMaskViewportH = viewportH;
}

}  // namespace lgx
