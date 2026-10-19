#pragma once

// ===========================================================================
// M-09 动效实验场 —— 让「控制中心进出场」在真机上有**可见形态**
//
// 背景（见 research/amdl_native_implementation_finding.md）：
//   AMDL 的 M-10 ~ M-15 六条动效**本来就已在 ytbl 里原生实现**，本项目里它们
//   失效的唯一原因是宿主的一个 ImGui 输入涓流 bug（已修）。但 **M-09 是例外** ——
//   ytbl 里没有任何「控制中心」形态的界面，所以它是唯一真正缺实现的一条。
//
//   本文件把 ControlCenter.h（按 ControlCenterContent.kt 逐行移植、9 组断言真机通过）
//   接到一个**可交互的玻璃面板**上，让它从「已验证的数学」变成「看得见的效果」。
//
// 为什么用 lgx::drawPanel() 而不是自己画：
//   玻璃渲染（折射/模糊/色散/边光）是本项目最不该重写的部分，而且 ytbl 的
//   `drawPanel` 是**公开 API**（GlassUI.h:368）。用它 = 玻璃外观与其它组件
//   完全一致，我只负责几何与动效 —— 这也正是 M-09 的本体。
//
// 交互设计（照 ControlCenterContent.kt 的手势语义）：
//   · 从顶部往下拖 → 展开；往上拖 → 收起
//   · 松手时按**速度方向**决定去留（速度优先，速度为零才看是否过半）
//   · 展开用 ζ=0.5 的欠阻尼弹簧（会过冲、有回弹感）
//     收起用 ζ=1.0 的临界阻尼（干脆利落，不过冲）
//   · 过拖时：面板缩小、行距撑开、越界位移用 exp 饱和
//   · 观感量（透明度/遮罩/模糊）跟 safeProgress，几何量跟 raw progress
//     —— 这条分工是 M-09 最容易做错的地方，原因见 ControlCenter.h 文件头
// ===========================================================================

#include "ControlCenter.h"
#include "ytbl/GlassUI.h"

#include <imgui.h>
#include <algorithm>
#include <cmath>

namespace aurora {

// ---- 实验场自身的可调参数（都暴露给调试面板）----
struct MotionLabTune {
  bool enabled = true;
  // 默认 360dp（density=2 ⇒ 720px）。主题窗口内宽约 830px，
  // 留出余量免得面板永远顶到夹取上限、看着发挤。
  float panelWidthDp = 360.0f;
  // ★ 高度默认 300dp（600px）而不是 520dp —— 真机踩出来的：
  //   舞台高 = 面板高 + 60dp，面板 520dp 时舞台 1160px，加上上方那排控件
  //   总共约 2000px，而主题窗口只有 1400px ⇒ **舞台整个落在折叠线以下，看不到**。
  //   改成 300dp 后「控件 + 舞台」约 1400px，正好在窗口内。
  float panelHeightDp = 300.0f;
  float cornerDp = 28.0f;         // 圆角（与用户源码 Dialog 一致）
  float lensHeightDp = 12.0f;
  float lensAmountDp = 24.0f;
  float spectral = 0.6f;
  float scrimAlpha = kControlCenterScrimAlpha;  // 0.4，源码 :65
  bool showDiagnostics = true;    // 面板上叠一行状态文字
  // 界面语言。自己带一份，不去读 secondary_imgui.cpp 里的内部 static
  // （那个是文件内 static，没有对外访问器）。
  bool chinese = true;
};

inline MotionLabTune& motionLabTune() {
  static MotionLabTune tune;
  return tune;
}

// 按 MotionLabTune::chinese 选中/英文文案。
inline const char* T(const char* zh, const char* en) {
  return motionLabTune().chinese ? zh : en;
}

namespace motionlab_detail {

// 手势状态。放在函数外，保证跨帧保持。
struct LabState {
  ControlCenterState cc;          // M-09 的两条进度动画
  bool dragging = false;
  float dragStartY = 0.0f;
  float dragStartProgress = 1.0f; // 按下那一刻的进动值（决定拖动是加还是减）
  float lastY = 0.0f;
  float lastTime = 0.0f;
  float velocity = 0.0f;          // 进度/秒
};
inline LabState& state() {
  static LabState s;
  return s;
}

inline float nowSeconds() {
  return static_cast<float>(ImGui::GetTime());
}

}  // namespace motionlab_detail

// ---- 给调试面板用的三个命令 ----

// 瞬移回初始态（完全展开），不带任何动画 —— 「重置」按钮用。
inline void MotionLabReset() {
  motionlab_detail::LabState& st = motionlab_detail::state();
  st.cc.snapTo(1.0f);
  st.dragging = false;
  st.velocity = 0.0f;
}

// 走一次真实的展开/收起动画 —— 用来在不方便拖拽时观察弹簧行为。
//   展开：ζ=0.5，会过冲回弹
//   收起：ζ=1.0，干脆利落
// 速度给一个小的非零值，让 target 的选择走「速度优先」那条分支。
inline void MotionLabSetExpanded(bool expanded) {
  motionlab_detail::LabState& st = motionlab_detail::state();
  st.dragging = false;
  st.cc.release(expanded ? 1.0f : -1.0f, kControlCenterMaxDragHeight);
}

// 在 ImGui 帧内调用。会吃掉自己那块舞台区域的输入。
//
// ★ 为什么它需要**独占一个标签页**（真机踩出来的）：
//   ImGui 的窗口**不支持「拖拽滚动」** —— 只有滚轮和滚动条能滚。而这个面板
//   非常密集，几乎每处按下都会命中某个控件（滑块会被拖走），所以靠触摸滑动
//   既滚不动、又会误改参数。结果就是：只要实验场排在其它控件后面，它就永远
//   在折叠线以下、看不到也拖不到。
//   ⇒ 放进独立标签页（`LiquidGlassRuntime.cpp` 的「动效实验场」页），
//     它就能独占窗口的全部高度。这个函数只画舞台本身。
inline void DrawMotionLab() {
  using namespace motionlab_detail;
  MotionLabTune& tune = motionLabTune();
  LabState& st = state();

  if (!tune.enabled) return;

  const float dt = std::clamp(ImGui::GetIO().DeltaTime, 1.0f / 240.0f, 0.05f);
  const float pxPerDp = lgx::px(1.0f);
  const float panelW = lgx::px(tune.panelWidthDp);
  const float panelH = lgx::px(tune.panelHeightDp);

  // 实验场占用的屏幕区域：一块固定高度的「舞台」，面板从舞台顶部往下长出来。
  //
  // ★ 必须夹到窗口可用宽度以内。
  //   曾经写成 `max(panelW + 40dp, GetContentRegionAvail().x)` —— 而
  //   panelW = lgx::px(420) = 840（density=2），于是 stageW = max(920, 830) = 920，
  //   **比主题窗口（860 宽）还宽**，会溢出并顶出横向滚动条。
  //   现在先夹宽度再定高度，保证舞台永远在窗口内。
  const ImVec2 stageMin = ImGui::GetCursorScreenPos();
  const float availW = std::max(ImGui::GetContentRegionAvail().x, lgx::px(80.0f));
  const float wantedW = panelW + lgx::px(40.0f);
  const float stageW = std::min(wantedW, availW);
  // 舞台高度 = 面板高 + 一点余量。留 30dp 而不是 60dp —— 见 panelHeightDp 的注释：
  // 这个高度直接决定「控件 + 舞台」能不能塞进主题窗口，留太多就会把舞台顶到折叠线以下。
  const float stageH = panelH + lgx::px(30.0f);
  const ImVec2 stageMax(stageMin.x + stageW, stageMin.y + stageH);

  // 面板本身也不能比舞台宽（否则左右会被裁掉，看着像 bug）
  const float drawPanelW = std::min(panelW, stageW - lgx::px(8.0f));
  const float drawPanelH = panelH;

  ImGui::InvisibleButton("##motion_lab_stage", ImVec2(stageW, stageH));
  const bool hovered = ImGui::IsItemHovered();
  const bool active = ImGui::IsItemActive();

  // ---------------- 手势 → M-09 状态机 ----------------
  const ImVec2 mouse = ImGui::GetIO().MousePos;
  const float maxDragHeight = kControlCenterMaxDragHeight;

  if (active && !st.dragging) {
    st.dragging = true;
    st.dragStartY = mouse.y;
    st.dragStartProgress = st.cc.enterProgress();   // ★ 从当前进度起拖，不是从 0
    st.lastY = mouse.y;
    st.lastTime = nowSeconds();
    st.velocity = 0.0f;
  }

  if (st.dragging) {
    if (active) {
      // 往下拖 → 进度增加。用「相对拖动起点的位移 / 舞台高」当作原始进度，
      // 这样手感与源码里 `enterProgressAnimation` 被手势直接驱动的语义一致。
      const float dy = mouse.y - st.dragStartY;
      // 起点是「已展开」还是「已收起」，决定拖动是加还是减
      const float raw = st.dragStartProgress + dy / std::max(stageH, 1.0f);
      st.cc.setDragProgress(raw);

      const float now = nowSeconds();
      const float dts = std::max(now - st.lastTime, 1e-4f);
      // 进度速度，再按 maxDragHeight 归一化 —— 对应源码 `velocity / maxDragHeight`
      st.velocity = ((mouse.y - st.lastY) / std::max(stageH, 1.0f)) / dts;
      st.lastY = mouse.y;
      st.lastTime = now;
    } else {
      st.dragging = false;
      st.cc.release(st.velocity, maxDragHeight);
    }
  }

  st.cc.tick(dt);

  // ---------------- 由 M-09 算出这一帧的几何/观感 ----------------
  //
  // ★ 几何计算走的是 ControlCenter.h 里的**纯函数** ComputeControlCenterLayout，
  //   它不碰 ImGui / ytbl / 全局状态，所以能在 PC 上离线严格验证
  //   （见 tests/ControlCenterTest.cpp 第 10 组）。这里只负责把结果喂给绘制。
  const float raw = st.cc.progress();
  const float safe = st.cc.safeProgress();
  const ControlCenterVisual v = ComputeControlCenterVisual(raw, safe, pxPerDp);
  const ControlCenterLayout layout =
      ComputeControlCenterLayout(raw, safe, pxPerDp, stageMin.x, stageMin.y, stageW, stageH,
                                 drawPanelW, drawPanelH, lgx::px(10.0f));
  const lgx::Box panelBox(layout.left, layout.top, layout.right, layout.bottom);

  // ---------------- 画：遮罩 ----------------
  ImDrawList* dl = ImGui::GetWindowDrawList();
  if (dl != nullptr && v.scrimAlpha > 0.002f) {
    const int a = static_cast<int>(std::clamp(v.scrimAlpha * tune.scrimAlpha / kControlCenterScrimAlpha,
                                             0.0f, 1.0f) * 255.0f + 0.5f);
    dl->AddRectFilled(stageMin, stageMax, IM_COL32(0, 0, 0, a));
  }

  // ---------------- 画：控制中心面板 ----------------
  // 几何与观感全部来自上面那个纯函数，这里不再自己算任何位置。
  if (v.alpha > 0.004f) {
    lgx::PanelStyle style;
    style.corners = lgx::Corners::rounded(lgx::px(tune.cornerDp));
    style.optics.blurPx = v.blurPx;                       // 源码 :197  4dp * safeProgress
    style.optics.lensHeightPx = lgx::px(tune.lensHeightDp);
    style.optics.lensAmountPx = lgx::px(tune.lensAmountDp);
    style.optics.spectral = tune.spectral;
    style.optics.colorBoost = true;
    style.edgeOn = true;
    style.edge = lgx::EdgeLight::gradient();
    style.shadowOn = true;
    style.alpha = v.alpha;
    style.fallback = lgx::Rgba(0.0f, 0.0f, 0.0f, 0.0f);
    style.surface = lgx::Rgba(1.0f, 1.0f, 1.0f, 0.10f);
    style.surfaceMode = lgx::SurfaceMode::Overlay;
    lgx::drawPanel(style, panelBox);
  }

  // ---------------- 画：面板内部的「格子」 ----------------
  // 行距用 v.rowSpacingPx —— 源码 :141 的 `16dp * max(progress-1, 0)`，
  // 也就是**只有过拖时行距才撑开**，这是 M-09 一个容易被漏掉的细节。
  if (v.alpha > 0.05f) {
    const int cols = 2;
    const int rows = 4;
    const float gap = lgx::px(12.0f) + v.rowSpacingPx * 0.25f;
    const float cellW = (panelBox.right - panelBox.left - gap * (cols + 1)) / cols;
    const float cellH = lgx::px(60.0f) + v.rowSpacingPx * 0.5f;
    const int cellAlpha = static_cast<int>(std::clamp(v.alpha, 0.0f, 1.0f) * 255.0f + 0.5f);
    for (int r = 0; r < rows; ++r) {
      for (int c = 0; c < cols; ++c) {
        const float x0 = panelBox.left + gap + c * (cellW + gap);
        const float y0 = panelBox.top + gap + r * (cellH + gap);
        if (y0 + cellH > panelBox.bottom - gap) continue;
        lgx::PanelStyle cell;
        cell.corners = lgx::Corners::rounded(lgx::px(18.0f));
        cell.optics.blurPx = v.blurPx * 0.5f;
        cell.edgeOn = false;
        cell.shadowOn = false;
        cell.alpha = v.alpha * 0.9f;
        cell.fallback = lgx::Rgba(0.0f, 0.0f, 0.0f, 0.0f);
        cell.surface = lgx::Rgba(1.0f, 1.0f, 1.0f, 0.14f);
        cell.surfaceMode = lgx::SurfaceMode::Overlay;
        lgx::drawPanel(cell, lgx::Box(x0, y0, x0 + cellW, y0 + cellH));
      }
    }
    (void)cellAlpha;
  }

  // ---------------- 状态文字 ----------------
  if (tune.showDiagnostics) {
    ImGui::SetCursorScreenPos(ImVec2(stageMin.x + lgx::px(6.0f),
                                     stageMax.y + lgx::px(2.0f)));
    ImGui::Text("M-09  raw=%.3f  safe=%.3f  transY=%.1f  scale/%.3f  scrim=%.3f  blur=%.1f  row=%.1f%s",
                static_cast<double>(raw), static_cast<double>(safe),
                static_cast<double>(v.translationY), static_cast<double>(v.scaleDivisor),
                static_cast<double>(v.scrimAlpha), static_cast<double>(v.blurPx),
                static_cast<double>(v.rowSpacingPx),
                st.dragging ? "  [拖拽中]" : (hovered ? "  [可拖拽]" : ""));
  }
}

// ===========================================================================
// 实验场的控件面板 + 舞台 —— 供**独立标签页**调用（见 DrawMotionLab 的说明）
//
// 抽成单独的函数是因为：控件 + 舞台加起来约 1400px，塞在「液态玻璃」标签页里
// 会被前面的上百个控件挤到折叠线以下，而 ImGui 又不支持拖拽滚动。
// ===========================================================================
inline void DrawMotionLabTab() {
  MotionLabTune& tune = motionLabTune();
  ImGui::PushID("motion_lab_tab");

  ImGui::Checkbox(T("启用实验场", "Enable lab"), &tune.enabled);
  ImGui::SameLine();
  if (ImGui::Button(T("重置", "Reset"))) {
    MotionLabReset();
  }
  ImGui::SameLine();
  if (ImGui::Button(T("收起", "Collapse"))) {
    MotionLabSetExpanded(false);
  }
  ImGui::SameLine();
  if (ImGui::Button(T("展开", "Expand"))) {
    MotionLabSetExpanded(true);
  }

  ImGui::SliderFloat(T("面板宽 dp", "Panel width dp"), &tune.panelWidthDp, 200.0f, 600.0f,
                     "%.0f");
  ImGui::SliderFloat(T("面板高 dp", "Panel height dp"), &tune.panelHeightDp, 200.0f, 900.0f,
                     "%.0f");
  ImGui::SliderFloat(T("圆角 dp", "Corner dp"), &tune.cornerDp, 0.0f, 60.0f, "%.0f");
  ImGui::SliderFloat(T("折射高度 dp", "Lens height dp"), &tune.lensHeightDp, 0.0f, 40.0f,
                     "%.0f");
  ImGui::SliderFloat(T("折射强度 dp", "Lens amount dp"), &tune.lensAmountDp, 0.0f, 60.0f,
                     "%.0f");
  ImGui::SliderFloat(T("色散强度", "Dispersion"), &tune.spectral, 0.0f, 2.0f, "%.2f");
  ImGui::SliderFloat(T("遮罩浓度", "Scrim alpha"), &tune.scrimAlpha, 0.0f, 1.0f, "%.2f");
  ImGui::Checkbox(T("显示状态数字", "Show diagnostics"), &tune.showDiagnostics);

  ImGui::TextWrapped("%s",
                     T("在下面的舞台上往下拖 = 展开，往上拖 = 收起；松手按速度决定去留"
                           "（速度优先）。展开用 ζ=0.5（会过冲回弹），收起用 ζ=1.0（干脆利落）。",
                           "Drag down on the stage below to expand, up to collapse. Release "
                           "decides by velocity first. Expand uses zeta=0.5 (overshoots, "
                           "bouncy); collapse uses zeta=1.0."));

  DrawMotionLab();
  ImGui::PopID();
}

}  // namespace aurora
