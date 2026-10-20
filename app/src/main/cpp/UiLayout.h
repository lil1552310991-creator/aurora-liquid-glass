#pragma once

// -----------------------------------------------------------------------------
// 界面布局（UiLayout）—— 纯逻辑，零 ImGui / 零 Android 依赖。
//
// 解决的实际问题（用户反馈）：
//   · 横竖屏切换后窗口"飘到屏幕外面"；
//   · 横屏启动时主题窗口太长，底部根本够不到，调不了参数。
//
// 根因是位置/尺寸写死成像素常量：
//   SetNextWindowSize(ImVec2(760, 1400), FirstUseEver);
//   SetNextWindowPos(ImVec2(16, 730), FirstUseEver);
//   竖屏 1080x2376 时 730+1400=2130 刚好放得下；横屏 2376x1080 时同一个 1400
//   高直接甩出屏幕 700 多像素 —— 而且是 FirstUseEver，切回来也不会重算。
//
// 这里改成**按当前屏幕算**，并且按朝向换一种摆法：
//   竖屏：上下堆叠（二级窗口在上，主题窗口占满下方剩余高度）
//   横屏：左右并排（二级窗口在左，主题窗口在右）
// 两种朝向都保证两个窗口完整落在屏幕内，而且不是靠"裁掉超出部分"——
// 尺寸和位置都是**先按可用空间算出来的**。
//
// 另一个硬约束：触摸拦截区是"两个窗口的并集"，每帧由窗口形状重算
// （见 aurora_overlay.cpp 的 BuildOverlayTouchRegion）。所以布局一变，
// 触摸区会自动跟上，这里不需要额外处理。
// -----------------------------------------------------------------------------

namespace aurora::ui_layout {

struct Input final {
  float screenW = 0.0f;
  float screenH = 0.0f;
  // dp → px 的缩放（本工程按 2.0 用：字体与控件都是 2 倍放大）。
  float density = 2.0f;
};

struct Rect final {
  float x = 0.0f;
  float y = 0.0f;
  float w = 0.0f;
  float h = 0.0f;

  float right() const { return x + w; }
  float bottom() const { return y + h; }
  bool Valid() const { return w > 0.0f && h > 0.0f; }
};

struct Layout final {
  Rect secondary;      // 二级液态玻璃窗口
  Rect theme;          // 主题（调试面板）窗口
  bool horizontal = false;   // true = 左右并排（横屏）
};

namespace detail {

// ★ 布局不追求"占满整屏"。用户明确要求：留出空余方便拖边缘缩窗口。
//   88% 是权衡值：留太多会让横屏调试面板高度不足，留太少又抓不到边。
constexpr float kHeightFill = 0.88f;    // 窗口高度占可用高度的比例
constexpr float kMargin = 16.0f;      // 屏幕边缘留白
constexpr float kGap = 14.0f;         // 两个窗口之间
constexpr float kMinSecondaryW = 320.0f;
constexpr float kMinSecondaryH = 260.0f;
constexpr float kMinThemeW = 360.0f;
constexpr float kMinThemeH = 260.0f;
// 二级窗口的"理想"尺寸（屏幕够大时用它，不再无限膨胀）
constexpr float kPreferredSecondaryW = 760.0f;
constexpr float kPreferredSecondaryH = 700.0f;

inline float Clamp(float value, float lo, float hi) {
  if (value < lo) return lo;
  if (value > hi) return hi;
  return value;
}

}  // namespace detail

// 按屏幕尺寸与朝向算出两个窗口的矩形。
//
// 关键不变量（离线测试逐条钉住）：
//   1) 两个矩形都完整落在 [0, screenW] × [0, screenH] 内；
//   2) 两个矩形互不重叠；
//   3) 横屏时左右并排（宽度方向切分），竖屏时上下堆叠（高度方向切分）；
//   4) 可用空间足够时用"理想尺寸"，不足时按比例压缩，而不是裁掉。
inline Layout Compute(const Input& in) {
  using namespace detail;
  Layout out;
  if (in.screenW <= 0.0f || in.screenH <= 0.0f) return out;

  const float availW = in.screenW - kMargin * 2.0f;
  const float availH = in.screenH - kMargin * 2.0f;
  if (availW <= 0.0f || availH <= 0.0f) return out;

  // 朝向判定：宽 > 高 就是横屏。用 >= 让正方形走竖屏分支（更保守）。
  out.horizontal = in.screenW > in.screenH;

  if (out.horizontal) {
    // ---- 横屏：左右并排 ----
    // 二级窗口取可用宽度的 42%，但不超过理想宽度；主题窗口吃掉剩下的。
    // 34%（原 42%）：更窄一点，右侧留白更多。
    float secW = Clamp(availW * 0.34f, kMinSecondaryW, kPreferredSecondaryW);
    if (secW < kMinSecondaryW) secW = availW * 0.5f;   // 屏幕极窄的兜底
    // 主题窗口占 52%，剩下约 14% 作右侧空白 —— 给"拖右边缘改宽度"用。
    const float themeW = Clamp(availW * 0.52f, kMinThemeW, availW - secW - kGap);
    // 高度只吃 88%：上下各留一点，方便抓上/下边缘。
    const float height = availH * kHeightFill;

    out.secondary.x = kMargin;
    out.secondary.y = kMargin;
    out.secondary.w = secW;
    out.secondary.h = height;

    out.theme.x = kMargin + secW + kGap;
    out.theme.y = kMargin;
    out.theme.w = themeW;
    out.theme.h = height;
  } else {
    // ---- 竖屏：上下堆叠 ----
    // 二级窗口取可用高度的 38%，但不超过理想高度；主题窗口吃掉剩下的。
    // 上下也留边：先按 88% 算出"可用高度"，再在它里面分。
    const float usableH = availH * kHeightFill;
    float secH = Clamp(usableH * 0.36f, kMinSecondaryH, kPreferredSecondaryH);
    if (secH < kMinSecondaryH) secH = usableH * 0.5f;   // 屏幕极矮的兜底
    const float themeH = usableH - secH - kGap;
    // 宽度两者一致：触摸拦截区是并集，宽度对齐能少圈无用区域。
    // 宽度也只吃 92%：右侧留出可抓取的空白。
    // 720p 竖屏时 availW 只有 688，吃满就完全贴边、拖不动宽度 ——
    // 这个数值有离线用例钉着（UiLayoutTest 的 rightGap 断言）。
    const float width = Clamp(availW * 0.92f, 0.0f, kPreferredSecondaryW);
    out.secondary.x = kMargin;
    out.secondary.y = kMargin;
    out.secondary.w = width;
    out.secondary.h = secH;

    out.theme.x = kMargin;
    out.theme.y = kMargin + secH + kGap;
    out.theme.w = width;
    out.theme.h = themeH;
  }

  // 最后一道保险：任何一路算出负数/越界，都夹回可用区域。
  // （这是数值兜底，不是"靠裁剪解决问题" —— 上面的尺寸本来就是算出来的。）
  const auto sanitize = [&](Rect& r) {
    if (r.w < 1.0f) r.w = 1.0f;
    if (r.h < 1.0f) r.h = 1.0f;
    if (r.x < 0.0f) r.x = 0.0f;
    if (r.y < 0.0f) r.y = 0.0f;
    if (r.right() > in.screenW) r.x = in.screenW - r.w;
    if (r.bottom() > in.screenH) r.y = in.screenH - r.h;
    if (r.x < 0.0f) { r.x = 0.0f; r.w = in.screenW; }
    if (r.y < 0.0f) { r.y = 0.0f; r.h = in.screenH; }
  };
  sanitize(out.secondary);
  sanitize(out.theme);
  return out;
}

// 判断是否需要重新布局：屏幕尺寸变了（横竖屏切换 / 分屏 / 折叠屏展开）。
// 位置或尺寸的细小抖动（像素级）不算，避免每帧都重置用户调好的窗口。
inline bool NeedsRelayout(float lastW, float lastH, float screenW, float screenH) {
  constexpr float kTolerance = 2.0f;
  if (lastW <= 0.0f || lastH <= 0.0f) return true;   // 首次
  const float dw = lastW - screenW;
  const float dh = lastH - screenH;
  return (dw > kTolerance || dw < -kTolerance || dh > kTolerance || dh < -kTolerance);
}

}  // namespace aurora::ui_layout
