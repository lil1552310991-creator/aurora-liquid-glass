// =============================================================================
//  "参数 → 真实渲染输入" 的离线回归
//
//  这一项守的是整个调试工作台最容易变成**假控件**的那一段：
//  滑块拖了，PanelStyle 到底变没变、变的是不是你以为的那一项。
//
//  做法：所有组件的玻璃配方都已被抽成纯函数（ytbl/GlassRecipes.h），
//  于是可以在 PC 上直接构造 PanelStyle 并**逐字段**比对：
//    · 默认 Tune 必须复现原实现里那组写死的常量（golden 值）；
//    · 改一个 Tune 字段，只允许对应的 PanelStyle 字段变化（其它字段逐位相同）；
//    · 两个组件实例的 Tune 互不影响（"调按钮 1 不会动按钮 2"的结构性证明）。
//
//  注意：本文件**不跑 GL**，也不调用 draw()；它验证的是"配方"这一段。
//  PanelStyle → PanelJob → shader uniform 那一段由 RenderDamageTest /
//  PanelCallbackBatchTest / SceneCompositeTest 覆盖。
// =============================================================================
#include "third_party/liquidglass/ytbl/GlassRecipes.h"
#include "third_party/liquidglass/ytbl/GlassUI.h"
#include "third_party/liquidglass/ytbl/GlassConfig.h"

#include <cassert>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

using namespace lgx;

// -----------------------------------------------------------------------------
// 宿主机替身：Runtime.cpp 的 GL 相关函数不参与本测试（-ffunction-sections +
// --gc-sections 会把它们丢掉），但 px()/densityScale() 这两个纯函数要真的来自
// 生产代码，否则"dp→px 换算"就没被验证。这里只补齐缺失的链接符号。
// -----------------------------------------------------------------------------
namespace {
int g_checks = 0;

void Check(bool condition, const char* what) {
  ++g_checks;
  if (!condition) {
    std::printf("  断言失败（第 %d 项）：%s\n", g_checks, what);
    std::fflush(stdout);
    assert(condition);
  }
}

void CheckNear(float actual, float expected, float epsilon, const char* what) {
  ++g_checks;
  if (!(std::fabs(actual - expected) <= epsilon)) {
    std::printf("  断言失败（第 %d 项）：%s（实际 %.6f，期望 %.6f）\n", g_checks, what,
                static_cast<double>(actual), static_cast<double>(expected));
    std::fflush(stdout);
    assert(false);
  }
}

// ---- PanelStyle 的逐字段比对 -------------------------------------------------
//  这是"只改了该改的那一项"的判据：把两份 style 的每个字段摊平比较，
//  输出**所有**不同的字段名（而不是第一个不同的），失败信息才能直接指出越界项。
void CompareStyle(const PanelStyle& a, const PanelStyle& b, std::vector<std::string>* out) {
  auto push = [out](const char* name) { if (out != nullptr) out->push_back(name); };
  auto f = [&](const char* name, float x, float y) { if (x != y) push(name); };

  f("corners.topLeft", a.corners.topLeft, b.corners.topLeft);
  f("corners.topRight", a.corners.topRight, b.corners.topRight);
  f("corners.bottomRight", a.corners.bottomRight, b.corners.bottomRight);
  f("corners.bottomLeft", a.corners.bottomLeft, b.corners.bottomLeft);

  f("optics.colorBoost", a.optics.colorBoost ? 1.0f : 0.0f, b.optics.colorBoost ? 1.0f : 0.0f);
  f("optics.brightness", a.optics.brightness, b.optics.brightness);
  f("optics.contrast", a.optics.contrast, b.optics.contrast);
  f("optics.saturation", a.optics.saturation, b.optics.saturation);
  f("optics.opacityOn", a.optics.opacityOn ? 1.0f : 0.0f, b.optics.opacityOn ? 1.0f : 0.0f);
  f("optics.opacity", a.optics.opacity, b.optics.opacity);
  f("optics.blurPx", a.optics.blurPx, b.optics.blurPx);
  f("optics.lensHeightPx", a.optics.lensHeightPx, b.optics.lensHeightPx);
  f("optics.lensAmountPx", a.optics.lensAmountPx, b.optics.lensAmountPx);
  f("optics.depthAmount", a.optics.depthAmount, b.optics.depthAmount);
  f("optics.darkGuard", a.optics.darkGuard, b.optics.darkGuard);
  f("optics.spectral", a.optics.spectral, b.optics.spectral);

  f("edgeOn", a.edgeOn ? 1.0f : 0.0f, b.edgeOn ? 1.0f : 0.0f);
  f("edge.mode", (float)a.edge.mode, (float)b.edge.mode);
  f("edge.color", (float)a.edge.color, (float)b.edge.color);
  f("edge.angleDegrees", a.edge.angleDegrees, b.edge.angleDegrees);
  f("edge.falloff", a.edge.falloff, b.edge.falloff);
  f("edge.widthDp", a.edge.widthDp, b.edge.widthDp);
  f("edge.softnessDp", a.edge.softnessDp, b.edge.softnessDp);
  f("edge.alpha", a.edge.alpha, b.edge.alpha);

  f("shadowOn", a.shadowOn ? 1.0f : 0.0f, b.shadowOn ? 1.0f : 0.0f);
  f("shadow.radiusDp", a.shadow.radiusDp, b.shadow.radiusDp);
  f("shadow.offsetXDp", a.shadow.offsetXDp, b.shadow.offsetXDp);
  f("shadow.offsetYDp", a.shadow.offsetYDp, b.shadow.offsetYDp);
  f("shadow.color", (float)a.shadow.color, (float)b.shadow.color);
  f("shadow.alpha", a.shadow.alpha, b.shadow.alpha);

  f("insetOn", a.insetOn ? 1.0f : 0.0f, b.insetOn ? 1.0f : 0.0f);
  f("inset.radiusDp", a.inset.radiusDp, b.inset.radiusDp);
  f("inset.offsetXDp", a.inset.offsetXDp, b.inset.offsetXDp);
  f("inset.offsetYDp", a.inset.offsetYDp, b.inset.offsetYDp);
  f("inset.color", (float)a.inset.color, (float)b.inset.color);
  f("inset.alpha", a.inset.alpha, b.inset.alpha);

  f("surface.r", a.surface.r, b.surface.r);
  f("surface.g", a.surface.g, b.surface.g);
  f("surface.b", a.surface.b, b.surface.b);
  f("surface.a", a.surface.a, b.surface.a);
  f("surfaceMode", (float)a.surfaceMode, (float)b.surfaceMode);
  f("alpha", a.alpha, b.alpha);

  f("backdropOn", a.backdropOn ? 1.0f : 0.0f, b.backdropOn ? 1.0f : 0.0f);
  f("opaqueBackdrop", a.opaqueBackdrop ? 1.0f : 0.0f, b.opaqueBackdrop ? 1.0f : 0.0f);
  f("backdropScaleX", a.backdropScaleX, b.backdropScaleX);
  f("backdropScaleY", a.backdropScaleY, b.backdropScaleY);
  f("backdropBlend", a.backdropBlend, b.backdropBlend);
  f("fallback.r", a.fallback.r, b.fallback.r);
  f("fallback.g", a.fallback.g, b.fallback.g);
  f("fallback.b", a.fallback.b, b.fallback.b);
  f("fallback.a", a.fallback.a, b.fallback.a);

  f("trackOn", a.trackOn ? 1.0f : 0.0f, b.trackOn ? 1.0f : 0.0f);
  f("trackBox.left", a.trackBox.left, b.trackBox.left);
  f("trackBox.top", a.trackBox.top, b.trackBox.top);
  f("trackBox.right", a.trackBox.right, b.trackBox.right);
  f("trackBox.bottom", a.trackBox.bottom, b.trackBox.bottom);
  f("trackRadius", a.trackRadius, b.trackRadius);
  f("trackColor.r", a.trackColor.r, b.trackColor.r);
  f("trackColor.g", a.trackColor.g, b.trackColor.g);
  f("trackColor.b", a.trackColor.b, b.trackColor.b);
  f("trackColor.a", a.trackColor.a, b.trackColor.a);

  f("glow.on", a.glow.on ? 1.0f : 0.0f, b.glow.on ? 1.0f : 0.0f);
  f("glow.progress", a.glow.progress, b.glow.progress);
  f("glow.x", a.glow.x, b.glow.x);
  f("glow.y", a.glow.y, b.glow.y);
}

std::string Join(const std::vector<std::string>& items) {
  std::string out;
  for (std::size_t i = 0; i < items.size(); ++i) {
    if (i != 0) out += ", ";
    out += items[i];
  }
  return out;
}

// 断言"两份 style 的差异恰好是某几个字段"
void CheckDiff(const PanelStyle& a, const PanelStyle& b,
               const std::vector<std::string>& expected, const char* what) {
  std::vector<std::string> diff;
  CompareStyle(a, b, &diff);
  ++g_checks;
  if (diff != expected) {
    std::printf("  断言失败（第 %d 项）：%s\n    实际差异: [%s]\n    期望差异: [%s]\n",
                g_checks, what, Join(diff).c_str(), Join(expected).c_str());
    std::fflush(stdout);
    assert(false);
  }
}

detail::ToggleTrackVisual MakeTrack() {
  detail::ToggleTrackVisual track;
  track.box = Box(10.0f, 20.0f, 138.0f, 76.0f);   // 64x28 dp @ density 2
  track.radius = 28.0f;
  track.color = 0xFF30D158u;
  return track;
}

// PillButton 是不可拷贝的（持有 State*）⇒ 用就地配置的方式，不返回对象
void ConfigureButton(PillButton& button, bool tinted, bool surfaced, bool contour) {
  button.label = "test";
  button.tinted = tinted;
  button.surfaced = surfaced;
  button.contourEdge = contour;
}

}  // namespace

int main() {
  std::printf("GlassRecipeTest: 参数 → 真实渲染输入（PanelStyle）回归\n");
  CheckNear(px(1.0f), 2.0f, 1e-6f, "dp→px 密度换算（lgx::px 来自生产代码）");

  // =========================================================================
  // 组 1：默认 Tune 必须复现原实现里写死的那组常量（golden）
  // =========================================================================
  {
    const detail::ToggleTrackVisual track = MakeTrack();
    const ToggleTune tune;

    // press = 0：模糊取 GLASS_BLUR_DP（★ 静止时最模糊）、透镜为 0、表面全亮
    const PanelStyle rest = detail::ToggleThumbStyle(tune, 0.0f, 0.0f, track);
    CheckNear(rest.optics.blurPx, px(config::Layout::GLASS_BLUR_DP), 1e-6f,
              "开关圆钮静止模糊 = GLASS_BLUR_DP（原 Toggle.cpp:159 的 lerp(FLOOR, GLASS, 1-p)）");
    CheckNear(rest.optics.lensHeightPx, 0.0f, 1e-6f, "静止时没有折射");
    CheckNear(rest.optics.lensAmountPx, 0.0f, 1e-6f, "静止时没有折射位移");
    CheckNear(rest.optics.darkGuard, config::Layout::TOGGLE_DARK_GUARD, 1e-6f, "darkGuard 常量");
    CheckNear(rest.optics.spectral, 1.0f, 1e-6f, "色散写死 1.0");
    Check(rest.optics.colorBoost == false, "colorBoost 写死 false");
    Check(rest.surfaceMode == SurfaceMode::Overlay, "表面模式 Overlay");
    CheckNear(rest.surface.a, 1.0f, 1e-6f, "静止时表面 alpha = 1 - press = 1");
    CheckNear(rest.backdropScaleX, config::Layout::TOGGLE_BACKDROP_X_MIN, 1e-6f,
              "静止背景缩放 X 下限");
    CheckNear(rest.backdropScaleY, config::Layout::TOGGLE_BACKDROP_Y_MIN, 1e-6f,
              "静止背景缩放 Y 下限");
    CheckNear(rest.backdropBlend, 1.0f, 1e-6f, "背景混合写死 1.0");
    Check(rest.shadowOn == false, "开关圆钮没有外阴影");
    Check(rest.insetOn == true, "开关圆钮有内阴影");
    CheckNear(rest.inset.radiusDp,
              config::Layout::TOGGLE_INNER_DP * config::Layout::TOGGLE_INNER_REST_DP, 1e-6f,
              "静止内阴影半径 = TOGGLE_INNER_DP * REST_DP");
    CheckNear(rest.inset.alpha, config::Layout::TOGGLE_INNER_REST_ALPHA, 1e-6f,
              "静止内阴影 alpha = TOGGLE_INNER_REST_ALPHA");
    Check(rest.trackOn == true, "轨道开启");
    CheckNear(rest.trackRadius, track.radius, 1e-6f, "轨道圆角来自调用方");
    CheckNear(rest.edge.alpha, 0.0f, 1e-6f, "静止时 EdgeLight::ambient(0) ⇒ alpha 0");
    Check(rest.edge.mode == EdgeMode::Ambient, "开关圆钮的边光是 Ambient");

    // press = 1：模糊降到 GLASS_BLUR_FLOOR_DP（越按越清晰）、透镜满、表面透明
    const PanelStyle held = detail::ToggleThumbStyle(tune, 1.0f, 1.0f, track);
    CheckNear(held.optics.blurPx, px(config::Layout::GLASS_BLUR_FLOOR_DP), 1e-6f,
              "按压模糊 = GLASS_BLUR_FLOOR_DP（按下去更清晰，与 Kyant 同向）");
    CheckNear(held.optics.lensHeightPx, px(config::Layout::TOGGLE_LENS_HEIGHT_DP), 1e-6f,
              "按压折射高度 = TOGGLE_LENS_HEIGHT_DP");
    CheckNear(held.optics.lensAmountPx, px(config::Layout::TOGGLE_LENS_AMOUNT_DP), 1e-6f,
              "按压折射强度 = TOGGLE_LENS_AMOUNT_DP");
    CheckNear(held.surface.a, 0.0f, 1e-6f, "按压到底表面完全透明");
    CheckNear(held.backdropScaleX, config::Layout::TOGGLE_BACKDROP_X_MAX, 1e-6f,
              "按压背景缩放 X 上限");
    CheckNear(held.inset.alpha, 1.0f, 1e-6f, "按压到底内阴影 alpha = 1");
    CheckNear(held.edge.alpha, 1.0f, 1e-6f, "按压到底 EdgeLight::ambient(1)");
  }

  // =========================================================================
  // 组 2：改一个参数 ⇒ 只改对应的那一项（不多改、不少改）
  // =========================================================================
  {
    const detail::ToggleTrackVisual track = MakeTrack();
    const ToggleTune base;
    const PanelStyle before = detail::ToggleThumbStyle(base, 0.5f, 0.5f, track);

    {
      ToggleTune t = base;
      t.lensHeightPressDp = 20.0f;
      CheckDiff(before, detail::ToggleThumbStyle(t, 0.5f, 0.5f, track),
                {"optics.lensHeightPx"}, "折射高度只改 lensHeightPx");
    }
    {
      ToggleTune t = base;
      t.lensAmountPressDp = 44.0f;
      CheckDiff(before, detail::ToggleThumbStyle(t, 0.5f, 0.5f, track),
                {"optics.lensAmountPx"}, "折射强度只改 lensAmountPx");
    }
    {
      ToggleTune t = base;
      t.blurPressDp = 3.0f;   // 按压端点变化 ⇒ 中点插值也变
      CheckDiff(before, detail::ToggleThumbStyle(t, 0.5f, 0.5f, track), {"optics.blurPx"},
                "按压模糊只改 blurPx");
    }
    {
      ToggleTune t = base;
      t.insetRestAlpha = 0.5f;
      CheckDiff(before, detail::ToggleThumbStyle(t, 0.5f, 0.5f, track), {"inset.alpha"},
                "静止内阴影 alpha 只改 inset.alpha");
    }
    {
      ToggleTune t = base;
      t.backdropScaleXMax = 1.5f;
      CheckDiff(before, detail::ToggleThumbStyle(t, 0.5f, 0.5f, track), {"backdropScaleX"},
                "背景缩放上限只改 backdropScaleX");
    }
    {
      ToggleTune t = base;
      t.spectralOn = false;
      CheckDiff(before, detail::ToggleThumbStyle(t, 0.5f, 0.5f, track), {"optics.spectral"},
                "色散开关只改 spectral");
    }
    {
      // 折射增益改的是"press 的有效值"，所以它必然同时影响所有由 press 驱动的量。
      // 这里只断言**必须**包含的那几项，而不是钉死总数 —— 钉死总数会让测试
      // 在"以后多了一个由 press 驱动的量"时假报警。
      ToggleTune t = base;
      t.lensGain = 2.0f;
      std::vector<std::string> diff;
      CompareStyle(before, detail::ToggleThumbStyle(t, 0.5f, 0.5f, track), &diff);
      auto has = [&diff](const char* name) {
        for (const std::string& item : diff) {
          if (item == name) return true;
        }
        return false;
      };
      Check(has("optics.lensHeightPx") && has("optics.lensAmountPx") && has("inset.radiusDp"),
            "折射增益同时作用于透镜高度/强度/内阴影");
      Check(!diff.empty(), "折射增益确实改变了渲染输入（不是无效参数）");
    }
    // 明确的反例：改一个**不影响**该组的参数，必须一个字段都不变
    {
      ToggleTune t = base;
      t.trackWidthDp = 120.0f;    // 轨道宽度由调用方算 Box，不属于 PanelStyle
      std::vector<std::string> diff;
      CompareStyle(before, detail::ToggleThumbStyle(t, 0.5f, 0.5f, track), &diff);
      Check(diff.empty(), "轨道宽度不进入圆钮的 PanelStyle（几何在组件层算）");
    }
  }

  // =========================================================================
  // 组 3：两个组件实例的参数互不影响
  // =========================================================================
  {
    const detail::ToggleTrackVisual track = MakeTrack();
    Toggle first;
    Toggle second;
    Toggle third;
    first.tune.lensHeightPressDp = 30.0f;
    second.tune.lensAmountPressDp = 55.0f;
    CheckNear(third.tune.lensHeightPressDp, config::Layout::TOGGLE_LENS_HEIGHT_DP, 1e-6f,
              "没被改过的实例仍是默认值");
    const PanelStyle a = detail::ToggleThumbStyle(first.tune, 0.5f, 1.0f, track);
    const PanelStyle b = detail::ToggleThumbStyle(second.tune, 0.5f, 1.0f, track);
    CheckNear(a.optics.lensHeightPx, px(30.0f), 1e-6f, "第一个实例用自己的折射高度");
    CheckNear(b.optics.lensAmountPx, px(55.0f), 1e-6f, "第二个实例用自己的折射强度");
    CheckNear(b.optics.lensHeightPx, px(config::Layout::TOGGLE_LENS_HEIGHT_DP), 1e-6f,
              "第二个实例的折射高度没有被第一个改到");

    // 两个按钮实例
    PillButton b1;
    PillButton b2;
    ConfigureButton(b1, false, false, false);
    ConfigureButton(b2, false, false, false);
    b1.tune.darkGuard = 0.8f;
    b1.tune.shadowRadiusDp = 40.0f;
    const PanelStyle s1 = detail::PillButtonStyle(b1, 0.0f, 1.0f);
    const PanelStyle s2 = detail::PillButtonStyle(b2, 0.0f, 1.0f);
    CheckNear(s1.optics.darkGuard, 0.8f, 1e-6f, "按钮 1 的 darkGuard 生效");
    CheckNear(s2.optics.darkGuard, 0.0f, 1e-6f, "按钮 2 的 darkGuard 未受影响");
    CheckNear(s1.shadow.radiusDp, 40.0f, 1e-6f, "按钮 1 的阴影半径生效");
    CheckNear(s2.shadow.radiusDp, 24.0f, 1e-6f, "按钮 2 的阴影半径仍是默认");
  }

  // =========================================================================
  // 组 4：PillButton 的默认分支与原实现完全一致
  // =========================================================================
  {
    {
      PillButton button; ConfigureButton(button, false, false, false);   // 普通按钮
      const PanelStyle style = detail::PillButtonStyle(button, 0.0f, 1.0f);
      Check(style.corners.topLeft == config::CAPSULE_RADIUS, "默认是胶囊");
      Check(style.optics.colorBoost == true, "按钮 colorBoost 写死 true");
      CheckNear(style.optics.depthAmount, 0.0f, 1e-6f, "按钮 depthAmount 写死 0");
      CheckNear(style.optics.blurPx, px(config::Layout::BUTTON_BLUR_DP), 1e-6f, "按钮模糊 2dp");
      CheckNear(style.optics.lensHeightPx, px(config::Layout::BUTTON_LENS_HEIGHT_DP), 1e-6f,
                "按钮折射高度 12dp");
      CheckNear(style.optics.lensAmountPx, px(config::Layout::BUTTON_LENS_AMOUNT_DP), 1e-6f,
                "按钮折射强度 24dp");
      CheckNear(style.optics.spectral, 0.0f, 1e-6f, "按钮默认无色散");
      Check(style.surfaceMode == SurfaceMode::Overlay, "普通按钮表面模式 Overlay");
      Check(style.edgeOn == true, "边光默认开启");
      Check(style.edge.mode == EdgeMode::Gradient, "默认边光是 Gradient");
      Check(style.shadowOn == true, "默认开外阴影");
      CheckNear(style.shadow.offsetYDp, 4.0f, 1e-6f, "阴影偏移 Y = 4（DropShadow 默认）");
      Check(style.insetOn == false, "默认不开内阴影");
      CheckNear(style.fallback.a, 0.0f, 1e-6f, "fallback 全透明");
    }
    {
      PillButton button; ConfigureButton(button, true, false, false);    // 着色按钮
      const PanelStyle style = detail::PillButtonStyle(button, 0.0f, 1.0f);
      Check(style.surfaceMode == SurfaceMode::Hue, "着色按钮走 Hue");
    }
    {
      PillButton button; ConfigureButton(button, false, true, false);    // 表面色按钮
      const PanelStyle style = detail::PillButtonStyle(button, 0.0f, 1.0f);
      Check(style.surfaceMode == SurfaceMode::Overlay, "表面色按钮走 Overlay");
    }
    {
      PillButton button; ConfigureButton(button, false, false, true);    // 轮廓光按钮
      // 第 2 个参数是 press、第 3 个是 fade（面板整体 alpha）—— 这里只让 press 变，
      // 否则 style.alpha/surface.a 也会跟着变，断言就分不清是谁造成的
      const PanelStyle style = detail::PillButtonStyle(button, 0.0f, 1.0f);
      Check(style.edge.mode == EdgeMode::Contour, "contourEdge ⇒ Contour 边光");
      CheckNear(style.edge.alpha, config::Render::EDGE_CONTOUR_ALPHA, 1e-6f,
                "轮廓光基础 alpha = EDGE_CONTOUR_ALPHA");
      CheckNear(style.edge.widthDp, config::Render::EDGE_CONTOUR_WIDTH, 1e-6f, "轮廓光宽度");
      const PanelStyle pressed = detail::PillButtonStyle(button, 1.0f, 1.0f);
      CheckNear(pressed.edge.alpha,
                config::Render::EDGE_CONTOUR_ALPHA + config::Render::EDGE_CONTOUR_PRESS_GAIN,
                1e-6f, "轮廓光随按压增亮（PRESS_GAIN）");
      std::vector<std::string> diff;
      CompareStyle(style, pressed, &diff);
      Check(diff.size() == 1 && diff[0] == "edge.alpha", "轮廓光只有 alpha 随按压变化");
    }
    {
      // 形状：关掉胶囊 + 四角独立
      PillButton button; ConfigureButton(button, false, false, false);
      button.tune.capsule = false;
      button.tune.cornerTlDp = 8.0f;
      button.tune.cornerTrDp = 4.0f;
      button.tune.cornerBrDp = 2.0f;
      button.tune.cornerBlDp = 1.0f;
      const PanelStyle style = detail::PillButtonStyle(button, 0.0f, 1.0f);
      CheckNear(style.corners.topLeft, px(8.0f), 1e-6f, "四角独立：左上");
      CheckNear(style.corners.topRight, px(4.0f), 1e-6f, "四角独立：右上");
      CheckNear(style.corners.bottomRight, px(2.0f), 1e-6f, "四角独立：右下");
      CheckNear(style.corners.bottomLeft, px(1.0f), 1e-6f, "四角独立：左下");
    }
    {
      // 边光模式切换：只动 edge.*
      PillButton plain; ConfigureButton(plain, false, false, false);
      PillButton overridden; ConfigureButton(overridden, false, false, false);
      overridden.tune.edge.mode = 3;      // Plain
      std::vector<std::string> diff;
      CompareStyle(detail::PillButtonStyle(plain, 0.0f, 1.0f),
                   detail::PillButtonStyle(overridden, 0.0f, 1.0f), &diff);
      // 判据是"**只**动了 edge.*"而不是"恰好动了 N 项"：
      // 切模式本来就会同时改到配方里的宽度/柔度/预设色（它们都是模式的一部分），
      // 钉死数量会在配方演进而非出错时误报。
      bool hasMode = false;
      bool onlyEdge = !diff.empty();
      for (const std::string& name : diff) {
        if (name == "edge.mode") hasMode = true;
        if (name.rfind("edge", 0) != 0) onlyEdge = false;
      }
      Check(hasMode, "切换边光模式一定改到 edge.mode");
      Check(onlyEdge, "切边光模式不会波及边光以外的任何字段");
    }
  }

  // =========================================================================
  // 组 5：Slider 圆钮与轨道几何
  // =========================================================================
  {
    const SliderTune tune;
    const PanelStyle rest = detail::SliderThumbStyle(tune, 0.0f);
    CheckNear(rest.optics.blurPx, px(config::Layout::GLASS_BLUR_DP), 1e-6f,
              "滑块静止模糊 = GLASS_BLUR_DP（原 Slider.cpp:193-194 的同一个 lerp）");
    CheckNear(rest.optics.lensHeightPx, 0.0f, 1e-6f, "滑块静止无折射");
    CheckNear(rest.optics.spectral, 0.0f, 1e-6f, "滑块默认无色散");
    Check(rest.shadowOn, "滑块有外阴影");
    CheckNear(rest.shadow.radiusDp, config::Layout::SLIDER_SHADOW_DP, 1e-6f, "滑块阴影半径 4dp");
    CheckNear(rest.shadow.offsetYDp, config::Layout::SLIDER_SHADOW_DP / 6.0f, 1e-6f,
              "滑块阴影偏移 Y = 半径/6");
    Check(rest.insetOn == false, "静止时内阴影关闭（原 press > ALPHA_MIN）");
    const PanelStyle held = detail::SliderThumbStyle(tune, 1.0f);
    CheckNear(held.optics.lensHeightPx, px(config::Layout::SLIDER_LENS_HEIGHT_DP), 1e-6f,
              "滑块按压折射高度 12dp");
    CheckNear(held.optics.lensAmountPx, px(config::Layout::SLIDER_LENS_AMOUNT_DP), 1e-6f,
              "滑块按压折射强度 14dp");
    Check(held.insetOn, "按压时内阴影打开");
    // ★ dp / px 的分界：InsetShadow 收的是 **dp**（GlassPanel 在 appendJob 里
    //   自己乘 densityScale），所以这里不能套 lgx::px()。套了就会得到 2 倍。
    CheckNear(held.inset.radiusDp, config::Layout::SLIDER_INNER_DP, 1e-6f,
              "按压内阴影半径 = SLIDER_INNER_DP（dp，不再乘密度）");

    // 轨道几何：默认与原来写死的完全一致
    const Box box(0.0f, 0.0f, 400.0f, 48.0f);   // 高 48px ⇒ 轨道 12px 居中
    const detail::SliderTrackVisual half =
        detail::SliderTrackLayout(tune, box, 0.5f, 0x11111111u, 0xFF0091FFu);
    CheckNear(half.rail.top, (48.0f - px(config::Layout::SLIDER_TRACK_H_DP)) * 0.5f, 1e-6f,
              "轨道垂直居中");
    CheckNear(half.railRadius, px(config::Layout::SLIDER_TRACK_H_DP) * 0.5f, 1e-6f,
              "轨道圆角 = 高度一半（胶囊）");
    Check(half.fillVisible, "进度 0.5 ⇒ 有填充");
    CheckNear(half.fill.right, 200.0f, 1e-6f, "填充宽度 = 宽度 × 进度");
    CheckNear(half.fillRadius, px(config::Layout::SLIDER_TRACK_H_DP) * 0.5f, 1e-6f,
              "填充圆角 = 高度一半");
    CheckNear(half.railColor, 0x11111111u, 1e-6f, "轨道色透传");
    CheckNear(half.fillColor, 0xFF0091FFu, 1e-6f, "填充色透传");
    const detail::SliderTrackVisual zero =
        detail::SliderTrackLayout(tune, box, 0.0f, 0x11111111u, 0xFF0091FFu);
    Check(!zero.fillVisible, "进度 0 ⇒ 不画填充（原 fillWidth > 0.5 判据）");

    // 改轨道高度 ⇒ 只有轨道的几何跟随
    SliderTune tall = tune;
    tall.trackHeightDp = 16.0f;
    const detail::SliderTrackVisual tallTrack =
        detail::SliderTrackLayout(tall, box, 0.5f, 0x11111111u, 0xFF0091FFu);
    CheckNear(tallTrack.railRadius, px(16.0f) * 0.5f, 1e-6f, "轨道高度参数真的改到几何");
  }

  // =========================================================================
  // 组 6：导航栏（背景层 / 指示器层）
  // =========================================================================
  {
    const TabStripTune tune;
    const PanelStyle background =
        detail::TabNavigationBackgroundStyle(tune, 0x66121212u, 0.0f, 0.0f, 0.0f, 700.0f, 128.0f);
    CheckNear(background.optics.blurPx, px(config::Layout::TAB_BLUR_DP), 1e-6f,
              "导航栏背景模糊 8dp");
    CheckNear(background.optics.lensHeightPx, px(config::Layout::TAB_LENS_HEIGHT_DP), 1e-6f,
              "导航栏折射高度 24dp");
    CheckNear(background.optics.lensAmountPx, px(config::Layout::TAB_LENS_AMOUNT_DP), 1e-6f,
              "导航栏折射强度 24dp");
    Check(background.optics.colorBoost, "导航栏背景 colorBoost = true");
    Check(background.shadowOn, "导航栏有投影");
    CheckNear(background.shadow.radiusDp, 24.0f, 1e-6f, "投影取 DropShadow 默认半径");
    Check(!background.glow.on, "没有按压时不提交高光斑");

    const PanelStyle withHalo = detail::TabNavigationBackgroundStyle(
        tune, 0x66121212u, 0.5f, 123.0f, 32.0f, 700.0f, 128.0f);
    Check(withHalo.glow.on, "有按压时机提交高光斑");
    CheckNear(withHalo.glow.progress, 0.5f, 1e-6f, "高光斑强度");
    CheckNear(withHalo.glow.x, 123.0f, 1e-6f, "高光斑位置 X");
    CheckNear(withHalo.glow.y, 32.0f, 1e-6f, "高光斑位置 Y");

    const PanelStyle idle = detail::TabNavigationIndicatorStyle(tune, 0.0f, false);
    CheckNear(idle.optics.lensHeightPx, 0.0f, 1e-6f, "指示器静止无折射");
    CheckNear(idle.optics.spectral, config::Layout::TAB_SPECTRAL, 1e-6f, "指示器色散常量");
    Check(idle.edgeOn == false, "指示器静止时边光关闭（press > ALPHA_MIN）");
    const PanelStyle pressed = detail::TabNavigationIndicatorStyle(tune, 1.0f, false);
    CheckNear(pressed.optics.lensHeightPx, px(config::Layout::TAB_PRESS_LENS_HEIGHT_DP), 1e-6f,
              "指示器按压折射高度");
    Check(pressed.edgeOn, "指示器按压时边光打开");
    CheckNear(pressed.edge.alpha, 1.0f, 1e-6f, "指示器边光 alpha = press");
    // ★ 导航栏这一处原实现**确实**套了 lgx::px()（TabBar.cpp 的
    //   InsetShadow(px(TAB_PRESS_INNER_DP) * press, press)）—— 与 Slider 那处不同。
    //   这正说明"dp/px 换算要逐处核对调用点"，不能一刀切。
    CheckNear(pressed.inset.radiusDp, px(config::Layout::TAB_PRESS_INNER_DP), 1e-6f,
              "指示器按压内阴影半径 = px(TAB_PRESS_INNER_DP)");

    // 改一个参数 ⇒ 只有那一项
    TabStripTune changed = tune;
    changed.pressLensAmountDp = 40.0f;
    CheckDiff(pressed, detail::TabNavigationIndicatorStyle(changed, 1.0f, false),
              {"optics.lensAmountPx"}, "指示器折射强度只改 lensAmountPx");
    TabStripTune spectralOff = tune;
    spectralOff.spectral = 0.0f;
    CheckDiff(idle, detail::TabNavigationIndicatorStyle(spectralOff, 0.0f, false),
              {"optics.spectral"}, "指示器色散只改 spectral");
  }

  std::printf("GlassRecipeTest: %d 项断言通过（6 组）\n", g_checks);
  return 0;
}
