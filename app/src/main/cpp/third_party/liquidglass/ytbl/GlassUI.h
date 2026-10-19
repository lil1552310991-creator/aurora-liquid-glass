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
#pragma once

#include "GlassConfig.h"

#include <cstdint>
#include <vector>

namespace lgx {

inline constexpr int TAB_LIMIT = 8;
inline constexpr int MENU_ENTRY_LIMIT = 12;

struct Point {
    float x = 0.0f;
    float y = 0.0f;

    Point() = default;
    Point(float px, float py) : x(px), y(py) {}
};

struct Box {
    float left = 0.0f;
    float top = 0.0f;
    float right = 0.0f;
    float bottom = 0.0f;

    Box() = default;
    Box(float l, float t, float r, float b) : left(l), top(t), right(r), bottom(b) {}
    Box(const Point& min, const Point& max)
        : left(min.x), top(min.y), right(max.x), bottom(max.y) {}

    float width() const { return right - left; }
    float height() const { return bottom - top; }
    float centerX() const { return (left + right) * 0.5f; }
    float centerY() const { return (top + bottom) * 0.5f; }
    Point center() const { return Point(centerX(), centerY()); }

    bool holds(const Point& p) const {
        return p.x >= left && p.x <= right && p.y >= top && p.y <= bottom;
    }
    bool holds(float x, float y) const {
        return x >= left && x <= right && y >= top && y <= bottom;
    }
    Box grown(float amount) const {
        return Box(left - amount, top - amount, right + amount, bottom + amount);
    }
    Box shifted(float dx, float dy) const {
        return Box(left + dx, top + dy, right + dx, bottom + dy);
    }
};

struct Rgba {
    float r = 0.0f;
    float g = 0.0f;
    float b = 0.0f;
    float a = 0.0f;

    Rgba() = default;
    Rgba(float red, float green, float blue, float alpha)
        : r(red), g(green), b(blue), a(alpha) {}
};

struct Corners {
    float topLeft = config::CAPSULE_RADIUS;
    float topRight = config::CAPSULE_RADIUS;
    float bottomRight = config::CAPSULE_RADIUS;
    float bottomLeft = config::CAPSULE_RADIUS;

    Corners() = default;
    explicit Corners(float radius)
        : topLeft(radius), topRight(radius), bottomRight(radius), bottomLeft(radius) {}
    Corners(float tl, float tr, float br, float bl)
        : topLeft(tl), topRight(tr), bottomRight(br), bottomLeft(bl) {}

    static Corners capsule() { return Corners(); }
    static Corners rounded(float radius) { return Corners(radius); }
};

struct TextSize {
    float width = 0.0f;
    float height = 0.0f;
};

struct PointerState {
    float x = 0.0f;
    float y = 0.0f;
    bool down = false;
    bool pressed = false;
    bool released = false;
};

struct Surface {
    float width = 0.0f;
    float height = 0.0f;
    float pixelScaleX = 1.0f;
    float pixelScaleY = 1.0f;
};

using TextMeasurer = TextSize (*)(const char* content, float size);

unsigned int rgba(float alpha, unsigned int rgb);
Rgba rgbaOf(unsigned int argb);
unsigned int mixRgb(unsigned int from, unsigned int to, float t);

float saturate(float value);
float clampRange(float value, float low, float high);
float lerp(float from, float to, float t);
float easeOutCubic(float x);

float px(float value);
void setScale(float value);

struct GlassColors {
    Rgba primary;
    Rgba secondary;
};

void setGlassColors(const GlassColors& colors, bool withMotion = true);
bool glassIsLight();
float colorProgress();

void setBackdropEnabled(bool on);
void bindBackdropTexture(unsigned int texture, int width, int height);
void releaseBackdropTexture();
Rgba frostedFallbackColor();

// ---------------------------------------------------------------------------
// ★ Aurora 增量钩子（非上游 API，默认未注册 ⇒ 行为与上游逐位一致）
//
// 背景来源两种模式：
//   1) bindBackdropTexture() 绑定一张**全局**纹理 —— 所有面板都按
//      「宿主屏幕像素 / 该纹理尺寸」取 UV。它只能表达"整个世界的一张图"，
//      无法表达"这一块玻璃下方的内容"。
//   2) 不绑定 ⇒ 本库自己用 glCopyTexSubImage2D 从**当前帧缓冲**里抠出面板
//      所在区域。它天然是 Z 序正确的（此刻帧缓冲里只有先于本面板绘制的东西），
//      但帧缓冲里没有宿主之外的现实世界（例如 Android 桌面）。
//
// Aurora 需要的是两者之和：帧缓冲（=本进程已绘制的下层 UI）叠在宿主提供的
// 世界底层（=我们图层之下的屏幕内容）之上，且合成结果 alpha=1（世界是不透明的）。
// 这个钩子在"已经从帧缓冲抠出区域、还没做模糊/绘制"之间被调用，宿主可以就地
// 把抠出来的像素替换成 Z 序正确的合成结果。
//
// 返回 true 表示宿主已经重写了 captureTexture 的内容；返回 false 表示保持原样。
// 回调必须自行保存/恢复 GL 状态（帧缓冲绑定、视口、混合、程序、VAO）。
// ---------------------------------------------------------------------------
struct PanelSampleRect {
    float left = 0.0f;
    float top = 0.0f;
    float right = 0.0f;
    float bottom = 0.0f;
};

using SceneComposer = bool (*)(void* user, const PanelSampleRect& rect,
                               unsigned int captureTexture, int captureStrideW,
                               int captureStrideH, int viewWidth, int viewHeight);

void setSceneComposer(SceneComposer composer, void* user = nullptr);

// Aurora read-only hook: append the current panel draw inputs to an exact damage
// key. Call after building the UI and before executing panel render callbacks;
// callback outputs (captured samples, gather state, shadePainted) are excluded.
// No optical parameters, animation state, or rendering behavior are changed.
void appendPanelVisualSnapshot(std::vector<std::uint8_t>& bytes);

void setPointer(const PointerState& state);

void setSurface(const Surface& surface);
void setHostBounds(const Box& box);

void setTextMeasurer(TextMeasurer measurer);

class Canvas;

using IconPainter = void (*)(Canvas& canvas, const Point& center, float size,
                             unsigned int color, void* tag);

struct PointerProbe {
    bool held = false;
    bool activated = false;
    bool clicked = false;
};

class CanvasBackend {
public:
    virtual ~CanvasBackend() = default;

    virtual Box clipRect() = 0;
    virtual void onFillRect(const Box& box, float radius, unsigned int color) = 0;
    virtual void onStrokeRect(const Box& box, float radius, float thickness,
                              unsigned int color) = 0;
    virtual void onFillPoly(const Point* points, int count, unsigned int color) = 0;
    virtual void onFillConcave(const Point* points, int count, unsigned int color) = 0;
    virtual void onStrokePoly(const Point* points, int count, float thickness,
                              unsigned int color) = 0;
    virtual void onLine(const Point& from, const Point& to, unsigned int color,
                        float thickness) = 0;
    virtual void onFillCircle(const Point& center, float radius, unsigned int color,
                              int segments) = 0;
    virtual void onFillTriangle(const Point& a, const Point& b, const Point& c,
                                unsigned int color) = 0;
    virtual void onFillArc(const Point& center, float radius, float fromRadians,
                           float toRadians, unsigned int color, int segments) = 0;
    virtual void onText(const Point& position, float size, unsigned int color,
                        const char* content, float wrapWidth) = 0;
    virtual void onImage(unsigned int texture, const Box& box, const Point& uvMin,
                         const Point& uvMax, unsigned int color) = 0;
    virtual void onPushClip(const Box& box) = 0;
    virtual void onPopClip() = 0;
    virtual void onPanelCapture(int index) = 0;
    virtual void onPanelPaint(int index) = 0;
    virtual PointerProbe onProbe(const Box& box, const void* owner, bool interactive) = 0;
    virtual void onBlockPointer(const Box& box) = 0;
    virtual void onClearPointerBlock() = 0;
};

void attachCanvasBackend(CanvasBackend* backend);

class Canvas {
public:
    explicit Canvas(CanvasBackend* backend) : surface_(backend) {}

    void fillRect(const Box& box, float radius, unsigned int color);
    void strokeRect(const Box& box, float radius, float thickness, unsigned int color);
    void fillPoly(const Point* points, int count, unsigned int color);
    void fillConcave(const Point* points, int count, unsigned int color);
    void strokePoly(const Point* points, int count, float thickness, unsigned int color);
    void line(const Point& from, const Point& to, unsigned int color, float thickness);
    void fillCircle(const Point& center, float radius, unsigned int color, int segments);
    void fillTriangle(const Point& a, const Point& b, const Point& c, unsigned int color);
    void fillArc(const Point& center, float radius, float fromRadians, float toRadians,
                 unsigned int color, int segments);
    void text(const Point& position, float size, unsigned int color, const char* content,
              float wrapWidth = 0.0f);
    void image(unsigned int texture, const Box& box, const Point& uvMin, const Point& uvMax,
               unsigned int color);
    void pushClip(const Box& box);
    void popClip();
    void icon(IconPainter painter, const Point& center, float size, unsigned int color,
              void* tag);

private:
    CanvasBackend* surface_ = nullptr;
};

bool startup();
void shutdown();
void beginFrame(float deltaSeconds);

enum class EdgeMode { Gradient, Plain, Ambient, Contour };

struct EdgeLight {
    EdgeMode mode = EdgeMode::Gradient;
    unsigned int color = config::Render::EDGE_TINT;
    float angleDegrees = 45.0f;
    float falloff = 1.0f;
    float widthDp = 0.5f;
    float softnessDp = 0.25f;
    float alpha = 1.0f;

    EdgeLight() = default;
    EdgeLight(EdgeMode m, unsigned int tint, float angle, float sharp, float width,
              float softness, float a)
        : mode(m), color(tint), angleDegrees(angle), falloff(sharp), widthDp(width),
          softnessDp(softness), alpha(a) {}

    static EdgeLight gradient() { return EdgeLight(); }
    static EdgeLight plain(unsigned int tint = config::Render::EDGE_TINT_PLAIN);
    static EdgeLight ambient(float strength = config::Render::EDGE_AMBIENT_ALPHA);
    static EdgeLight contour(float angleDegrees = -90.0f, float falloff = 1.35f,
                             unsigned int tint = 0xCCFFFFFFu);
};

struct DropShadow {
    float radiusDp = 24.0f;
    float offsetXDp = 0.0f;
    float offsetYDp = 4.0f;
    unsigned int color = 0x1A000000u;
    float alpha = 1.0f;

    DropShadow() = default;
    explicit DropShadow(float radius, unsigned int tint)
        : radiusDp(radius), offsetYDp(radius / config::Render::SHADOW_OFFSET_DIVISOR), color(tint) {}
    DropShadow(float radius, float offsetX, float offsetY, unsigned int tint, float a)
        : radiusDp(radius), offsetXDp(offsetX), offsetYDp(offsetY), color(tint), alpha(a) {}
};

struct InsetShadow {
    float radiusDp = 24.0f;
    float offsetXDp = 0.0f;
    float offsetYDp = 24.0f;
    unsigned int color = 0x26000000u;
    float alpha = 1.0f;

    InsetShadow() = default;
    InsetShadow(float radius, float a)
        : radiusDp(radius), offsetXDp(0.0f), offsetYDp(0.0f), alpha(a) {}
    InsetShadow(float radius, float offsetX, float offsetY, unsigned int tint, float a)
        : radiusDp(radius), offsetXDp(offsetX), offsetYDp(offsetY), color(tint), alpha(a) {}
};

struct Optics {
    bool colorBoost = false;
    float brightness = 0.0f;
    float contrast = 1.0f;
    float saturation = 1.0f;
    bool opacityOn = false;
    float opacity = 1.0f;
    float blurPx = 0.0f;
    float lensHeightPx = 0.0f;
    float lensAmountPx = 0.0f;
    float depthAmount = 0.0f;
    float darkGuard = 0.0f;
    float spectral = 0.0f;
};

enum class SurfaceMode { None, Hue, Overlay };

struct GlowSpot {
    bool on = false;
    float progress = 0.0f;
    float x = 0.0f;
    float y = 0.0f;
};

struct PanelStyle {
    Corners corners;
    Optics optics;

    EdgeLight edge;
    bool edgeOn = true;

    DropShadow shadow;
    bool shadowOn = false;

    InsetShadow inset;
    bool insetOn = false;

    Rgba surface;
    SurfaceMode surfaceMode = SurfaceMode::None;
    float alpha = 1.0f;

    bool backdropOn = true;
    bool opaqueBackdrop = false;
    float backdropScaleX = 1.0f;
    float backdropScaleY = 1.0f;
    float backdropBlend = 0.0f;
    Rgba fallback;

    bool trackOn = false;
    Box trackBox;
    float trackRadius = 0.0f;
    Rgba trackColor;

    GlowSpot glow;

    bool liquid = false;
    Point dropCenter;
    Point dropHalf;
    float dropRadius = 0.0f;
    Point feedCenter;
    Point feedHalf;
    float feedRadius = 0.0f;
    float feedBlend = 0.0f;
    Point strandCenter;
    Point strandHalf;
    float strandRadius = 0.0f;
    float strandBlend = 0.0f;
};

int submitPanel(const PanelStyle& style, const Box& box);

void runPanelCapture(int index, const Box& clip);
void runPanelPaint(int index, const Box& clip);
// Both stages share the same clip, so invisible glass can skip capture too.
void runPanelCapturePaint(int index, const Box& clip);
void drawPanel(const PanelStyle& style, const Box& box);

// ★ Aurora 增量：宿主注入的"内容可视区"圆角遮罩（SDF，带抗锯齿）。
//   cx/cy = 中心（cy 用自上而下的屏幕坐标）、halfW/halfH = 半宽半高、
//   radius < 0 ⇒ 关闭。面板提交时快照，所以调用点要包住这一批绘制。
//   用途：ImGui 的矩形 scissor 管不到 ytbl 自己的 shader，靠近窗口圆角的
//   玻璃组件会突破轮廓 —— 用它在片元上裁剪到窗口的圆角几何。
void setContentMask(float cx, float cy, float halfW, float halfH, float radius, float viewportH);
void blockPointer(const Box& box);
void clearPointerBlock();

// =============================================================================
// ★ Aurora 增量：组件级可调配方（Tune）
//
// 这些结构把各组件的"玻璃配方"从 .cpp 里写死的常量提升成公开、可读写、默认可
// 复现的字段，供液态玻璃调试工作台逐参数绑定。
//
// 三条硬约束（改这个文件时必须守住）：
//   ① 每个字段的初值 **严格等于** 原实现里用的常量或默认构造值；
//      不动它 ⇒ 渲染结果与加这个结构之前逐位相同。
//   ② 只提升**真实参与绘制**的量。凡是 shader 不读、绘制不用的，不许出现在这里
//      （否则工作台就会出现"拖了没反应"的假滑块）。
//   ③ 由动画驱动的量（例如 Toggle 的 press）提升的是**端点值或增益**，
//      不是把动画本身变成参数 —— 手指按下去该动还得动。
// =============================================================================

// ---- 边光配方（各组件共用的一份参数）---------------------------------------
//
// mode 语义（这是刻意的，为了同时支持"跟随动画"和"手动指定"两种用法）：
//   0 = 跟随组件自身的按压动画（例如 Toggle 的 `EdgeLight::ambient(press)`），
//       此时 alpha 是**增益**（1.0 = 原配方，不改变任何东西）
//   1 = Gradient  2 = Ambient  3 = Plain  4 = Contour（此时 alpha 是绝对值）
//   5 = 关闭
struct EdgeTune {
    int mode = 0;
    float widthDp = 0.5f;
    float softnessDp = 0.25f;
    float alpha = 1.0f;
    float angleDegrees = 45.0f;
    float falloff = 1.0f;
    bool usePresetTint = true;               // true ⇒ 用 ytbl 的主题自适应预设色
    // ★ 用真正的 float[4] 而不是 Rgba：调试层要把它当一个颜色参数直接绑定，
    //   绑定的是一个**数组**，不能靠结构体成员的地址算术（那是 UB）。
    float tint[4] = {1.0f, 1.0f, 1.0f, 0.5f};
};

// ---- 液态按键 PillButton -----------------------------------------------------
struct PillTune {
    // 形状
    bool capsule = true;                 // 原实现写死 Corners::capsule()
    float cornerTlDp = 16.0f;            // 仅 !capsule 时生效
    float cornerTrDp = 16.0f;
    float cornerBrDp = 16.0f;
    float cornerBlDp = 16.0f;
    // 光学（blur/lens 仍在 PillButton 自己的字段上，这边是它们够不着的那部分）
    bool colorBoost = true;              // 原实现写死 true
    float depthAmount = 0.0f;            // 原实现写死 0
    float darkGuard = 0.0f;              // 原实现不设 ⇒ 0
    // 色彩滤镜
    float brightness = 0.0f;
    float contrast = 1.0f;
    float saturation = 1.0f;
    bool opacityOn = false;
    float opacity = 1.0f;
    // 表面
    int surfaceModeOverride = -1;        // -1 = 跟随 tinted/surfaced 的原逻辑
    float fallbackR = 0.0f;              // 原实现 style.fallback = (0,0,0,0)
    float fallbackG = 0.0f;
    float fallbackB = 0.0f;
    float fallbackA = 0.0f;
    // 背景采样
    bool backdropOn = true;
    bool opaqueBackdrop = false;
    float backdropScaleX = 1.0f;
    float backdropScaleY = 1.0f;
    float backdropBlend = 0.0f;
    // 边光（mode 见 EdgeTune；默认 0 = 跟随 contourEdge 的原逻辑）
    EdgeTune edge;
    // 轮廓光（contourEdge == true 时原实现写死的那一组）
    float contourAlpha = config::Render::EDGE_CONTOUR_ALPHA;
    float contourPressGain = config::Render::EDGE_CONTOUR_PRESS_GAIN;
    float contourWidthDp = config::Render::EDGE_CONTOUR_WIDTH;
    float contourSoftnessDp = config::Render::EDGE_CONTOUR_SOFTNESS;
    // 外阴影（原实现只开 shadowOn，配方取 DropShadow 的默认构造）
    float shadowRadiusDp = 24.0f;
    float shadowOffsetXDp = 0.0f;
    float shadowOffsetYDp = 4.0f;
    float shadowTint[4] = {0.0f, 0.0f, 0.0f, 0.101960786f};  // 0x1A000000
    float shadowAlpha = 1.0f;
    // 内阴影（原实现不开；打开后配方与 InsetShadow() 默认一致）
    bool insetOn = false;
    float insetRadiusDp = 24.0f;
    float insetOffsetXDp = 0.0f;
    float insetOffsetYDp = 24.0f;
    float insetTint[4] = {0.0f, 0.0f, 0.0f, 0.14901961f};    // 0x26000000
    float insetAlpha = 1.0f;
    // 交互（弹簧）
    float glowStiffness = config::Motion::PRESS_STIFFNESS;
    float glowDamping = config::Motion::PRESS_DAMPING;
    float pointerStiffness = config::Motion::POINTER_STIFFNESS;
    float pointerDamping = config::Motion::POINTER_DAMPING;
};

// ---- 液态开关 Toggle ---------------------------------------------------------
struct ToggleTune {
    // 尺寸（dp）—— 原实现全部来自 config::Layout::TOGGLE_*
    float trackWidthDp = config::Layout::TOGGLE_TRACK_W_DP;
    float trackHeightDp = config::Layout::TOGGLE_TRACK_H_DP;
    float thumbWidthDp = config::Layout::TOGGLE_THUMB_W_DP;
    float thumbHeightDp = config::Layout::TOGGLE_THUMB_H_DP;
    float paddingDp = config::Layout::TOGGLE_PADDING_DP;
    float travelDp = config::Layout::TOGGLE_TRAVEL_DP;
    float trackRadiusRatio = 0.5f;       // 原实现 trackRadius = trackHeight * 0.5
    // 交互
    float dragScaleDp = config::Layout::TOGGLE_DRAG_SCALE_DP;
    float dragSlopDp = config::Layout::TOGGLE_DRAG_SLOP_DP;
    float velocityGain = config::Layout::TOGGLE_VELOCITY_GAIN;
    float pressedScale = config::Motion::DRAG_SCALE;              // 按压缩放
    float valueStiffness = config::Motion::DRAG_VALUE_STIFFNESS;  // 弹簧刚度
    float valueDamping = config::Motion::DRAG_VALUE_DAMPING;      // 阻尼
    // 玻璃（按压两端点：press=0 用 rest，press=1 用 press）
    //  ★ 注意方向：原实现是 blur = lerp(FLOOR, GLASS, 1 - press) ⇒
    //    **静止时模糊最大（GLASS_BLUR_DP）、按下去反而更清晰（FLOOR）**。
    //    （Kyant 的 LiquidToggle 同样是 blur(8.dp * (1 - p))：越按越清晰。）
    //    离线回归会盯住这两个默认值，写反了会直接红。
    float blurRestDp = config::Layout::GLASS_BLUR_DP;
    float blurPressDp = config::Layout::GLASS_BLUR_FLOOR_DP;
    float lensHeightPressDp = config::Layout::TOGGLE_LENS_HEIGHT_DP;
    float lensAmountPressDp = config::Layout::TOGGLE_LENS_AMOUNT_DP;
    float lensGain = 1.0f;               // 乘在 press 上（1.0 = 原配方）
    float darkGuard = config::Layout::TOGGLE_DARK_GUARD;
    bool spectralOn = true;              // 原实现写死 spectral = 1.0
    // 背景采样（原实现按 press 在 min..max 之间插值）
    float backdropScaleXMin = config::Layout::TOGGLE_BACKDROP_X_MIN;
    float backdropScaleXMax = config::Layout::TOGGLE_BACKDROP_X_MAX;
    float backdropScaleYMin = config::Layout::TOGGLE_BACKDROP_Y_MIN;
    float backdropScaleYMax = config::Layout::TOGGLE_BACKDROP_Y_MAX;
    float backdropBlend = 1.0f;
    // 表面 / 边光 / 内阴影
    float surfaceAlphaGain = 1.0f;       // 原实现 surface.a = 1 - press
    int surfaceMode = 2;                 // 2 = Overlay
    float edgeAlphaGain = 1.0f;          // 原实现 EdgeLight::ambient(press)
    EdgeTune edge;                       // mode 0 = 用上面的 edgeAlphaGain 语义
    float insetRadiusDp = config::Layout::TOGGLE_INNER_DP;
    float insetRestAlpha = config::Layout::TOGGLE_INNER_REST_ALPHA;
    float insetRestRadius = config::Layout::TOGGLE_INNER_REST_DP;
};

// ---- 液态滑块 Slider ---------------------------------------------------------
struct SliderTune {
    // 尺寸
    float trackHeightDp = config::Layout::SLIDER_TRACK_H_DP;
    float trackRadiusRatio = 0.5f;
    float fillRadiusRatio = 0.5f;
    float thumbWidthDp = config::Layout::SLIDER_THUMB_W_DP;
    float thumbHeightDp = config::Layout::SLIDER_THUMB_H_DP;
    float thumbEdgeMin = config::Layout::SLIDER_THUMB_EDGE_MIN;
    float thumbEdgeMax = config::Layout::SLIDER_THUMB_EDGE_MAX;
    float velocityGain = config::Layout::SLIDER_VELOCITY_GAIN;
    float pressedScale = config::Motion::DRAG_SCALE;
    float valueStiffness = config::Motion::DRAG_VALUE_STIFFNESS;
    float valueDamping = config::Motion::DRAG_VALUE_DAMPING;
    // 玻璃（方向与 Toggle 一致：静止模糊最大、按下去更清晰）
    float blurRestDp = config::Layout::GLASS_BLUR_DP;
    float blurPressDp = config::Layout::GLASS_BLUR_FLOOR_DP;
    float lensHeightPressDp = config::Layout::SLIDER_LENS_HEIGHT_DP;
    float lensAmountPressDp = config::Layout::SLIDER_LENS_AMOUNT_DP;
    float lensGain = 1.0f;
    bool spectralOn = false;             // 原实现写死 spectral = 0
    float dispersion = 0.0f;             // spectralOn 打开后的强度
    float depthAmount = 0.0f;
    float darkGuard = 0.0f;
    // 背景采样
    float backdropScaleXMin = config::Layout::SLIDER_BACKDROP_X_MIN;
    float backdropScaleXMax = config::Layout::SLIDER_BACKDROP_X_MAX;
    float backdropScaleYMin = config::Layout::SLIDER_BACKDROP_Y_MIN;
    float backdropScaleYMax = config::Layout::SLIDER_BACKDROP_Y_MAX;
    float backdropBlend = 1.0f;
    bool backdropOn = true;
    // 表面 / 边光 / 阴影
    float surfaceAlphaGain = 1.0f;
    int surfaceMode = 2;
    float edgeAlphaGain = 1.0f;          // 原实现 EdgeLight::ambient(1.0)
    EdgeTune edge;                       // mode 0 = 用上面的 edgeAlphaGain 语义
    float shadowRadiusDp = config::Layout::SLIDER_SHADOW_DP;
    float shadowOffsetYRatio = 1.0f / config::Render::SHADOW_OFFSET_DIVISOR;
    float shadowAlpha = 1.0f;
    float insetRadiusDp = config::Layout::SLIDER_INNER_DP;
    bool insetOnPress = true;            // 原实现 insetOn = press > ALPHA_MIN
};

// ---- 液态底部导航 TabStrip ---------------------------------------------------
struct TabStripTune {
    // 尺寸（dp）—— 原实现来自 config::Layout::TAB_*
    float panelHeightDp = config::Layout::TAB_PANEL_H_DP;
    float innerHeightDp = config::Layout::TAB_INNER_H_DP;
    float insetDp = config::Layout::TAB_INSET_DP;
    float slopDp = config::Layout::TAB_SLOP_DP;
    float panelShiftDp = config::Layout::TAB_PANEL_SHIFT_DP;
    float cellScale = config::Layout::TAB_CELL_SCALE;
    float iconGapDp = config::Layout::TAB_ICON_GAP_DP;
    float panelGrowDp = config::Layout::TAB_PANEL_GROW_DP;
    float velocityGain = config::Layout::TAB_VELOCITY_GAIN;
    // 背景层（container 面板）
    float blurDp = config::Layout::TAB_BLUR_DP;
    float lensHeightDp = config::Layout::TAB_LENS_HEIGHT_DP;
    float lensAmountDp = config::Layout::TAB_LENS_AMOUNT_DP;
    bool colorBoost = true;              // 原实现写死 true
    float depthAmount = 0.0f;            // 原实现写死 0
    bool shadowOn = true;                // 原实现 shadowOn = true + DropShadow() 默认
    float shadowRadiusDp = 24.0f;
    float shadowOffsetYDp = 4.0f;
    float shadowTint[4] = {0.0f, 0.0f, 0.0f, 0.101960786f};
    float shadowAlpha = 1.0f;
    // 指示器层（thumb）
    float pressLensHeightDp = config::Layout::TAB_PRESS_LENS_HEIGHT_DP;
    float pressLensAmountDp = config::Layout::TAB_PRESS_LENS_AMOUNT_DP;
    float spectral = config::Layout::TAB_SPECTRAL;
    float idleAlpha = config::Layout::TAB_IDLE_ALPHA;
    float pressAlpha = config::Layout::TAB_PRESS_ALPHA;
    float pressInnerDp = config::Layout::TAB_PRESS_INNER_DP;
    float indicatorShadowDp = config::Layout::TAB_SHADOW_DP;
    float indicatorShadowAlpha = config::Layout::TAB_SHADOW_ALPHA;
    // 选中指示器跟随的弹簧（原实现来自 config::Motion::TAB_*）
    float indicatorHeldScale = config::Motion::TAB_INDICATOR_HELD_SCALE;
    float panelStiffness = config::Motion::TAB_PANEL_STIFFNESS;
    float panelVisible = config::Motion::TAB_PANEL_VISIBLE;
};

class PillButton {
public:
    PillButton();
    ~PillButton();
    PillButton(const PillButton&) = delete;
    PillButton& operator=(const PillButton&) = delete;

    const char* label = nullptr;
    bool interactive = true;
    bool tinted = false;
    Rgba tint = Rgba(1.0f, 1.0f, 1.0f, 1.0f);
    bool surfaced = false;
    Rgba surface = Rgba(1.0f, 1.0f, 1.0f, 0.3f);
    unsigned int textColor = 0;
    float textSizeSp = config::Layout::BUTTON_TEXT_SP;
    float blurDp = config::Layout::BUTTON_BLUR_DP;
    float lensHeightDp = config::Layout::BUTTON_LENS_HEIGHT_DP;
    float lensAmountDp = config::Layout::BUTTON_LENS_AMOUNT_DP;
    float spectral = 0.0f;
    float visualScale = 1.0f;
    float alphaScale = 1.0f;
    bool shadowOn = true;
    bool contourEdge = false;
    bool glassOn = true;
    bool textOn = true;
    IconPainter icon = nullptr;
    void* iconTag = nullptr;
    int clicks = 0;

    // ★ Aurora 增量：把此前写死在 buttonStyle() 里的那部分配方变成可调参数。
    //   所有字段默认值严格等于原实现里用的常量/默认构造值 ⇒ 不碰它时逐像素不变。
    //   为什么需要它：调试工作台要求"每个玻璃组件都能被独立读取/修改/保存"，
    //   而按钮此前只能调 blur/lens/spectral，边光、阴影、内外阴影、表面模式、
    //   色彩滤镜、形状圆角全部够不着。
    PillTune tune;

    bool draw(const Box& box);
    void resetGlow();
    bool glowVisible() const;
    float glowProgress() const;
    bool busy() const;

private:
    struct State;
    State* state_ = nullptr;
    State& state();
    const State& state() const;
};

enum class EntryKind { Action, Heading, Divider };
enum class MenuReach { Auto, DownRight, DownLeft, UpRight, UpLeft };

struct MenuEntry {
    EntryKind kind = EntryKind::Action;
    const char* caption = nullptr;
    const char* detail = nullptr;
    const char* shortcut = nullptr;
    unsigned int texture = 0;
    IconPainter painter = nullptr;
    void* tag = nullptr;
    bool selectable = true;
    bool danger = false;
    bool keepOpen = false;
    float heightDp = 0.0f;
};

class PopMenu {
public:
    PopMenu();
    ~PopMenu();
    PopMenu(const PopMenu&) = delete;
    PopMenu& operator=(const PopMenu&) = delete;

    bool expanded = false;
    bool interactive = true;
    int cursorRow = 0;
    int count = 0;
    int activated = -1;
    MenuEntry entries[MENU_ENTRY_LIMIT];

    float widthDp = config::Layout::MENU_DEFAULT_W_DP;
    float cornerDp = config::Layout::MENU_DEFAULT_CORNER_DP;
    float bandCornerDp = config::Layout::MENU_DEFAULT_BAND_CORNER_DP;
    MenuReach reach = MenuReach::Auto;
    bool clampToHost = true;
    bool closeOnOutside = true;
    float threadDp = config::Layout::MENU_DEFAULT_THREAD_DP;
    float gapDp = config::Layout::MENU_DEFAULT_GAP_DP;
    float drainStart = config::Layout::MENU_DRAIN_START;
    float drainSpan = config::Layout::MENU_DRAIN_SPAN;
    float greetDp = config::Layout::MENU_DEFAULT_GREET_DP;
    float stretchGain = 1.0f;
    bool materialFlare = true;
    float rimDp = 1.0f;
    float rimAlpha = 1.0f;
    float jelly = 1.0f;

    const char* capText = nullptr;
    unsigned int capTextColor = 0;
    float capTextSp = config::Layout::MENU_CAP_TEXT_SP;
    unsigned int bandColor = config::Render::MENU_BAND;

    bool draw(const Box& capBox);
    void drawOverlay();
    void setOpen(bool value, bool withMotion = true);
    bool isOpen() const;

private:
    struct State;
    State* state_ = nullptr;
    State& state();
    const State& state() const;
    int slotAt(const Point& pointer) const;
};

class Toggle {
public:
    Toggle();
    ~Toggle();
    Toggle(const Toggle&) = delete;
    Toggle& operator=(const Toggle&) = delete;

    bool on = false;
    unsigned int accentColor = config::Layout::TOGGLE_ACCENT;
    unsigned int trackColor = config::Layout::TOGGLE_TRACK;

    // ★ Aurora 增量：逐实例可调配方（默认 = 原写死的常量）
    ToggleTune tune;

    bool draw(const Box& box);
    void setOn(bool value, bool withMotion = true);
    bool busy() const;

private:
    struct State;
    State* state_ = nullptr;
    State& state();
    const State& state() const;
};

class Slider {
public:
    Slider();
    ~Slider();
    Slider(const Slider&) = delete;
    Slider& operator=(const Slider&) = delete;

    float minValue = 0.0f;
    float maxValue = 1.0f;
    float value = 0.5f;
    unsigned int accentColor = config::Layout::SLIDER_ACCENT;
    unsigned int trackColor = config::Layout::SLIDER_TRACK;

    // ★ Aurora 增量：逐实例可调配方（默认 = 原写死的常量）
    SliderTune tune;

    bool draw(const Box& box);
    void setRange(float from, float to);
    float animatedValue() const;
    float displayValue() const;
    bool busy() const;

private:
    struct State;
    State* state_ = nullptr;
    State& state();
    const State& state() const;
};

class TabStrip {
public:
    TabStrip();
    ~TabStrip();
    TabStrip(const TabStrip&) = delete;
    TabStrip& operator=(const TabStrip&) = delete;

    int activeIndex = 0;
    int tabCount = 4;
    const char* titles[TAB_LIMIT] = {"Tab1", "Tab2", "Tab3", "Tab4",
                                     "Tab5", "Tab6", "Tab7", "Tab8"};
    unsigned int textures[TAB_LIMIT] = {};
    IconPainter painters[TAB_LIMIT] = {};
    void* tags[TAB_LIMIT] = {};
    float iconSizeDp = config::Layout::TAB_ICON_SIZE_DP;
    float labelSizeDp = config::Layout::TAB_LABEL_SIZE_DP;
    unsigned int accentColor = config::Layout::TAB_ACCENT;
    unsigned int containerColor = config::Layout::TAB_CONTAINER;
    unsigned int iconColor = 0;
    bool changed = false;

    // ★ Aurora 增量：导航栏每一层的可调配方（默认 = 原写死的常量）
    TabStripTune tune;

    bool appendTab(const char* title, unsigned int texture = 0);
    bool dropTab(int index);
    void resetTabs();

    bool draw(const Box& box);
    void select(int index, bool withMotion = true);
    float indicator() const;
    bool busy() const;

private:
    struct State;
    State* state_ = nullptr;
    State& state();
    const State& state() const;
};

class Modal {
public:
    Modal();
    ~Modal();
    Modal(const Modal&) = delete;
    Modal& operator=(const Modal&) = delete;

    bool visible = false;
    const char* title = nullptr;
    const char* message = nullptr;
    const char* cancelLabel = "Cancel";
    const char* confirmLabel = "OK";
    float glassBlurDp = config::Layout::DIALOG_GLASS_BLUR_DP;
    int result = 0;

    void open(const char* dialogTitle, const char* dialogMessage);
    void openFrom(const char* dialogTitle, const char* dialogMessage, const Box& origin);
    void close(int dialogResult);
    int draw(const Box& area);
    float sourceScale() const;

private:
    struct State;
    State* state_ = nullptr;
    State& state();
    const State& state() const;
    void advance(float dt);
};

void iconHome(Canvas& canvas, const Point& center, float size, unsigned int color, void* tag);
void iconSearch(Canvas& canvas, const Point& center, float size, unsigned int color, void* tag);
void iconMessage(Canvas& canvas, const Point& center, float size, unsigned int color, void* tag);
void iconProfile(Canvas& canvas, const Point& center, float size, unsigned int color, void* tag);
void iconHeart(Canvas& canvas, const Point& center, float size, unsigned int color, void* tag);
void iconStar(Canvas& canvas, const Point& center, float size, unsigned int color, void* tag);
void iconBell(Canvas& canvas, const Point& center, float size, unsigned int color, void* tag);
void iconGear(Canvas& canvas, const Point& center, float size, unsigned int color, void* tag);
void iconClose(Canvas& canvas, const Point& center, float size, unsigned int color, void* tag);
void iconCheck(Canvas& canvas, const Point& center, float size, unsigned int color, void* tag);
void iconChevron(Canvas& canvas, const Point& center, float size, unsigned int color, void* tag);

}
