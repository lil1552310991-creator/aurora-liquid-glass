// =============================================================================
// UiLayoutTest —— 界面布局的离线回归。
//
// 为什么要有这个测试（用户反馈）：
//   1) 横竖屏切换后窗口"飘到屏幕外面"；
//   2) 横屏启动时主题窗口太长，底部够不到，调不了参数。
//
// 根因是位置/尺寸写死成 760x1400 + (16,730)。竖屏 1080x2376 刚好够，
// 横屏 2376x1080 时 730+1400 直接甩出屏幕 700 多像素。
//
// 这个测试把"必须成立的性质"逐条钉住，而不是比对某组魔数：
//   · 两个窗口都完整落在屏幕内（任何朝向、任何分辨率）
//   · 两个窗口互不重叠
//   · 横屏左右并排、竖屏上下堆叠
//   · 屏幕够大时用理想尺寸，不够时按比例压缩而不是裁剪
//   · 只有屏幕尺寸真的变了才要求重新布局
//
// 纯逻辑，不需要设备、不需要 ImGui。
// =============================================================================

#include "UiLayout.h"

#include <cassert>
#include <cstdio>
#include <vector>

using aurora::ui_layout::Compute;
using aurora::ui_layout::Input;
using aurora::ui_layout::Layout;
using aurora::ui_layout::NeedsRelayout;
using aurora::ui_layout::Rect;

namespace {

bool Inside(const Rect& r, float w, float h) {
  constexpr float kEps = 0.5f;
  return r.x >= -kEps && r.y >= -kEps && r.right() <= w + kEps && r.bottom() <= h + kEps;
}

bool Overlaps(const Rect& a, const Rect& b) {
  constexpr float kEps = 0.5f;
  const bool separatedX = a.right() <= b.x + kEps || b.right() <= a.x + kEps;
  const bool separatedY = a.bottom() <= b.y + kEps || b.bottom() <= a.y + kEps;
  return !separatedX && !separatedY;
}

}  // namespace

int main() {
  // ── 真实屏幕尺寸（来自项目里的实测记录）────────────────────────────────
  struct Screen {
    float w;
    float h;
    const char* why;
  };
  const std::vector<Screen> screens = {
      {1080.0f, 2376.0f, "手机竖屏（2K 模式降采样）"},
      {2376.0f, 1080.0f, "手机横屏"},
      {1440.0f, 3168.0f, "手机竖屏（全分辨率）"},
      {3168.0f, 1440.0f, "手机横屏（全分辨率）"},
      {2400.0f, 3392.0f, "平板竖屏"},
      {3392.0f, 2400.0f, "平板横屏"},
      {1920.0f, 1080.0f, "1080p 横屏"},
      {720.0f, 1280.0f, "720p 竖屏"},
      {1280.0f, 720.0f, "720p 横屏"},
  };

  for (const Screen& s : screens) {
    Input in;
    in.screenW = s.w;
    in.screenH = s.h;
    const Layout layout = Compute(in);

    // 1) 两个窗口都必须有效
    assert(layout.secondary.Valid());
    assert(layout.theme.Valid());

    // 2) 都必须完整落在屏幕内 —— 这正是用户报的"飘到外面"
    assert(Inside(layout.secondary, s.w, s.h));
    assert(Inside(layout.theme, s.w, s.h));

    // 3) 不能互相压住
    assert(!Overlaps(layout.secondary, layout.theme));

    // 4) 朝向决定摆法
    const bool expectHorizontal = s.w > s.h;
    assert(layout.horizontal == expectHorizontal);
    if (expectHorizontal) {
      // 左右并排：主题窗口在右，且高度吃满
      assert(layout.theme.x > layout.secondary.x);
      assert(layout.theme.h == layout.secondary.h);
    } else {
      // 上下堆叠：主题窗口在下，且宽度对齐
      assert(layout.theme.y > layout.secondary.y);
      assert(layout.theme.w == layout.secondary.w);
    }
  }

  // ── 横屏时的主题窗口必须"够得着底部" ──────────────────────────────────
  // 这是用户最痛的那条：横屏 1080 高、窗口高 1400 ⇒ 底部永远点不到。
  {
    Input in;
    in.screenW = 2376.0f;
    in.screenH = 1080.0f;
    const Layout layout = Compute(in);
    assert(layout.theme.bottom() <= 1080.0f);
    // 至少占可用高度的 60%，否则调试面板没法用
    const float availH = 1080.0f - 32.0f;
    assert(layout.theme.h >= availH * 0.6f);
  }

  // ── 竖屏上下堆叠时，主题窗口也要占大头 ────────────────────────────────
  {
    Input in;
    in.screenW = 1080.0f;
    in.screenH = 2376.0f;
    const Layout layout = Compute(in);
    const float availH = 2376.0f - 32.0f;
    assert(layout.theme.h >= availH * 0.5f);
  }

  // ── 大屏用理想尺寸，不无脑铺满 ────────────────────────────────────────
  {
    Input in;
    in.screenW = 3392.0f;
    in.screenH = 2400.0f;
    const Layout layout = Compute(in);
    // 横屏时二级窗口宽度封顶在理想值附近，不会占掉 42% 的一大块
    assert(layout.secondary.w <= 760.0f + 0.5f);
  }

  // ── 极端尺寸不能算出负数或零 ──────────────────────────────────────────
  {
    Input tiny;
    tiny.screenW = 320.0f;
    tiny.screenH = 240.0f;
    const Layout layout = Compute(tiny);
    assert(layout.secondary.Valid());
    assert(layout.theme.Valid());
    assert(Inside(layout.secondary, 320.0f, 240.0f));
    assert(Inside(layout.theme, 320.0f, 240.0f));
  }
  {
    // 非法输入必须安全返回（不能除零、不能给负矩形）
    Input bad;
    bad.screenW = 0.0f;
    bad.screenH = 0.0f;
    const Layout layout = Compute(bad);
    assert(!layout.secondary.Valid());
    assert(!layout.theme.Valid());
  }

  // ── 重新布局的触发条件 ────────────────────────────────────────────────
  {
    // 首次必然要布局
    assert(NeedsRelayout(0.0f, 0.0f, 1080.0f, 2376.0f));
    // 尺寸没变（像素级抖动）不重置，避免每帧覆盖用户调好的窗口
    assert(!NeedsRelayout(1080.0f, 2376.0f, 1080.0f, 2376.0f));
    assert(!NeedsRelayout(1080.0f, 2376.0f, 1081.0f, 2375.0f));
    // 横竖屏切换必须触发
    assert(NeedsRelayout(1080.0f, 2376.0f, 2376.0f, 1080.0f));
    // 只变一个方向也要触发（分屏、折叠屏展开）
    assert(NeedsRelayout(1080.0f, 2376.0f, 1080.0f, 1200.0f));
  }

  std::puts("UiLayoutTest: 9 screens + 8 property groups passed");
}
