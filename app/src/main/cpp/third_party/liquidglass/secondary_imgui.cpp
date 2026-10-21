#include "../../AuroraLog.h"
#include "secondary_imgui.h"
#include "glass/DebugHighlight.h"
#include "UiLayout.h"

#include "ytbl/GlassUI.h"
#include "PanelGestures.h"        // 手势内核（纯逻辑，离线回归见 tests/PanelGesturesTest.cpp）
#include "ResizeGesture.h"        // 右下角长按缩放内核（回归 tests/ResizeGestureTest.cpp）
#include "CornerResizeHandle.h"   // 角部缩放手柄的几何/动画 + 边框激活反馈
#include "Haptics.h"              // 触觉反馈（纯逻辑层；平台实现在宿主 ELF 注册）
#include "CornerResizeOverlay.h"  // ★ 右下角长按缩放（与主窗口共用同一套接线）
#include "ImeAvoid.h"             // ★ 输入法弹层避让（纯逻辑，有离线回归）
#include "GlassDemoDerive.h"      // ★ Demo 派生量公式 + 同行判据（纯逻辑，有离线回归）

// M-14 / M-15 的动画数学（纯逻辑模块，已由 tests/ 下的单测真机验证过）。
// 它们只依赖 Motion.h（Compose 语义弹簧），不依赖 ImGui / ytbl。
#include "LiquidMorph.h"
#include "TouchHighlight.h"
#include "PanelCallbackBatch.h"
#include "PanelEfficiencyStats.h"

// ★★ Liquid Glass Inspector：场景参数 + 组件注册表。
//   本文件只负责 ImGui 侧的布局/手势/触摸形状；所有玻璃参数与组件实例都住在
//   aurora::glass::Scene()（glass/GlassScene.h），由 GlassTargets.cpp 登记成
//   可读写的调试参数。这样"参数存储 → PanelStyle → shader"这条链可以在
//   宿主机上离线验证（tests/GlassRecipeTest.cpp）。
#include "glass/GlassScene.h"
#include "glass/GlassRegistry.h"
#include "GlassTargets.h"

#include <imgui.h>
#include "backends/imgui_impl_opengl3.h"   // 圆角内容遮罩接口（Aurora 增量）
#include <imgui_internal.h>
#include <math.h>
#include <stdint.h>
#include <android/log.h>   // 手势状态转换日志（tag 与主程序一致，便于 logcat 过滤）
#include <atomic>
#include <algorithm>
#include <chrono>
#include <cstdarg>
#include <mutex>
#include <cstdlib>

namespace secondary_imgui {
namespace {

static bool s_Chinese = false;

// 环境开关（诊断用）：值为 "1" 视为打开。见窗口玻璃那段的逐层隔离注释。
static bool EnvOn(const char* name) {
  const char* v = std::getenv(name);
  return v != nullptr && v[0] == '1';
}

// ★ 窗口圆角半径只有这一个来源：外壳玻璃、内容圆角遮罩、缩放手柄三者共用。
//   （原来三处各写一遍 lgx::px(28.0f) —— 手柄与外壳一旦不同步就会"看起来没贴住"。）
// ★ 窗口圆角半径只有这一个来源：外壳玻璃、内容圆角遮罩、缩放手柄三者共用。
//   （原来三处各写一遍 lgx::px(28.0f) 与 kWindowCornerDp 常量 —— 手柄与外壳一旦
//    不同步就会"看起来没贴住"。现在这个值住在场景参数里（工作台可调），
//    三处仍然读同一个来源。）
static float WindowCornerRadiusPx() {
  return lgx::px(aurora::glass::Scene().window.cornerDp);
}
static std::atomic<bool> s_Visible{false};
static HostInfo s_HostInfo;
static bool s_WantsTextInput = false;
static char s_ImeText[256] = {};
static float s_TouchRect[4] = {0.0f, 0.0f, 0.0f, 0.0f};
static std::mutex s_TouchRectMutex;
static std::uint32_t s_TouchWindowId = 0;
static std::vector<aurora::VisibleUiShape> s_TouchShapes;
// Modal scrims can cover otherwise transparent glass corners. Capture their
// actual fills with the current drawlist clip; shadows remain pass-through.
static bool s_RecordOverlayTouchShapes = false;


static float s_Anchor[4] = {0.0f, 0.0f, 0.0f, 0.0f};
static bool s_AnchorValid = false;
static std::mutex s_AnchorMutex;

// ★★ 参数与组件实例已经搬到 aurora::glass::Scene()（glass/GlassScene.h）。
//
// 为什么搬：调试工作台要绑定到"渲染实现真正读的那个字段"。如果这些参数继续以
// static 的形式埋在本文件里（3000+ 行、依赖 ImGui），参数绑定层就没法在宿主机上
// 编译，也就没法做"改了参数 → 渲染输入真的变了"的离线回归。
//
// 这次搬迁顺带修掉一个结构性缺陷：原来 5 枚 PillButton 共用一组 static 变量
// （s_ButtonBlurDp 等）⇒「调按钮 1 会改到按钮 2」是必然的。现在每枚按钮、
// 每个开关、每根滑块、每块芯片都有**自己的** tune。
static aurora::glass::GlassScene& Scene() { return aurora::glass::Scene(); }

// ★ 下面这些是**交互状态**，不是玻璃参数 —— 它们描述"手指此刻在干什么"，
//   不属于调试工作台的范围（工作台调的是"玻璃长什么样"）。
//   玻璃参数全部在 Scene() 里。
// 下面这三个仍被 SetVisible() 与手势层使用（隐藏窗口时清掉拖拽/缩放状态），
// 与"组件演示"无关，保留。
static bool s_PlaygroundDragging = false;
static bool s_PlaygroundResizing = false;
// 组件演示（PillButton / Toggle / Slider / PopMenu / TabStrip / 芯片 / 卡片 / 实验场）
// 的全部状态变量与配色表都已随演示代码删除。

static const char* Text(const char* chinese, const char* english) {
  return s_Chinese ? chinese : english;
}

static ImU32 Color(unsigned int argb) {
  return IM_COL32((argb >> 16) & 0xffu, (argb >> 8) & 0xffu,
                  argb & 0xffu, (argb >> 24) & 0xffu);
}
// =============================================================================
// ★ 组件级调试参数（每个成品组件 × 每一种效果）
//
// 对应 docs/KYANT_BACKDROP_PARAM_SURFACE.md 第三部分「★ 调试面板的控件清单」。
// 下面这些 static 就是「原来写死在 DrawContents()/DrawGlassChip() 里的量」。
// 默认值只有两条取法，保证不动任何滑杆时外观与改动前逐像素一致：
//   ① ytbl 里有同名常量的直接取该常量（如 BUTTON_BLUR_DP）——
//      与组件内部用的是同一个数，天然一致；
//   ② 没有常量的（DrawContents 里的字面量）照抄原字面量。
// 颜色一律用 Vec4Of(原 ARGB) 初始化，与原代码逐位相同 —— 这一点很关键：
// ytbl 内部有几处「颜色 == 默认常量就切主题自适应分支」的判断
// （Toggle.cpp:134-139、Slider.cpp:159-164、TabBar.cpp:264-267），
// 只有逐位相同，那些分支才会照旧生效。
//
// ★ 实时性：这些量全部在 DrawContents() 里逐帧读取并重建 PanelStyle，
//   而 ytbl 的 appendJob()（GlassPanel.cpp:203-278）是在 submitPanel() 里
//   **同步**求值的，所有 px()/uniform 当场固化进 PanelJob；推迟到绘制回调里
//   跑的 runPanelCapture/runPanelPaint（GlassPanel.cpp:481-507）不再读密度、
//   也不再读这些参数。所以改完下一帧即生效，不需要任何通知机制，
//   也不会触发 shader 重编译（ytbl 的边光/折射分支都在同一个片元着色器里，
//   靠 uEdgeMode 等 uniform 选择，不像 Kyant 那样换 program）。
// =============================================================================
// Vec4Of / ArgbOf（ImVec4 ↔ ARGB）：旧的共用变量面板删掉后已无调用者。
// 场景参数一律用 float[4]，转换在 ArgbOfFloats() 里做（见上）。

// ---- 边光（§3.2 分组 4）-----------------------------------------------------
//
// ★ 三条来自 KYANT 参数面规格的约定（KYANT_BACKDROP_PARAM_SURFACE.md:1506）：
//   ① 没有 spread —— Kyant 与 ytbl 都没有，**不做**这个控件；
//   ② Ambient 的角度/衰减在 shader 里根本不参与运算
//      （Shaders.cpp:410-413 只读 uEdgeAlpha/uEdgeTintAlpha/band），
//      所以 mode==2 时这两项灰显 —— 不做假控件；
//   ③ Ambient 的宽度/柔度默认取 ytbl 的 EDGE_AMBIENT_WIDTH/SOFTNESS
//      （0.3333/0.1667dp，GlassConfig.h:84-85），也就是「组件里用的
//      Ambient.copy(width/1.5, blur/1.5)」那一套，**不是**库默认
//      Highlight.Ambient 的 0.5/0.25dp（差 1.5×）。
// ★★ 这一段原来定义的是本文件自己的 EdgeTune / PanelTune / EdgePreset 与
//    "把参数写进 PanelStyle" 的转换函数。它们已经整体搬到
//    glass/GlassScene.h + glass/GlassScene.cpp（BuildPanelStyle / BuildWindowStyle），
//    理由同上：参数存储与"参数 → PanelStyle"的转换必须能被宿主机离线验证。
//
//    与旧实现的差别（刻意的）：
//      · 去掉了 kTune* 分块掩码 —— 每个目标有自己完整的一份，
//        不再需要"这次只应用形状、不应用光学"这种共享变量时代的补丁；
//      · 光学四项（blur / lens / lensAmount / dispersion）并进同一个 PanelTune，
//        原来它们单独放在 s_BlurDp 之类的变量里、与这里重复；
//      · 每枚芯片、每根开关/滑块的子层都有独立实例。

// ytbl 的 Toggle / Slider / TabStrip 内部尺寸都是编译期常量
// （GlassConfig.h 的 TOGGLE_* / SLIDER_* / TAB_*），没有对应字段，
// 唯一不改 ytbl 就能做到的「尺寸」控制是全局密度比例的等比缩放。
// 这个 RAII 只在一次 draw() 调用期间改密度：
// px()/densityScale() 只在 appendJob()（GlassPanel.cpp:203-278，submitPanel()
// 内部同步执行）与 PillButton.cpp:198 的取字号处被读到，绘制回调
// runPanelCapture/runPanelPaint 里没有 px()/densityScale()，
// 所以缩放会被完整固化成 PanelJob，不会漏到别的组件上。
// 倍率 == 1.0 时完全不调用 setScale ⇒ 默认外观不变。
class ScopedGlassScale {
public:
  explicit ScopedGlassScale(float factor) {
    if (!(factor > 0.0f) || factor == 1.0f) return;
    base_ = lgx::px(1.0f);
    lgx::setScale(base_ * factor);
    active_ = true;
  }
  ~ScopedGlassScale() {
    if (active_) lgx::setScale(base_);
  }
  ScopedGlassScale(const ScopedGlassScale&) = delete;
  ScopedGlassScale& operator=(const ScopedGlassScale&) = delete;

private:
  float base_ = 1.0f;
  bool active_ = false;
};

// 把「液态按键」的参数写进一枚 PillButton 的旧辅助函数已经删除：
// 现在每枚按钮有自己的 PillButton::tune 与字段，在 GlassScene 构造时各自初始化一次，
// 不需要每帧从一组共用变量复制（那正是"调按钮 1 改到按钮 2"的来源）。

// ===========================================================================
// M-14 / M-15 接线 —— 把「触摸点高亮」与「液滴跟随」接到 ytbl 的 PillButton 上
//
// 移植来源（本机 Kotlin 源码逐行核对）：
//   M-14 utils/InteractiveHighlight.kt      → app/src/main/cpp/TouchHighlight.h
//   M-15 components/LiquidButton.kt         → app/src/main/cpp/LiquidMorph.h
// 两者的动画数学已在 tests/TouchHighlightTest.cpp 与 tests/LiquidButtonTest.cpp 里
// 真机验证过；这里只做「接到组件上」。
//
// ★ 三个关键决定，都写清理由：
//
//   1. **按压进度用 ytbl 自己的 `glowProgress()`**，而不是另外造一个。
//      PillButton.cpp:63 是 `state().glow.progress()`，由 `probe.held` 驱动
//      （:147），语义与 M-10 的 pressProgress 一致。用它就不必自己再判一次
//      按下/抬起，也不会有两套状态打架。
//
//   2. **形变写回 Box 几何**，不用 `visualScale`。
//      `visualScale` 是**均匀**缩放，而 M-15 的 scaleX/scaleY 是两个不同的值
//      （还要乘宽高比修正），均匀缩放表达不了。改 Box 则完全等价且不碰 ytbl。
//
//   3. **指针偏移来自 ImGui 的鼠标位置**，喂给 M-14 的状态机拿 offset。
//      M-15 的公式用的就是 M-14 的 `interactiveHighlight.offset`，所以两者
//      必须共用**同一个**状态机实例 —— 这里用 `ButtonHighlight` 把它们绑在一起。
//
// 默认行为：所有 s_Motion* 开关为 true 时启用。把开关关掉即退回 ytbl 原生外观，
// 方便做「接线前/后」对比。
// ===========================================================================

// ★★ 默认**关闭** —— 因为 ytbl 自己已经原生实现了 M-14 与 M-15：
//
//   M-15（液滴跟随）PillButton.cpp:149-177 + GlassConfig.h:409-410
//       scale = lerp(1, 1 + BUTTON_GLOW_RISE_DP/height, press)          ← 与 M-15 逐字一致
//       shift = maxOffset * tanh(BUTTON_DRAG_TANH * offset / maxOffset) ← 含 tanh 饱和
//       scaleX/Y = scale + dragGain*|cos/sin(角)*offset/长边|*min(宽高比,1)
//       BUTTON_GLOW_RISE_DP = 4.0f、BUTTON_DRAG_TANH = 0.05f            ← 正是 M-15 的两个常量
//
//   M-14（手指光斑）Shaders.cpp:439-443
//       spot = smoothstep(uGlowReach, uGlowReach * 0.5, distance(vLocal, uGlowAt))
//       add  = 0.08 * level + 0.15 * level * spot                       ← 两层加法，逐字一致
//
// 所以在 PillButton 上再套一层我们的形变/光斑会**叠加两次**，画面是错的。
// 默认 false；置 true 只用于「用我们的模块替换 ytbl 原生效果」的对照实验。
//
// 这两个模块（LiquidMorph.h / TouchHighlight.h）的价值在于**给没有原生实现的组件用**，
// 以及作为可单测的参考实现（tests/LiquidButtonTest.cpp、tests/TouchHighlightTest.cpp）。
// M-14/M-15 的按钮动效模块（LiquidMorph.h / TouchHighlight.h）原本在这里挂给
// PillButton 使用。PillButton 已随组件演示删除，这层绑定也随之移除；
// 那两个头文件本身仍保留，因为它们有独立的离线用例
// （tests/LiquidButtonTest.cpp、tests/TouchHighlightTest.cpp）。
static void ClearTouchRect() {
  std::lock_guard<std::mutex> lock(s_TouchRectMutex);
  s_TouchRect[0] = 0.0f;
  s_TouchRect[1] = 0.0f;
  s_TouchRect[2] = 0.0f;
  s_TouchRect[3] = 0.0f;
  s_TouchWindowId = 0;
  s_TouchShapes.clear();
}

static void RecordTouchShape(const lgx::Box& box, float radius,
                             std::uint8_t roundedCorners = 0x0fu) {
  if (box.width() <= 0.0f || box.height() <= 0.0f) return;
  aurora::VisibleUiShape shape;
  shape.left = box.left;
  shape.top = box.top;
  shape.right = box.right;
  shape.bottom = box.bottom;
  shape.radius = radius;
  shape.shape = radius > 0.0f ? aurora::VisibleUiShapeKind::RoundedRectangle
                              : aurora::VisibleUiShapeKind::Rectangle;
  shape.roundedCorners = roundedCorners;
  const ImDrawList* draw = ImGui::GetWindowDrawList();
  const ImVec2 clipMin = draw->GetClipRectMin();
  const ImVec2 clipMax = draw->GetClipRectMax();
  shape.clipLeft = clipMin.x;
  shape.clipTop = clipMin.y;
  shape.clipRight = clipMax.x;
  shape.clipBottom = clipMax.y;
  std::lock_guard<std::mutex> lock(s_TouchRectMutex);
  s_TouchShapes.push_back(shape);
}

static void RecordWindowDecorations(ImGuiWindow* window) {
  if ((ImGui::GetColorU32(ImGuiCol_ScrollbarBg) & IM_COL32_A_MASK) != 0u) {
    for (int axis = 0; axis < 2; ++axis) {
      if (axis == 0 ? !window->ScrollbarX : !window->ScrollbarY) continue;
      const ImRect bounds = ImGui::GetWindowScrollbarRect(window, static_cast<ImGuiAxis>(axis));
      std::uint8_t corners = axis == 0 ? 0x08u : 0x04u;
      if (axis == 0 && !window->ScrollbarY) corners |= 0x04u;
      if (axis == 1 && (window->Flags & ImGuiWindowFlags_NoTitleBar) != 0 &&
          (window->Flags & ImGuiWindowFlags_MenuBar) == 0) corners |= 0x02u;
      aurora::VisibleUiShape shape;
      shape.left = bounds.Min.x;
      shape.top = bounds.Min.y;
      shape.right = bounds.Max.x;
      shape.bottom = bounds.Max.y;
      shape.radius = window->WindowRounding;
      shape.shape = aurora::VisibleUiShapeKind::RoundedRectangle;
      shape.roundedCorners = corners;
      shape.clipLeft = window->OuterRectClipped.Min.x;
      shape.clipTop = window->OuterRectClipped.Min.y;
      shape.clipRight = window->OuterRectClipped.Max.x;
      shape.clipBottom = window->OuterRectClipped.Max.y;
      std::lock_guard<std::mutex> lock(s_TouchRectMutex);
      s_TouchShapes.push_back(shape);
    }
  }
  if ((window->Flags & ImGuiWindowFlags_NoResize) != 0) return;

  // Read the real decoration mesh already emitted by Begin(), instead of
  // inventing a square hit target in the transparent glass corner. Only the
  // visible resize-grip triangles at the bottom corners enter this snapshot.
  const ImU32 gripColors[] = {ImGui::GetColorU32(ImGuiCol_ResizeGrip),
                             ImGui::GetColorU32(ImGuiCol_ResizeGripHovered),
                             ImGui::GetColorU32(ImGuiCol_ResizeGripActive)};
  const float gripSize = truncf(fmaxf(ImGui::GetFontSize() * 1.10f,
      window->WindowRounding + 1.0f + ImGui::GetFontSize() * 0.2f));
  const ImDrawList& draw = *window->DrawList;
  for (int index = 0; index + 2 < draw.IdxBuffer.Size; index += 3) {
    const ImDrawVert* verts[] = {&draw.VtxBuffer[draw.IdxBuffer[index]],
                                &draw.VtxBuffer[draw.IdxBuffer[index + 1]],
                                &draw.VtxBuffer[draw.IdxBuffer[index + 2]]};
    const ImU32 color = verts[0]->col;
    if ((color & IM_COL32_A_MASK) == 0u || verts[1]->col != color || verts[2]->col != color ||
        (color != gripColors[0] && color != gripColors[1] && color != gripColors[2])) continue;
    float top = verts[0]->pos.y;
    float bottom = top;
    bool inGrip = true;
    for (const ImDrawVert* vertex : verts) {
      top = fminf(top, vertex->pos.y);
      bottom = fmaxf(bottom, vertex->pos.y);
      const float x = vertex->pos.x;
      inGrip = inGrip && vertex->pos.y >= window->Pos.y + window->Size.y - gripSize - 1.0f &&
          (x <= window->Pos.x + gripSize + 1.0f || x >= window->Pos.x + window->Size.x - gripSize - 1.0f);
    }
    if (!inGrip) continue;
    for (int y = static_cast<int>(floorf(top)); y < static_cast<int>(ceilf(bottom)); ++y) {
      const float sampleY = static_cast<float>(y) + 0.5f;
      float intersections[3];
      int count = 0;
      for (int edge = 0; edge < 3; ++edge) {
        const ImVec2 a = verts[edge]->pos;
        const ImVec2 b = verts[(edge + 1) % 3]->pos;
        if (sampleY < fminf(a.y, b.y) || sampleY >= fmaxf(a.y, b.y)) continue;
        intersections[count++] = a.x + (b.x - a.x) * (sampleY - a.y) / (b.y - a.y);
      }
      if (count < 2) continue;
      std::sort(intersections, intersections + count);
      aurora::VisibleUiShape shape;
      shape.left = intersections[0];
      shape.top = static_cast<float>(y);
      shape.right = intersections[count - 1];
      shape.bottom = static_cast<float>(y + 1);
      shape.clipLeft = window->OuterRectClipped.Min.x;
      shape.clipTop = window->OuterRectClipped.Min.y;
      shape.clipRight = window->OuterRectClipped.Max.x;
      shape.clipBottom = window->OuterRectClipped.Max.y;
      std::lock_guard<std::mutex> lock(s_TouchRectMutex);
      s_TouchShapes.push_back(shape);
    }
  }
}



class ImGuiCanvasBackend final : public lgx::CanvasBackend {
public:
  static bool IsPanelCallback(ImDrawCallback callback) {
    return callback == &PanelCaptureCallback || callback == &PanelPaintCallback ||
           callback == &PanelCapturePaintCallback;
  }

  lgx::Box clipRect() override {
    // ★★ 面板（含它的阴影/边光）**不能用 ImGui 的内容裁剪矩形**当裁剪。
    //
    //   ImGui 的 `window->ClipRect` 是 `InnerClipRect`：右边先减掉滚动条宽度、
    //   下边减掉边框。用它去裁剪面板 job，结果就是：
    //     阴影与边光在**右/下**被切掉、在**左/上**留下 ——
    //   看起来正是"窗口左下角一块方形灰补丁"（真机实测：左下阴影 27 灰阶、
    //   右下只有 2 灰阶，同一尺度同一层，差别只来自裁剪）。
    //   本项目的"右侧边框光影缺失"也是同一个机制。
    //
    //   阴影本来就该画到面板**外面**去（那是投影的定义），所以面板 job 的裁剪
    //   由 `ScopedPanelClip` 给出的"面板范围 + 外扩"决定；其余画布图元照旧用
    //   ImGui 的裁剪矩形（内容滚动/裁剪仍然正确）。
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const ImVec2 min = draw->GetClipRectMin();
    const ImVec2 max = draw->GetClipRectMax();
    return lgx::Box(min.x, min.y, max.x, max.y);
  }

  void onFillRect(const lgx::Box& box, float radius, unsigned int color) override {
    if (s_RecordOverlayTouchShapes && (color & 0xff000000u) != 0u) {
      RecordTouchShape(box, radius);
    }
    ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(box.left, box.top),
                                              ImVec2(box.right, box.bottom),
                                              Color(color), radius, 0);
  }

  void onStrokeRect(const lgx::Box& box, float radius, float thickness,
                    unsigned int color) override {
    ImGui::GetWindowDrawList()->AddRect(ImVec2(box.left, box.top),
                                        ImVec2(box.right, box.bottom),
                                        Color(color), radius, 0, thickness);
  }

  void onFillPoly(const lgx::Point* points, int count, unsigned int color) override {
    ImVec2 buffer[kVertexLimit];
    if (count < 3 || count > kVertexLimit) return;
    for (int i = 0; i < count; ++i) buffer[i] = ImVec2(points[i].x, points[i].y);
    ImGui::GetWindowDrawList()->AddConvexPolyFilled(buffer, count, Color(color));
  }

  void onFillConcave(const lgx::Point* points, int count, unsigned int color) override {
    ImVec2 buffer[kVertexLimit];
    if (count < 3 || count > kVertexLimit) return;
    for (int i = 0; i < count; ++i) buffer[i] = ImVec2(points[i].x, points[i].y);
    ImGui::GetWindowDrawList()->AddConcavePolyFilled(buffer, count, Color(color));
  }

  void onStrokePoly(const lgx::Point* points, int count, float thickness,
                    unsigned int color) override {
    ImVec2 buffer[kVertexLimit];
    if (count < 2 || count > kVertexLimit) return;
    for (int i = 0; i < count; ++i) buffer[i] = ImVec2(points[i].x, points[i].y);
    ImGui::GetWindowDrawList()->AddPolyline(buffer, count, Color(color), 0, thickness);
  }

  void onLine(const lgx::Point& from, const lgx::Point& to, unsigned int color,
              float thickness) override {
    ImGui::GetWindowDrawList()->AddLine(ImVec2(from.x, from.y), ImVec2(to.x, to.y),
                                        Color(color), thickness);
  }

  void onFillCircle(const lgx::Point& center, float radius, unsigned int color,
                    int segments) override {
    ImGui::GetWindowDrawList()->AddCircleFilled(ImVec2(center.x, center.y), radius,
                                                Color(color), segments);
  }

  void onFillTriangle(const lgx::Point& a, const lgx::Point& b, const lgx::Point& c,
                      unsigned int color) override {
    ImGui::GetWindowDrawList()->AddTriangleFilled(ImVec2(a.x, a.y), ImVec2(b.x, b.y),
                                                  ImVec2(c.x, c.y), Color(color));
  }

  void onFillArc(const lgx::Point& center, float radius, float fromRadians,
                 float toRadians, unsigned int color, int segments) override {
    ImDrawList* draw = ImGui::GetWindowDrawList();
    draw->PathArcTo(ImVec2(center.x, center.y), radius, fromRadians, toRadians, segments);
    draw->PathFillConvex(Color(color));
  }

  void onText(const lgx::Point& position, float size, unsigned int color,
              const char* content, float wrapWidth) override {
    if (content == nullptr) return;
    ImGui::GetWindowDrawList()->AddText(ImGui::GetFont(), size,
                                        ImVec2(position.x, position.y), Color(color),
                                        content, nullptr, wrapWidth);
  }

  void onImage(unsigned int texture, const lgx::Box& box, const lgx::Point& uvMin,
               const lgx::Point& uvMax, unsigned int color) override {
    if (texture == 0u) return;
    ImGui::GetWindowDrawList()->AddImage(
        (ImTextureID)(intptr_t)texture, ImVec2(box.left, box.top),
        ImVec2(box.right, box.bottom), ImVec2(uvMin.x, uvMin.y),
        ImVec2(uvMax.x, uvMax.y), Color(color));
  }

  void onPushClip(const lgx::Box& box) override {
    ImGui::GetWindowDrawList()->PushClipRect(ImVec2(box.left, box.top),
                                             ImVec2(box.right, box.bottom), true);
  }

  void onPopClip() override { ImGui::GetWindowDrawList()->PopClipRect(); }

  void onPanelCapture(int index) override {
    ImGui::GetWindowDrawList()->AddCallback(&PanelCaptureCallback,
                                            (void*)(intptr_t)index);
    ImGui::GetWindowDrawList()->AddCallback(ImDrawCallback_ResetRenderState, nullptr);
  }

  void onPanelPaint(int index) override {
    ImDrawList* draw = ImGui::GetWindowDrawList();
    static const bool combineCallbacks = [] {
      const char* value = std::getenv("AURORA_PANEL_BATCH");
      return value == nullptr || value[0] != '0' || value[1] != '\0';
    }();
    if (combineCallbacks && aurora::CombineAdjacentPanelCallbacks(
                                *draw, index, &PanelCaptureCallback,
                                &PanelCapturePaintCallback)) {
      aurora::panel_efficiency::resetsSaved.fetch_add(1, std::memory_order_relaxed);
      return;
    }
    draw->AddCallback(&PanelPaintCallback, (void*)(intptr_t)index);
    draw->AddCallback(ImDrawCallback_ResetRenderState, nullptr);
  }

  lgx::PointerProbe onProbe(const lgx::Box& box, const void* owner,
                            bool interactive) override {
    lgx::PointerProbe probe;
    const ImVec2 size(box.width(), box.height());
    if (size.x <= 0.0f || size.y <= 0.0f) return probe;
    ImGui::SetCursorScreenPos(ImVec2(box.left, box.top));
    if (!interactive) {
      ImGui::Dummy(size);
      return probe;
    }
    ImGui::PushID(owner);
    const bool released = ImGui::InvisibleButton("##lgx_probe", size,
                                                 ImGuiButtonFlags_MouseButtonLeft);
    ImGui::PopID();
    probe.held = ImGui::IsItemActive();
    probe.activated = ImGui::IsItemActivated();
    probe.clicked = released;
    return probe;
  }

  void onBlockPointer(const lgx::Box& box) override {
    (void)box;
    ImGui::SetHoveredID(ImHashStr("##lgx_pointer_shield"));
    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) || ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
      ImGui::ClearActiveID();
    }
  }

  void onClearPointerBlock() override {}

private:
  static constexpr int kVertexLimit = 128;

  static void PanelCaptureCallback(const ImDrawList*, const ImDrawCmd* cmd) {
    aurora::panel_efficiency::captureCallbacks.fetch_add(1, std::memory_order_relaxed);
    const int index = (int)(intptr_t)cmd->UserCallbackData;
    const lgx::Box clip = ClipOf(cmd);
    lgx::runPanelCapture(index, clip);
  }

  static void PanelPaintCallback(const ImDrawList*, const ImDrawCmd* cmd) {
    aurora::panel_efficiency::paintCallbacks.fetch_add(1, std::memory_order_relaxed);
    const int index = (int)(intptr_t)cmd->UserCallbackData;
    const lgx::Box clip = ClipOf(cmd);
    lgx::runPanelPaint(index, clip);
  }

  static void PanelCapturePaintCallback(const ImDrawList*, const ImDrawCmd* cmd) {
    aurora::panel_efficiency::combinedCallbacks.fetch_add(1, std::memory_order_relaxed);
    const int index = (int)(intptr_t)cmd->UserCallbackData;
    const lgx::Box clip = ClipOf(cmd);
    lgx::runPanelCapturePaint(index, clip);
  }

  static lgx::Box ClipOf(const ImDrawCmd* cmd) {
    return lgx::Box(cmd->ClipRect.x, cmd->ClipRect.y, cmd->ClipRect.z, cmd->ClipRect.w);
  }
};

static ImGuiCanvasBackend s_Backend;

// 面板 job 的裁剪 = "面板范围 + 阴影/边光外扩"（见 clipRect() 的注释）。
// 外扩量直接从 style 算，不写魔法数字：阴影半径 × SHADOW_EXTENT_GAIN + 偏移。
static float PanelClipMargin(const lgx::PanelStyle& style) {
  float margin = lgx::px(12.0f);   // 捕获 pad + 边光柔边
  if (style.shadowOn) {
    const float radius = lgx::px(style.shadow.radiusDp);
    margin += radius * lgx::config::Render::SHADOW_EXTENT_GAIN;
    margin += std::fabs(lgx::px(style.shadow.offsetXDp));
    margin += std::fabs(lgx::px(style.shadow.offsetYDp));
  }
  return margin;
}

struct ScopedPanelClip {
  // ★★ 面板 job 的裁剪必须**分成两类**，这是用户明确区分的三个区域：
  //
  //   A. **窗口外层光学效果区域**（窗口自己的玻璃面板 + 它的投影）
  //      —— 投影本来就画在窗口外面，所以要用"面板 + 阴影外扩"
  //      **替换** ImGui 的内容裁剪矩形（`intersect=false`）。
  //      只改 `clipRect()` 没用：面板是**延迟 draw callback**，裁剪取的是
  //      提交那一刻记下的 `cmd->ClipRect`（实测改了画面一个像素都不动）。
  //
  //   B. **窗口内部内容可视区域**（内容里的玻璃面板：按钮/卡片/示例）
  //      —— **绝不越出窗口**，一律沿用 ImGui 的裁剪矩形
  //      （`window->ClipRect` = InnerClipRect，已扣掉标题栏与边框）。
  //      这一类**不要**用本类。
  //
  // ★★ 踩过的坑（就是用户报的那两个现象）：第一版把两类混在一起，
  //    内容面板也替换了裁剪 ⇒ 按钮/卡片画到**标题栏上**（看着像"顶部标签栏
  //    还在，被灰色玻璃盖住"）以及**画到窗口外面**（"内容超出窗口底边"）。
  //    根因不在布局，而在裁剪被替换掉了。
  explicit ScopedPanelClip(const lgx::PanelStyle& style, const lgx::Box& box) {
    const float margin = PanelClipMargin(style);
    ImGui::PushClipRect(ImVec2(box.left - margin, box.top - margin),
                        ImVec2(box.right + margin, box.bottom + margin), false);
  }
  ~ScopedPanelClip() { ImGui::PopClipRect(); }
};
// ---- 输入框 ↔ 系统输入法 的镜像同步：状态与应用函数 -------------------------
//  ★ 定义必须放在 DrawContents **之前**：ApplyPendingImeText 要在画输入框之前调用。
namespace {
// 输入框的 ID：每帧在 InputText **之后**记录（label 随语言变，不能写死字符串），
// 供下一帧在 InputText **之前**设置 ReloadUserBuf 用。
ImGuiID g_ImeInputId = 0;
char g_ImeTextPending[256] = {};
bool g_ImeTextPendingSet = false;
int g_ImeSelStart = -1;   // UTF-16 码元；<0 = 移到末尾
int g_ImeSelEnd = -1;
}  // namespace

// 在 InputText **之前**调用：把外部（系统输入法）的文本装进用户缓冲，
// 并让 ImGui 重新从缓冲拉取（ReloadUserBuf），**当帧生效**。
// 走官方通道而不是手改 `state->TextA/Stb`：imgui_internal.h:1110-1117 明确写了
// "若你在 active 期间改了用户缓冲，就调 ReloadUserBufAnd*"。
static void ApplyPendingImeText() {
  if (!g_ImeTextPendingSet) return;
  g_ImeTextPendingSet = false;
  std::snprintf(s_ImeText, sizeof(s_ImeText), "%s", g_ImeTextPending);
  if (g_ImeInputId == 0) return;                 // 输入框还没画过：只改缓冲即可
  ImGuiInputTextState* state = ImGui::GetInputTextState(g_ImeInputId);
  if (state == nullptr || state->ID != g_ImeInputId) return;   // 当前没在编辑：同上
  state->ReloadUserBuf = true;
  state->ReloadSelectionStart = g_ImeSelStart >= 0 ? g_ImeSelStart : INT_MAX;
  state->ReloadSelectionEnd = g_ImeSelEnd >= 0 ? g_ImeSelEnd : INT_MAX;
  g_ImeSelStart = g_ImeSelEnd = -1;
}

// ---------------------------------------------------------------------------
//  诊断：弹窗对**页面排版**的副作用探针（只在显式打开环境变量时生效，默认零开销）
// ---------------------------------------------------------------------------
//  为什么需要它：弹窗的 Cancel/OK 两个按钮是 ytbl 的自绘组件，绘制时会走
//  `lgx::probeBox` → 宿主 `HostBackend::onProbe`，而 onProbe 里有
//      ImGui::SetCursorScreenPos(控件左上角);
//      ImGui::InvisibleButton(...);          ← 会推进 ImGui 的光标
//  ⇒ 弹窗画完后，**ImGui 的光标停在弹窗的按钮上**（窗口中部）。
//  而「Aurora 运行时信息」（`DrawHostDiagnostics`）是在 `DrawContents()` **之后**
//  才绘制的 ⇒ 它从弹窗按钮的位置开始排版，屏幕上就表现为
//  "运行时信息被弹窗带上来了 / 位置异常"。
//
//  ★ 为什么不能靠点击复现：测试工具 的一次性手势注入构造不出"按下并抬起"
//    （见 AGENTS：input swipe 会提前送 UP），所以给一个**只读的诊断入口**：
//      AURORA_DIALOG_PROBE=1  → 启动后自动打开一次弹窗
//      AURORA_DIALOG_TRACE=1  → 打印三处光标位置（弹窗前 / 弹窗后 / 运行时信息开画）
//    两者默认都关，不影响任何发布行为。
static bool DialogEnvFlag(const char* name) {
  const char* v = std::getenv(name);
  return v != nullptr && v[0] != '\0' && v[0] != '0';
}

static bool DialogProbeEnabled() {
  static const int mode = [] {
    const char* v = std::getenv("AURORA_DIALOG_PROBE");
    if (v == nullptr || v[0] == '\0' || v[0] == '0') return 0;
    return v[0] - '0';
  }();
  return mode != 0;
}

/** AURORA_DIALOG_PROBE=2 ⇒ 用 openFrom（动画一：从触发按钮展开）；否则 open（动画二：从中心）。 */
static bool DialogProbeFromButton() {
  static const bool fromButton = [] {
    const char* v = std::getenv("AURORA_DIALOG_PROBE");
    return v != nullptr && v[0] == '2';
  }();
  return fromButton;
}

static bool DialogTraceEnabled() {
  static const bool on = DialogEnvFlag("AURORA_DIALOG_TRACE");
  return on;
}

static void DialogLog(const char* fmt, ...) {
  // 纯净版：DialogLog 只在 AURORA_DIALOG_TRACE=1 时被调用，但函数体本身
  // 仍然会被链接进来。编译期直接留空，连 va_start 都不做。
#if AURORA_SILENT
  (void)fmt;
#else
  va_list args;
  va_start(args, fmt);
  __android_log_vprint(ANDROID_LOG_INFO, "aurora_overlay", fmt, args);
  va_end(args);
#endif
}
/** 触控数据仿真：AURORA_TOUCH_SIM=1 时让 Pan/Pinch/Rot 在极端值之间交替。
 *  为的是**在没有真实手指的情况下**也能验证"数值长度变化会不会抖动布局" ——
 *  交替值刻意取"最短"与"最长"两种文本宽度（0.0,0.0 vs -9999.9,-9999.9）。 */
static bool TouchSimEnabled() {
  static const bool on = DialogEnvFlag("AURORA_TOUCH_SIM");
  return on;
}

/** A/B 开关：AURORA_TOUCH_LEGACY=1 走**修复前**的写法（PushTextWrapPos + Text），
 *  默认走定高容器。留着它是为了做同二进制内的对照实验（证明修复确实消除了抖动）。 */
static bool TouchLegacyLayout() {
  static const bool on = DialogEnvFlag("AURORA_TOUCH_LEGACY");
  return on;
}

/** 诊断：AURORA_IME_AUTOFOCUS=1 时，启动约 120 帧后**自动把键盘焦点给输入框**。
 *  用途：用户报的"重启后第一次点输入框要等几秒"必须在"冷启动后的第一次聚焦"上测，
 *  而 测试工具 的一次性手势注入构造不出"按下并抬起"（见 AGENTS）⇒ 用这个入口
 *  **程序化地模拟第一次点击**，从而能脚本化地量出
 *  "聚焦 → 请求拉起键盘"这一段里 ELF 自己花了多少时间（与系统输入法耗时区分开）。 */
static bool ImeAutoFocusEnabled() {
  static const bool on = DialogEnvFlag("AURORA_IME_AUTOFOCUS");
  return on;
}

static void DrawContents() {
  // 组件演示删掉后，io/dl/stage 都不再需要；scene 仅剩诊断弹窗入口在用。
  aurora::glass::GlassScene& scene = Scene();

  // 诊断入口：AURORA_DIALOG_PROBE=1/2 时自动打开一次弹窗（复现"弹窗副作用"用）
  //   1 = open()（动画二：从中心玻璃球）   2 = openFrom()（动画一：从触发按钮展开）
  //   ★ 延迟 300 帧（≈5 秒）再打开：这样外部（测试工具 注入）能先把页面滚到某个位置，
  //     从而验证"在任意滚动位置打开弹窗都不改变滚动位置、也不改变运行时信息的归属"。
  if (DialogProbeEnabled()) {
    static int s_ProbeFrames = 0;
    static bool s_ProbeOpened = false;
    if (!s_ProbeOpened && ++s_ProbeFrames >= 300) {
      s_ProbeOpened = true;
      const char* title = Text("弹窗标题", "Dialog Title");
      const char* message =
          Text("这是一个带有入场、退场、折射和双按钮交互的液态玻璃弹窗。",
               "This dialog uses animated dimming, refraction, and two actions.");
      if (DialogProbeFromButton()) {
        const ImVec2 at = ImGui::GetCursorScreenPos();
        scene.modal.openFrom(title, message,
                             lgx::Box(at.x, at.y + lgx::px(200.0f), at.x + lgx::px(180.0f),
                                      at.y + lgx::px(248.0f)));
        DialogLog("[诊断] AURORA_DIALOG_PROBE=2：已用 openFrom（动画一）打开弹窗");
      } else {
        scene.modal.open(title, message);
        DialogLog("[诊断] AURORA_DIALOG_PROBE=1：已用 open（动画二）打开弹窗");
      }
    }
  }

  ImGui::TextUnformatted(Text("输入法与多点触控", "IME and multi-touch"));
  // ★ 系统输入法刚回传的文本在这里装进去（镜像同步，见 secondary_imgui.h）
  ApplyPendingImeText();
  // 诊断（AURORA_IME_AUTOFOCUS=1）：启动约 120 帧后自动聚焦输入框，
  // 等价于"冷启动后用户第一次点击输入框"。`SetKeyboardFocusHere()` 必须在
  // **InputText 之前**调用 —— 它作用于"下一个 item"。
  {
    static int s_AutoFocusTick = 0;
    if (ImeAutoFocusEnabled() && ++s_AutoFocusTick == 120) {
      ImGui::SetKeyboardFocusHere();
      DialogLog("[诊断] AURORA_IME_AUTOFOCUS：第 120 帧自动聚焦输入框（模拟首次点击）");
    }
  }
  ImGui::InputText(Text("文本输入", "Text input"), s_ImeText, sizeof(s_ImeText));
  // 记下输入框的 ID：下一帧设置 ReloadUserBuf 要用它（label 随语言变，不能写死）
  g_ImeInputId = ImGui::GetItemID();
  s_WantsTextInput = ImGui::IsItemActive();
  ImGui::Text("%s %u", Text("活动触点", "Active pointers"),
              static_cast<unsigned int>(s_HostInfo.activePointers));
  // ★★★ 触控数据行（用户报的"只要触摸窗口，整个窗口就不停抽搐"）
  //
  //  根因：旧写法是
  //      ImGui::PushTextWrapPos(光标 + 可用宽);
  //      ImGui::Text("Pan %.1f,%.1f  Pinch %.2f  Rot %.2f rad", …);
  //      ImGui::PopTextWrapPos();
  //  —— 一旦数字变长（例 `Pan -123.4,-567.8`）文本超过可用宽度，ImGui 就**折成两行**，
  //  这个 item 的高度从 1 行变 2 行 ⇒ **下面所有 Demo 整体下移**；手指再动一下数字变短，
  //  又折回一行 ⇒ 上下反复，看起来就是整个窗口在抽搐。手指连续移动时每一帧都可能翻转。
  //
  //  修法（同时满足"定高 / 单行 / 裁剪 / 分段固定列 / 不触发布局重算"）：
  //    ① 用 `Dummy` 占一个**固定高度**的矩形 —— 布局层只看得到它，永远一行高；
  //    ② 文本用 draw list 的 `AddText` 直接画进这个矩形，**完全不产生布局 item**
  //       ⇒ 数值怎么变都不会让 ImGui 重新计算尺寸（也就不存在"测量缓存被失效"的问题）；
  //    ③ `PushClipRect` 裁到该矩形 ⇒ 超宽时被裁掉，而不是换行或撑开；
  //    ④ Pan / Pinch / Rot 三段各自占**固定比例宽度** ⇒ 某段数字变长只在自己段内变宽，
  //       绝不再推动其它两段（用户要求的"字段之间不左右跳动"）；
  //    ⑤ 格式化用栈上缓冲，零堆分配、零对象创建。
  //
  //  ★ 窗口缩放时 `GetContentRegionAvail().x` 每帧重读 ⇒ 可用宽度会重新分配；
  //    但**单次数据更新不引起任何布局抖动**，因为布局只看那个定高 Dummy。
  float touchPanX = s_HostInfo.gesturePanX;
  float touchPanY = s_HostInfo.gesturePanY;
  float touchScale = s_HostInfo.gestureScale;
  float touchRot = s_HostInfo.gestureRotation;
  if (TouchSimEnabled()) {
    // 每 30 帧在"最短文本"与"最长文本"之间翻转一次（约 0.5s，肉眼可辨、便于截图取证）。
    // ★ 长值刻意取得**足够极端**：本项目字号 40px、窗口内容宽约 672px ⇒
    //   `Pan 0.0,0.0 Pinch 1.00 Rot 0.00 rad` 恰好 2 行；长值能到 3 行以上。
    //   只有在"行数真的不同"时，旧写法的高度变化才会显形（第一版仿真值不够长，
    //   两组都是 2 行 ⇒ 旧布局的行高也恒定 80px，实验分辨不出问题）。
    //   用户要求里也明确写了"极端数值超出预留宽度时也不能破坏布局"，所以这组值是**合法测试输入**。
    static int s_TouchSimTick = 0;
    const bool wide = ((++s_TouchSimTick / 30) % 2) == 0;
    if (wide) {
      touchPanX = -99999999.9f; touchPanY = -99999999.9f;
      touchScale = 99999999.99f; touchRot = -35999999.99f;
    } else {
      touchPanX = 0.0f;     touchPanY = 0.0f;
      touchScale = 1.0f;    touchRot = 0.0f;
    }
  }

  float touchRowHeightForTrace = 0.0f;
  if (TouchLegacyLayout()) {
    // —— 修复前的写法（仅用于同二进制 A/B 对照，发布不走这里）——
    ImGui::PushTextWrapPos(ImGui::GetCursorPos().x + ImGui::GetContentRegionAvail().x);
    ImGui::Text("Pan %.1f,%.1f  Pinch %.2f  Rot %.2f rad",
                touchPanX, touchPanY, touchScale, touchRot);
    touchRowHeightForTrace = ImGui::GetItemRectSize().y;
    ImGui::PopTextWrapPos();
  } else {
    // ★★★ 垂直**分列**（用户要求）：Pan / Pinch / Rot 各占**一整行**。
    //   为什么不再用"同一行三段固定比例"：窗口窄到一定程度时，Pan 的文本
    //   （例如 `Pan -99999999.9,-99999999.9`）会越过它那一段的右边界、
    //   压到 Pinch 段上 —— 看起来就是三个字段**重叠**在一起（用户实测反馈）。
    //   分成三行之后：每行独占整行宽度、行内左对齐 ⇒ 任何窗口宽度都不可能重叠。
    //
    //   ★ 仍然满足任务一的"定高、单行、裁剪、零抖动"：
    //     · 每个 `TextUnformatted` 都是**单行** item（没有 PushTextWrapPos ⇒ 不换行）
    //       ⇒ 三行总高恒为 3×行高，与数值长短**无关**；
    //     · `PushClipRect` 把整个三行块裁到窗口内容宽度内 ⇒ 超宽被裁而不是溢出；
    //     · 块高不随数据变化 ⇒ 下方 Demo 一个像素都不动（下方不再有可变高度 item）。
    const ImVec2 blockMin = ImGui::GetCursorScreenPos();
    const float blockW = ImGui::GetContentRegionAvail().x;
    const float lineH = ImGui::GetTextLineHeight();
    const float spacingY = ImGui::GetStyle().ItemSpacing.y;
    // ★ 三行的实际占用是 `3×行高 + 2×行间距`（行与行之间也有 ItemSpacing）——
    //   第一版只写了 3×行高，于是第三行 `Rot` 正好落在裁剪矩形外**被裁掉**
    //   （截图证据：只显示 Pan / Pinch 两行）。这里必须把行间距算进去。
    const float blockH = lineH * 3.0f + spacingY * 2.0f;
    char panBuf[64];
    char pinchBuf[48];
    char rotBuf[48];
    std::snprintf(panBuf, sizeof(panBuf), "Pan %.1f,%.1f", touchPanX, touchPanY);
    std::snprintf(pinchBuf, sizeof(pinchBuf), "Pinch %.2f", touchScale);
    std::snprintf(rotBuf, sizeof(rotBuf), "Rot %.2f rad", touchRot);
    const float beforeY = blockMin.y;
    ImGui::PushClipRect(blockMin, ImVec2(blockMin.x + blockW, blockMin.y + blockH), true);
    ImGui::TextUnformatted(panBuf);
    ImGui::TextUnformatted(pinchBuf);
    ImGui::TextUnformatted(rotBuf);
    ImGui::PopClipRect();
    // 取证：块高应为恒定 3 行 + 2 间距（与数值长度无关）
    touchRowHeightForTrace = blockH;
    (void)beforeY;
  }
  // 布局取证：块高与"块后光标 y"在数值切换时必须**完全不变**。
  if (DialogTraceEnabled()) {
    const ImVec2 afterRow = ImGui::GetCursorScreenPos();
    DialogLog("[诊断] 触控行 模式=%s Pan=%.1f,%.1f Pinch=%.2f Rot=%.2f → 行高=%.1f 行后光标y=%.1f",
              TouchLegacyLayout() ? "旧(换行)" : "新(定高)", touchPanX, touchPanY, touchScale,
              touchRot, touchRowHeightForTrace, afterRow.y);
  }
  // ★ 这条横线也已删除（输入区与实时信息之间不再有分隔线）。
  ImGui::Spacing();
  // ---- 内容到此为止 ----
  //
  // ★★ 这里曾经是从「c++ 液态玻璃展示源码」移植过来的整套组件演示，
  //    共八段：PillButton×5、Toggle×3、Slider×2、PopMenu、TabStrip、
  //    Corners×6 芯片、EdgeLight、玻璃卡片、玻璃实验场。
  //    它们全部按用户要求删除，理由是：这些是那个开源项目的组件事例，
  //    留在本工程里既占帧时间（每块玻璃都是一次完整片元管线），
  //    又让"液态玻璃窗口"这个产品定位变得含糊。
  //
  //    保留的是：窗口壳本身（在 Draw 里）、这块文本输入区（上面）、
  //    以及右下角的实时性能监测（DrawHostDiagnostics）。
}

static void DrawHostDiagnostics() {
  if (DialogTraceEnabled()) {
    const ImVec2 c = ImGui::GetCursorScreenPos();
    DialogLog("[诊断] 运行时信息开画：光标=(%.0f,%.0f) 滚动=%.0f/%.0f",
              c.x, c.y, ImGui::GetScrollY(), ImGui::GetScrollMaxY());
  }
  // 用户要求：输入区与实时信息之间不要分隔线，两者紧挨着放在同一个位置。
  ImGui::Spacing();
  if (ImGui::CollapsingHeader(Text("Aurora 运行时信息", "Aurora runtime"),
                              ImGuiTreeNodeFlags_DefaultOpen)) {
    ImGui::Text("Renderer  %s", s_HostInfo.renderer.empty() ? "unknown" : s_HostInfo.renderer.c_str());
    ImGui::Text("Host      %s", s_HostInfo.host.empty() ? "unknown" : s_HostInfo.host.c_str());
    ImGui::Text("Surface   %s", s_HostInfo.surfaceReady ? "ready" : "lost");
    ImGui::Text("IME       %s", s_HostInfo.imeAvailable ? "available" : "unavailable");
    ImGui::Text("Backdrop  %s", s_HostInfo.backgroundInput ? "host framebuffer" : "fallback");
    // ★ 帧率与"卡不卡"分开显示。用户报过"感觉到掉帧但显示 120fps"，
    //   根因是旧版只给平均帧率（EMA 平滑，掩盖偶发长帧）。现在把长帧数
    //   摆在显眼位置：它才是和手感对应的那个数。
    ImGui::Text("Frames    %llu | FPS %.1f",
                static_cast<unsigned long long>(s_HostInfo.frameCount), s_HostInfo.fps);
    {
      const float jank = s_HostInfo.jankPerSecond;
      const ImVec4 color = jank < 0.5f   ? ImVec4(0.45f, 0.90f, 0.50f, 1.0f)
                           : jank < 3.0f ? ImVec4(0.95f, 0.80f, 0.35f, 1.0f)
                                         : ImVec4(0.95f, 0.45f, 0.40f, 1.0f);
      ImGui::TextColored(color, "Jank      %.1f/s (>%.1fms) worst %.1f ms", jank,
                         s_HostInfo.jankThresholdMs, s_HostInfo.worstFrameMilliseconds);
    }
    ImGui::Text("Safe area %.0f,%.0f - %.0f,%.0f",
                s_HostInfo.safeLeft, s_HostInfo.safeTop,
                s_HostInfo.safeRight, s_HostInfo.safeBottom);
    ImGui::Text("Input     %s", s_HostInfo.textInputRequested ? "text requested" : "pointer/UI");
    ImGui::Text("Pointers  %u", static_cast<unsigned int>(s_HostInfo.activePointers));
    ImGui::Text("Vulkan    %s (%u devices)",
                s_HostInfo.vulkanState.empty() ? "unavailable" : s_HostInfo.vulkanState.c_str(),
                s_HostInfo.vulkanDevices);
    if (s_HostInfo.surfaceFormats != 0 || s_HostInfo.presentModes != 0) {
      ImGui::Text("VK Surface %u formats / %u present modes",
                  s_HostInfo.surfaceFormats, s_HostInfo.presentModes);
    }
    if (!s_HostInfo.vulkanDiagnostic.empty()) {
      ImGui::PushTextWrapPos(0.0f);
      ImGui::TextDisabled("%s", s_HostInfo.vulkanDiagnostic.c_str());
      ImGui::PopTextWrapPos();
    }
  }
  // ★ 底部留出一个圆角半径的空白（用户报过"窗口底部的 Toggle Demo 被截断"）。
  //   窗口外壳是 28dp 圆角，而滚动视口是**矩形**：最后一行内容滚到底时正好压在
  //   圆角上，看起来就是"被切掉"。给内容末尾补一段与圆角同高的空白 ⇒
  //   任何一个 Demo 都能滚到圆角上方完整显示（滚动范围也随之变大，是正常的
  //   滚动视口裁剪，不是内容溢出 —— 内容仍然被圆角 SDF 遮罩裁在窗口轮廓内）。
  ImGui::Dummy(ImVec2(1.0f, WindowCornerRadiusPx()));
}

}

void InstallGlassBackend() {
  lgx::attachCanvasBackend(&s_Backend);
}

bool IsPanelDrawCallback(ImDrawCallback callback) {
  return ImGuiCanvasBackend::IsPanelCallback(callback);
}

void SetLanguage(bool chinese) {
  s_Chinese = chinese;
}

void UseLatencyComparisonWindowOptics() {
  // 诊断模式：把整窗的四项光学参数清零（用于对照"零光学效果"下的延迟）。
  // ★ 走注册表写入：这样主题窗口里的滑杆会**同步显示**成 0，
  //   而不是"值被偷偷改了但界面还显示原来的数"。
  aurora::glass::Registry& registry = aurora::glass::Global();
  if (registry.FindTarget("secondary_window") != nullptr) {
    registry.SetB("secondary_window", "glassEnabled", true, aurora::glass::ChangeOrigin::Test);
    registry.SetF("secondary_window", "blurDp", 0.0f, aurora::glass::ChangeOrigin::Test);
    registry.Commit();   // 立刻生效：这是启动路径，不等帧边界
    registry.SetF("secondary_window", "lensHeightDp", 0.0f, aurora::glass::ChangeOrigin::Test);
    registry.SetF("secondary_window", "lensAmountDp", 0.0f, aurora::glass::ChangeOrigin::Test);
    registry.SetF("secondary_window", "dispersion", 0.0f, aurora::glass::ChangeOrigin::Test);
    registry.Commit();
    return;
  }
  aurora::glass::WindowTune& window = Scene().window;
  window.glassEnabled = true;
  window.blurDp = 0.0f;
  window.blurAppliedDp = 0.0f;
  window.lensHeightDp = 0.0f;
  window.lensAmountDp = 0.0f;
  window.dispersion = 0.0f;
}

bool WantsTextInput() {
  return s_WantsTextInput;
}

// ★★ "想不想要文字输入"必须是**当前帧**的事实，不能沿用上一帧。
//
//  踩过的坑：`s_WantsTextInput` 只在 `if (begun) DrawContents()` 里被**全量赋值**，
//  于是窗口被折叠 / 被完全裁掉 / 不可见的那几帧它**保持上一帧的值**；而 InputText
//  一旦处于 active，这个值就是恒 true ⇒ 宿主的 `wantsTextInput()` 永远为真、
//  "拉起键盘"的上升沿再也不出现（键盘收起来就再也叫不回来）。
//  现在每帧开头先清零，只有真正画到输入框那一行才会被置上。
void BeginTextInputFrame() {
  s_WantsTextInput = false;
}

// ---- 输入框 ↔ 系统输入法 的镜像同步（见 secondary_imgui.h 的说明）-----------
//  ★ 这三样（变量 + 两个入口 + 应用函数）定义在 DrawContents 之前 ——
//    应用函数必须在画输入框**之前**被调用，所以不能放到文件后部去。

void SetImeText(const char* utf8Text, int selectionStartUtf16, int selectionEndUtf16) {
  if (utf8Text == nullptr) return;
  std::snprintf(g_ImeTextPending, sizeof(g_ImeTextPending), "%s", utf8Text);
  g_ImeSelStart = selectionStartUtf16;
  g_ImeSelEnd = selectionEndUtf16;
  g_ImeTextPendingSet = true;
}

const char* GetImeText() { return s_ImeText; }

void SetHostInfo(const HostInfo& info) {
  s_HostInfo = info;
}

void SetVisible(bool visible) {
  s_Visible.store(visible, std::memory_order_relaxed);
  if (!visible) {
    s_PlaygroundDragging = false;
    s_PlaygroundResizing = false;
    ClearTouchRect();
    s_WantsTextInput = false;
  }
}

bool IsVisible() {
  return s_Visible.load(std::memory_order_relaxed);
}

void SetAnchorRect(float x, float y, float w, float h) {
  std::lock_guard<std::mutex> lock(s_AnchorMutex);
  s_Anchor[0] = x;
  s_Anchor[1] = y;
  s_Anchor[2] = w;
  s_Anchor[3] = h;
  s_AnchorValid = w > 1.0f && h > 1.0f;
}

void GetTouchRect(float outRect[4]) {
  if (!outRect) return;
  std::lock_guard<std::mutex> lock(s_TouchRectMutex);
  outRect[0] = s_TouchRect[0];
  outRect[1] = s_TouchRect[1];
  outRect[2] = s_TouchRect[2];
  outRect[3] = s_TouchRect[3];
}

std::uint32_t TouchWindowId() {
  std::lock_guard<std::mutex> lock(s_TouchRectMutex);
  return s_TouchWindowId;
}

void AppendTouchShapes(std::vector<aurora::VisibleUiShape>& shapes) {
  std::lock_guard<std::mutex> lock(s_TouchRectMutex);
  shapes.insert(shapes.end(), s_TouchShapes.begin(), s_TouchShapes.end());
}

// =============================================================================
// ★★ 二级窗口手势层（用户要求）
//
//   ① 顶部标签栏/标题栏按住滑动 ⇒ **拖动整个窗口**（任意方向），标签点击切换保留；
//   ② 内容空白处上下滑动 ⇒ **滚动页面**（跟手 + 惯性 + 边界夹紧，不再有滚动条）；
//   ③ 组件（开关/进度条/按钮/标签）保持自身行为，**页面滚动不许抢它的触摸**；
//   ④ 一次手势定下归属后**不再改**（不在"拖动窗口/滚动/组件"之间反复横跳）。
//
//   判定与数学在 `PanelGestures.h`（纯逻辑、有离线回归 PanelGesturesTest）；
//   这里只做 ImGui 接线：命中判定用 **ImGui 的真实 item 矩形**
//   （`IsAnyItemHovered`），**不是**"像素是否透明"。
//
//   ★ 拖动窗口在 `Begin()` **之前**应用 ⇒ 当帧就用新位置绘制，不落后一帧（跟手）。
//   ★ 页面滚动用 `ImGui::SetScrollY`（内部会夹到 [0, ScrollMax]）；
//     惯性那半用 PanelGestures 的夹紧（撞边界立刻清零，不产生越界位移）。
// =============================================================================
static aurora::PanelGesture s_GesturePending = aurora::PanelGesture::None;
static aurora::PanelGesture s_GestureOwner = aurora::PanelGesture::None;
static ImVec2 s_GestureStartMouse(0.0f, 0.0f);
static ImVec2 s_GestureStartWindow(0.0f, 0.0f);
static float s_GestureStartScroll = 0.0f;
static float s_GestureVelocity = 0.0f;          // 像素/秒
static float s_GestureLastMouseY = 0.0f;
static aurora::PanelScrollInertia s_ScrollInertia;
static const bool s_GestureTrace = EnvOn("AURORA_GESTURE_TRACE");
static aurora::PanelGesture s_GestureLoggedPending = aurora::PanelGesture::None;
static aurora::PanelGesture s_GestureLoggedOwner = aurora::PanelGesture::None;

// 手势状态转换日志：只在状态变化时打（平时一条都不打）。
// 为什么要它：真机上"拖到一半停了"这种问题，看截图看不出来，只有日志能给现场。
static void GestureLog(const char* fmt, ...) {
#if AURORA_SILENT
  (void)fmt;   // 纯净版：连 va_start 都不做
#else
  va_list args;
  va_start(args, fmt);
  __android_log_vprint(ANDROID_LOG_INFO, "aurora_overlay", fmt, args);
  va_end(args);
#endif
}
static const char* GestureName(aurora::PanelGesture g) {
  switch (g) {
    case aurora::PanelGesture::Component: return "组件";
    case aurora::PanelGesture::WindowDrag: return "拖窗口";
    case aurora::PanelGesture::Scroll: return "滚页面";
    default: return "无";
  }
}

// =============================================================================
// ★★ 右下角"长按 + 拖动"缩放 —— Corner Resize Handle
//
//   交互（用户要求，逐条对应）：
//     · 默认**什么都不显示**（手柄完全收起时一个顶点都不产生）；
//     · 右下角一块**看不见的**判定区（44dp，向窗口内延伸）按住 **450ms**
//       （可调常量，见 ResizeGesture.h）⇒ 白色圆弧**沿圆角长出来**；
//     · **不用松手**就能直接拖：右/下变大、左/上变小、斜向同时改，左上角固定；
//     · 松手立刻定尺寸，圆弧沿同一条路径收回去（同一套弹簧，可中途反向）。
//   ★ 判定区**不会无条件抢组件**：宿主先看 ImGui 的真实 item 命中，
//     按在按钮/滑条上时根本不进长按判定（组件优先，与窗口拖动一致）。
//   ★ 缩放期间这一笔触摸由缩放独占：不滚动、不拖窗口、不点按钮。
//
//   ★★ 手柄造型（这一版**完全重做**，旧的"两根长白杆"已删除）：
//     旧的 DrawResizeHandle 画的是 `46dp 竖臂 + 20dp 弯角 + 46dp 横臂`
//     —— 两根长杆拼一个 L，真机截图里就是两根突兀的白杆（连接处还有折点）。
//     现在改成**一条连续的粗圆弧**（与窗口右下圆角同心、贴着圆角外侧、
//     两端圆端帽、单一多边形填充），几何与动画都在 CornerResizeHandle.h，
//     宿主只负责"把窗口实时矩形喂进去、把顶点交给 ImGui 画"。
// =============================================================================
// ★★ 长按缩放的全部状态与接线已经搬到共用模块 aurora::CornerResizeRuntime
//    （CornerResizeOverlay.h/.cpp）—— 主窗口与二级窗口现在跑的是**同一套**内核、
//    同一套手柄几何、同一个"只震一次"的触觉门，手感不可能走偏。
//    本文件只保留"某一帧指针压在组件上吗"这类 ImGui 侧的事实采集。
static aurora::CornerResizeRuntime s_CornerResize;

// ---- 输入法避让状态（只在渲染线程用）--------------------------------------
//   s_ImeAvoidPos/Size : 上一帧窗口**实际**的位置与尺寸（含已施加的避让位移）
//   s_ImeAvoidShift    : 已经施加的上移量（还原逻辑位置要用它）
static float s_ImeAvoidPos[2] = {16.0f, 16.0f};
static float s_ImeAvoidSize[2] = {760.0f, 700.0f};
static float s_ImeAvoidShift = 0.0f;
// ---- 圆角内容遮罩：ImGui 那条绘制路径 -------------------------------------
// 在 draw callback 里切换（同帧里有圆角窗口也有方角窗口，遮罩必须按绘制区间切换）。
static void ContentMaskSetCallback(const ImDrawList*, const ImDrawCmd* cmd) {
  const float* r = static_cast<const float*>(cmd->UserCallbackData);
  ImGui_ImplOpenGL3_SetClipRoundRect(r[0], r[1], r[2], r[3], r[4]);
}
static void ContentMaskClearCallback(const ImDrawList*, const ImDrawCmd*) {
  ImGui_ImplOpenGL3_SetClipRoundRect(0.0f, 0.0f, 0.0f, 0.0f, -1.0f);
}

// 角部缩放手柄：**一条连续的粗圆弧**（不是 L 形）。
//   · 几何每帧从**窗口实时矩形**现算 ⇒ 缩放过程中手柄自动跟着右下角走，
//     不存在"多处硬编码右下角坐标"；
//   · 整条弧（外弧 + 两个圆端帽 + 内弧）生成成**一个闭合多边形**，
//     用 AddConcavePolyFilled 一次填充 ⇒ 没有折线接头、没有端帽叠色、
//     AA 由 ImGui 的 fringe 一次算好（比"描边 + 两个实心圆"三条图元干净）；
//   · 画在**前景层**（窗口级 Overlay）：既不被内部内容的圆角裁剪误伤，
//     也不受窗口内容裁剪矩形限制 —— 弧本来就该有一点点在窗口轮廓之外。
// 手柄绘制也搬到了模块里（CornerResizeDrawHandle），保证两个窗口的圆弧完全一致。

static void ApplyPendingWindowDrag() {
  if (s_GestureOwner != aurora::PanelGesture::WindowDrag) return;
  const ImVec2 mouse = ImGui::GetIO().MousePos;
  float x = s_GestureStartWindow.x + (mouse.x - s_GestureStartMouse.x);
  float y = s_GestureStartWindow.y + (mouse.y - s_GestureStartMouse.y);
  // ★ 只保证"至少留一块在屏幕内"，**不做自由拖动的限制**：
  //   窗口 760 宽、屏 1080 ⇒ 横向仍有 320px 自由度（早先那次"拖不动"
  //   是因为把窗口夹进了 800 宽的拦截区，不是这里）。留 120px 是为了
  //   万一拖出去还能抓回来。
  const ImVec2 screen = ImGui::GetIO().DisplaySize;
  const float keep = lgx::px(40.0f);
  const ImVec2 size = ImGui::GetWindowSize();
  x = ImClamp(x, keep - size.x, screen.x - keep);
  y = ImClamp(y, 0.0f, screen.y - keep);
  ImGui::SetNextWindowPos(ImVec2(x, y));
}

static void RunSecondaryWindowGestures(ImGuiWindow* window) {
  if (window == nullptr) return;
  ImGuiIO& io = ImGui::GetIO();
  const ImVec2 mouse = io.MousePos;
  const bool down = ImGui::IsMouseDown(ImGuiMouseButton_Left);

  // ★★ 不要用 `s_HostInfo.activePointers` 当"手指还在不在"的判据：
  //    它是**按帧/按输入批次**刷新的，没有新事件的帧就是 0 —— 手指明明还在屏上
  //    拖动却会被"触点归零"误复位（真机日志：`[手势] 拖动中 窗口现在=(40,61)`
  //    紧接着 `[手势] 触点归零 ⇒ 复位`，拖动只跟了一小段就停）。
  //    现在只信 ImGui 自己的按键状态，另外两道保险：
  //      ① 每次新按下都**重新判定归属**（包括判成"不接管" ⇒ 上一笔一定被清掉）；
  //      ② 拖动结果夹在屏幕内（至少留 120px 可见）⇒ 即使 UP 事件丢了也丢不掉窗口。
  const bool downEffective = down;

  // ★ 组件命中判定（本帧真实 item 矩形；只看本帧的 HoveredId，理由见下面 ①）。
  //   缩放的长按判定也要用它：**按在按钮/滑条上时根本不进长按**，不抢组件触摸。
  ImGuiContext& g = *ImGui::GetCurrentContext();
  const bool hoveredHere = g.HoveredWindow == window && g.HoveredId != 0;
  const bool activeHere = g.ActiveId != 0 && g.ActiveIdWindow == window;
  const bool onComponentNow = hoveredHere || activeHere;

  // ---- ⓪ 右下角长按缩放：交给共用模块（主窗口用的是同一套）----------------
  //   ★ 语义（两个窗口完全一致）：
  //     · 判定区里压着组件时**不是放弃**，而是"长按优先"：满阈值 ⇒ 缩放接管，
  //       并在接管那一刻 ClearActiveID()（被压住的组件既不点击也不残留按下态）；
  //     · 提前松手 / 移动超容差 ⇒ 整笔触摸还给组件（点击、滑条拖动、滚动都不受影响）；
  //     · 长按还在数时间时也不让普通手势抢先提交，否则会一边滚页面一边激活缩放。
  {
    aurora::CornerResizeFrameInput input;
    input.pointerDown = downEffective;
    input.pointerX = mouse.x;
    input.pointerY = mouse.y;
    input.deltaSeconds = io.DeltaTime;
    input.componentHovered = onComponentNow;
    input.cornerRadiusPx = WindowCornerRadiusPx();
    input.label = "二级窗口";
    // ★ 两种缩放方式在这里切换（来自「主题 → 窗口缩放」页，与注册表是同一块内存）。
    //   手势进行中模块内部会**锁定**本次会话的模式（见 CornerResizeRuntime）。
    input.mode = aurora::CurrentResizeMode();
    input.params = aurora::ResizeParamsForSecondaryWindow();
    if (aurora::CornerResizeUpdate(s_CornerResize, window, input)) {
      // 这一笔触摸由缩放**独占**：不滚动、不拖窗口、不点按钮
      s_GestureOwner = aurora::PanelGesture::None;
      s_GesturePending = aurora::PanelGesture::None;
      s_ScrollInertia.Stop();
      return;
    }
  }

  // ---- ① 按下：判定归属（只判定一次）------------------------------------
  if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
    s_ScrollInertia.Stop();
    // 专用拖动区 = 窗口顶部一条**看不见**的把手（标题栏已按用户要求删除，
    // 但窗口仍然要能拖）。组件优先的判定在 ResolvePanelGesture 里。
    const float dragStripH = lgx::px(40.0f);
    const bool onDragStrip = !window->Collapsed
        && mouse.x >= window->Pos.x && mouse.x <= window->Pos.x + window->Size.x
        && mouse.y >= window->Pos.y && mouse.y <= window->Pos.y + dragStripH;
    // ★ 组件优先：命中判定用 **ImGui 的真实 item 矩形**，不是"像素是否透明"。
    //   · `g.HoveredId` 只由 ButtonBehavior 那条路设置（开关/进度条/按钮/标签都在内），
    //     **普通文字不会设置它** ⇒ 在文字上滑动照样能滚页面（这点特意查过 imgui.cpp:3979/4155）。
    //   · ★★ 只看**本帧**的 HoveredId，**不要**看 HoveredIdPreviousFrame：
    //     手指跳到按下点时，上一帧的 hover 还停在旧位置 ⇒ 会把"按在空白处"
    //     误判成"按在组件上" ⇒ **窗口就拖不动了**（真机实测踩到）。
    //   · 再按"窗口"收紧：主题窗口里的控件不该让二级窗口放弃滚动。
    const bool onComponent = onComponentNow;
    const bool inWindow = mouse.x >= window->Pos.x && mouse.x <= window->Pos.x + window->Size.x
        && mouse.y >= window->Pos.y && mouse.y <= window->Pos.y + window->Size.y;
    s_GesturePending = aurora::ResolvePanelGesture(onDragStrip, onComponent, inWindow);
    s_GestureOwner = aurora::PanelGesture::None;      // 还没提交：等移动阈值
    if (s_GesturePending != s_GestureLoggedPending) {
      s_GestureLoggedPending = s_GesturePending;
      GestureLog("[手势] 按下 (%.0f,%.0f) 归属=%s（拖动条=%d 组件=%d 窗口内=%d）"
                 " 窗口=(%.0f,%.0f)-(%.0f,%.0f) 长按区=右下 %.0fpx",
           mouse.x, mouse.y, GestureName(s_GesturePending),
           (int)onDragStrip, (int)onComponent, (int)inWindow,
           window->Pos.x, window->Pos.y,
           window->Pos.x + window->Size.x, window->Pos.y + window->Size.y,
           lgx::px(aurora::CornerResizeHoldParams().zoneDp));
    }
    s_GestureStartMouse = mouse;
    s_GestureStartWindow = window->Pos;
    s_GestureStartScroll = window->Scroll.y;
    s_GestureLastMouseY = mouse.y;
    s_GestureVelocity = 0.0f;
  }

  // ---- ② 超过移动阈值 ⇒ 提交（这就是"点击"与"拖动"的分界）----------------
  if (downEffective && s_GestureOwner == aurora::PanelGesture::None
      && s_GesturePending != aurora::PanelGesture::None) {
    const float threshold = aurora::PanelGestureThreshold(true);   // 触摸：给宽一点
    if (aurora::PanelGestureExceeds(mouse.x - s_GestureStartMouse.x,
                                    mouse.y - s_GestureStartMouse.y, threshold)) {
      s_GestureOwner = s_GesturePending;
      if (s_GestureOwner != s_GestureLoggedOwner) {
        s_GestureLoggedOwner = s_GestureOwner;
        GestureLog("[手势] 超过阈值 → 执行 %s", GestureName(s_GestureOwner));
      }
    }
  }

  // ---- ③ 执行 -----------------------------------------------------------
  // 拖动跟随探针：AURORA_GESTURE_TRACE=1 时每 10 帧打一行（默认一条都不打）
  if (s_GestureTrace && s_GestureOwner == aurora::PanelGesture::WindowDrag) {
    static int tick = 0;
    if ((tick++ % 10) == 0) {
      GestureLog("[手势] 拖动中 鼠标=(%.0f,%.0f) 窗口现在=(%.0f,%.0f)",
                 mouse.x, mouse.y, window->Pos.x, window->Pos.y);
    }
  }

  if (downEffective && s_GestureOwner == aurora::PanelGesture::Scroll) {
    // 跟手：手指位移 1:1 映射到滚动（SetScrollY 内部夹到 [0, ScrollMax]）。
    ImGui::SetScrollY(window, s_GestureStartScroll - (mouse.y - s_GestureStartMouse.y));
    s_GestureVelocity = aurora::UpdatePanelVelocity(
        s_GestureVelocity, s_GestureLastMouseY - mouse.y, io.DeltaTime);
    s_GestureLastMouseY = mouse.y;
  } else if (!downEffective) {
    // ---- ④ 松手：滚动交给惯性；窗口拖动立即停住（不飘）------------------
    if (s_GestureOwner == aurora::PanelGesture::Scroll) s_ScrollInertia.Start(s_GestureVelocity);
    s_GestureOwner = aurora::PanelGesture::None;
    s_GesturePending = aurora::PanelGesture::None;
    s_GestureVelocity = 0.0f;
  }

  // ---- ⑤ 惯性：继续滑，撞到顶/底立刻停（不允许越界位移）------------------
  if (s_ScrollInertia.Active() && !downEffective) {
    const float moved = s_ScrollInertia.Step(io.DeltaTime, window->Scroll.y, window->ScrollMax.y);
    if (moved != 0.0f) {
      ImGui::SetScrollY(window, aurora::ClampPanelScroll(
          window->Scroll.y + moved, window->ScrollMax.y));
    }
  }

}

void Draw() {
  // ★ 每帧先清零"想不想要文字输入"：只有本帧真的画到输入框并把它激活才会置真。
  //   这一行是"键盘收起后还能再叫出来"的另一半保证（另一半在 aurora_overlay.cpp
  //   的 ImeHost::consumeHiddenEvent + Renderer::requestTextInputEnd）。
  BeginTextInputFrame();
  if (!s_Visible.load(std::memory_order_relaxed)) {
    ClearTouchRect();
    return;
  }
  const ImGuiIO& io = ImGui::GetIO();
  const float screenW = fmaxf(1.0f, io.DisplaySize.x);
  const float screenH = fmaxf(1.0f, io.DisplaySize.y);

  // ---- 二级窗口布局：与主题窗口共用同一套算法（aurora::ui_layout）----
  //
  //   原来是"760 宽、700 高，夹到屏内"，和主题窗口各自为政：竖屏时两者
  //   宽度一致看着还行，横屏时二级窗口占满整宽、主题窗口被挤到屏幕外。
  //   现在统一由 UiLayout 按朝向分配：竖屏上下堆叠、横屏左右并排，
  //   两个窗口永远完整落在屏内且互不重叠。
  aurora::ui_layout::Input layoutInput;
  layoutInput.screenW = screenW;
  layoutInput.screenH = screenH;
  const aurora::ui_layout::Layout layout = aurora::ui_layout::Compute(layoutInput);
  float defW = layout.secondary.Valid() ? layout.secondary.w : fminf(760.0f, screenW - 32.0f);
  float defH = layout.secondary.Valid() ? layout.secondary.h : fminf(700.0f, screenH - 96.0f);
  float defX = layout.secondary.Valid() ? layout.secondary.x : 16.0f;
  float defY = layout.secondary.Valid() ? layout.secondary.y : 16.0f;
  // 长按缩放算出的锚点仍然优先（那是用户主动指定的落点，比默认布局更权威）。
  {
    std::lock_guard<std::mutex> lock(s_AnchorMutex);
    if (s_AnchorValid) {
      const float margin = lgx::px(8.0f);
      defW = fminf(defW, s_Anchor[2] - margin * 2.0f);
      defH = fminf(defH, s_Anchor[3] - margin * 2.0f);
      defX = s_Anchor[0] + margin;
      defY = s_Anchor[1] + margin;
    }
  }
  defW = fmaxf(240.0f, defW);
  defH = fmaxf(180.0f, defH);
  defX = fmaxf(0.0f, fminf(defX, screenW - defW));
  defY = fmaxf(0.0f, fminf(defY, screenH - defH));

  // 屏幕尺寸变了（横竖屏切换 / 分屏 / 折叠屏展开）就强制重排一次；
  // 尺寸没变时用 FirstUseEver，保留用户自己拖好的位置与大小。
  static float lastScreenW = 0.0f;
  static float lastScreenH = 0.0f;
  const bool relayout =
      aurora::ui_layout::NeedsRelayout(lastScreenW, lastScreenH, screenW, screenH);
  if (relayout) {
    lastScreenW = screenW;
    lastScreenH = screenH;
  }
  ImGui::SetNextWindowSize(ImVec2(defW, defH),
                           relayout ? ImGuiCond_Always : ImGuiCond_FirstUseEver);
  ImGui::SetNextWindowPos(ImVec2(defX, defY),
                          relayout ? ImGuiCond_Always : ImGuiCond_FirstUseEver);

  bool open = s_Visible.load(std::memory_order_relaxed);
  // ★★ 手势契约（用户要求）：
  //   NoScrollbar —— **彻底移除**右侧纵向滚动条。这是 ImGui 的真开关：
  //     `ScrollbarY = … && !(flags & NoScrollbar)`（imgui.cpp:6919）⇒
  //     `ScrollbarSizes = (0,0)` ⇒ `InnerRect` 不再扣掉滚动条宽度
  //     ⇒ **右侧布局空间真的还给了内容**（不是只把滚动条藏起来），
  //     右侧那条玻璃边光/折射也不再被"内容裁剪矩形"切掉。
  //     页面滚动改由下面的手势层做（拖拽 + 惯性），不再依赖滚动条。
  //   NoMove —— 关掉 ImGui 自带的"拖标题栏移动窗口"，改由统一手势层处理，
  //     这样**标题栏与标签栏的行为完全一致**（同一阈值、同一套归属判定）。
  // ★★ NoTitleBar（用户要求）：顶上那条标题栏**整个删掉** ——
  //    不再有标题条、折叠三角和 ×，窗口变成**一整块液态玻璃**；
  //    显示/隐藏改由主菜单（主题窗口）的「显示二级液态玻璃窗口」复选框控制。
  //    ★ 拖动没有丢：改到窗口顶部一条看不见的**专用拖动区**（见手势层），
  //      而且组件优先，所以那条上的进度条/开关不会被拖动抢走。
  const ImGuiWindowFlags windowFlags = (Scene().window.glassEnabled
      ? ImGuiWindowFlags_NoBackground : 0)
      | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoMove
      | ImGuiWindowFlags_NoTitleBar
      // ★★ NoResize（用户要求）：把旧的右下角缩放标记**连同它的独立拖动热区**
      //    一起删掉 —— ImGui 的 resize grip 默认就是蓝色（ImGuiCol_ResizeGrip
      //    = 0.26/0.59/0.98），而且 RecordWindowDecorations 还会把它的形状记进
      //    触摸拦截区（secondary_imgui.cpp 里那段有 if (NoResize) return; 提前退出）。
      //    不是藏起来：ImGui 的窗口缩放边界/手柄判定整体关闭，改为下面的
      //    "右下角长按 450ms ⇒ 白色 L 形手柄 ⇒ 拖动改尺寸"。
      | ImGuiWindowFlags_NoResize;
  ApplyPendingWindowDrag();
  aurora::CornerResizeApplyPending(s_CornerResize);   // 长按缩放算出的新尺寸（左上角固定）
  // ★★ 输入法避让（adjustPan 语义）：键盘弹出来时把二级窗口上移，别压住键盘。
  //    同 ImeAvoid.h 的说明：必须先用"当前实际位置 + 已施加位移"还原逻辑位置，
  //    否则每帧都会再叠一次位移、窗口会飞出屏幕。
  {
    const float imeTopNow = (s_HostInfo.imeBottom > s_HostInfo.imeTop)
        ? static_cast<float>(s_HostInfo.imeTop) : 0.0f;
    const float want = aurora::ComputeImeAvoidShift(
        s_ImeAvoidPos[1], s_ImeAvoidSize[1], s_ImeAvoidShift, imeTopNow, 0.0f);
    if (std::fabs(want - s_ImeAvoidShift) > 0.01f) {
      const float y = aurora::ImeAvoidWindowY(s_ImeAvoidPos[1], s_ImeAvoidShift, want);
      ImGui::SetNextWindowPos(ImVec2(s_ImeAvoidPos[0], y), ImGuiCond_Always);
      s_ImeAvoidShift = want;
      GestureLog("[输入法] 二级窗口避让：上移 %.0fpx（键盘顶边 %.0f）", want, imeTopNow);
    }
  }
  if (Scene().window.glassEnabled) {
    const ImU32 kTitleStrip = lgx::glassIsLight()
        ? IM_COL32(244, 246, 250, 170) : IM_COL32(26, 30, 37, 150);
    ImGui::PushStyleColor(ImGuiCol_TitleBg, kTitleStrip);
    ImGui::PushStyleColor(ImGuiCol_TitleBgActive, kTitleStrip);
    ImGui::PushStyleColor(ImGuiCol_TitleBgCollapsed, kTitleStrip);
  }
  const bool begun = ImGui::Begin(Text("二级液态玻璃", "Secondary Liquid Glass"), &open,
                                  windowFlags);
  if (Scene().window.glassEnabled) ImGui::PopStyleColor(3);
  s_Visible.store(open, std::memory_order_relaxed);

  ImGuiWindow* const window = ImGui::GetCurrentWindow();
  // 记下这一帧真正用的位置/尺寸：下一帧的输入法避让要靠它还原逻辑位置。
  if (window != nullptr) {
    s_ImeAvoidPos[0] = window->Pos.x;
    s_ImeAvoidPos[1] = window->Pos.y;
    s_ImeAvoidSize[0] = window->Size.x;
    s_ImeAvoidSize[1] = window->Size.y;
  }
  {
    std::lock_guard<std::mutex> lock(s_TouchRectMutex);
    s_TouchShapes.clear();
    s_TouchWindowId = window->ID;
  }

  if (open) {
    // ★ 这里曾经有一段"把窗口夹进触摸拦截区"的代码，**已撤销** —— 它把拖动废掉了。
    //
    // 初衷是好的：拦截区只能在启动时设一次（Region 替换符号已按 [未核实-2] 禁用），
    // 窗口被拖出去的部分会点不动。但夹取的边界就是拦截区本身（800 宽），
    // 而窗口宽 760 ⇒ **横向只剩 40 像素活动空间**，用户一拖就被弹回，
    // 感觉就是"窗口拖动不了"（用户实测反馈）。
    //
    // 现在的做法：**允许自由拖动**，改用"每次启动都从设计位置开始"来避免窗口
    // 永久漂到框外（ImGui 默认会把位置存进 ini，所以之前会"记住"上一次
    // 被拖走的位置 —— 见 Renderer.cpp 里关掉 io.IniFilename 的那段注释）。
    // 当前宿主按每帧的可见形状更新 SF 输入区域，拖到初始框外也会继续跟随。

    const ImVec2 pos = ImGui::GetWindowPos();
    const ImVec2 size = ImGui::GetWindowSize();
    std::lock_guard<std::mutex> lock(s_TouchRectMutex);
    s_TouchRect[0] = pos.x;
    s_TouchRect[1] = pos.y;
    s_TouchRect[2] = size.x;
    s_TouchRect[3] = size.y;
  } else {
    ClearTouchRect();
  }

  if (open && !window->Hidden) {
    const ImRect bounds = window->Collapsed ? window->TitleBarRect() : window->Rect();
    if (!Scene().window.glassEnabled || window->Collapsed) {
      aurora::VisibleUiShape shape;
      shape.left = bounds.Min.x;
      shape.top = bounds.Min.y;
      shape.right = bounds.Max.x;
      shape.bottom = bounds.Max.y;
      shape.radius = window->WindowRounding;
      shape.shape = shape.radius > 0.0f ? aurora::VisibleUiShapeKind::RoundedRectangle
                                       : aurora::VisibleUiShapeKind::Rectangle;
      shape.clipLeft = window->OuterRectClipped.Min.x;
      shape.clipTop = window->OuterRectClipped.Min.y;
      shape.clipRight = window->OuterRectClipped.Max.x;
      shape.clipBottom = window->OuterRectClipped.Max.y;
      std::lock_guard<std::mutex> lock(s_TouchRectMutex);
      s_TouchShapes.push_back(shape);
    } else if ((window->Flags & ImGuiWindowFlags_NoTitleBar) == 0) {
      // ImGui draws the title before pushing InnerClipRect: only its top corners
      // use WindowRounding, independently of the glass body's 28dp circle SDF.
      const ImRect title = window->TitleBarRect();
      aurora::VisibleUiShape shape;
      shape.left = title.Min.x;
      shape.top = title.Min.y;
      shape.right = title.Max.x;
      shape.bottom = title.Max.y;
      shape.radius = window->WindowRounding;
      shape.shape = shape.radius > 0.0f ? aurora::VisibleUiShapeKind::RoundedRectangle
                                       : aurora::VisibleUiShapeKind::Rectangle;
      shape.roundedCorners = 0x03u;
      shape.clipLeft = window->OuterRectClipped.Min.x;
      shape.clipTop = window->OuterRectClipped.Min.y;
      shape.clipRight = window->OuterRectClipped.Max.x;
      shape.clipBottom = window->OuterRectClipped.Max.y;
      std::lock_guard<std::mutex> lock(s_TouchRectMutex);
      s_TouchShapes.push_back(shape);
    }
    if (Scene().window.glassEnabled && !window->Collapsed) RecordWindowDecorations(window);
  }

  if (begun) {
    const ImVec2 pos = ImGui::GetWindowPos();
    const ImVec2 size = ImGui::GetWindowSize();
    const ImVec2 max(pos.x + size.x, pos.y + size.y);

    // ★★★ 帧边界：把调试工作台攒下的参数更新**一次性**应用。
    //
    //  为什么放在这里（二级窗口绘制之前）而不是帧首：
    //    主题窗口在同一帧里**先于**二级窗口绘制 ⇒ 用户这一帧拖的滑块，
    //    在这一帧的二级窗口里就能看到结果（零帧延迟），
    //    同时 UI 线程从不等 GPU、也从不同步等待任何东西。
    //  合并：同一个参数在两次提交之间的多次写入只保留最新一次（见
    //    Registry::MarkDirty），所以"高频拖动"不会变成高频重建。
    {
      aurora::glass::Registry& registry = aurora::glass::Global();
      // 注册（幂等）→ 每帧确认存在 → 回收消失的目标（动态发现）
      aurora::glass::RegisterSecondaryTargets();
      registry.Commit();
      registry.Sweep();
    }

    lgx::setBackdropEnabled(true);
    if (Scene().window.glassEnabled) {
      // ★ 逐层隔离开关（**只用于定位问题**，默认全关=正常渲染）。
      //   用法：AURORA_NO_SHADOW=1 / _BACKDROP=1 / _EDGE=1 / _LENS=1 / _BLUR=1
      //   为什么要它：左下角那块"淡灰色直角"要靠"关掉某一层它就不见了"来定位，
      //   靠读 shader 推是推不出来的（已经推过一轮，没结论）。
      static const bool kNoShadow = EnvOn("AURORA_NO_SHADOW");
      static const bool kNoBackdrop = EnvOn("AURORA_NO_BACKDROP");
      static const bool kNoEdge = EnvOn("AURORA_NO_EDGE");
      static const bool kNoLens = EnvOn("AURORA_NO_LENS");
      static const bool kNoBlur = EnvOn("AURORA_NO_BLUR");
      lgx::PanelStyle windowStyle = aurora::glass::BuildWindowStyle(Scene().window);
      // 兜底色：跟随主题时用 ImGui 的页面底色（与原实现一致）
      if (Scene().window.fallbackFollowTheme) {
        const ImVec4& pageBg = ImGui::GetStyleColorVec4(ImGuiCol_WindowBg);
        windowStyle.fallback = lgx::Rgba(pageBg.x, pageBg.y, pageBg.z, 1.0f);
      }
      // 逐层隔离诊断开关（只用于定位问题，默认全关 = 正常渲染）
      if (kNoBlur) windowStyle.optics.blurPx = 0.0f;
      if (kNoLens) {
        windowStyle.optics.lensHeightPx = 0.0f;
        windowStyle.optics.lensAmountPx = 0.0f;
        windowStyle.optics.depthAmount = 0.0f;
        windowStyle.optics.spectral = 0.0f;
      }
      if (kNoEdge) windowStyle.edgeOn = false;
      if (kNoShadow) windowStyle.shadowOn = false;
      if (kNoBackdrop) windowStyle.backdropOn = false;

      // ★★★ 缩放激活反馈：整个窗口边框加厚 + 提亮 + 增强折射 ---------------
      //  用的进度就是手柄那一个（同一个弹簧、同一个状态转换 ⇒ 严格同步）。
      //  ★ 只改**视觉样式**：窗口矩形、圆角、内容布局一个字节都不动 ——
      //    "加厚的是窗口的视觉边缘，不引起宽高跳变或内容重排"。
      //  ★ 边缘走的是面板自己的圆角 SDF ⇒ 四个圆角连续，不会出现直角/断裂/
      //    突兀白描边/矩形遮罩（这些正是"画一条粗白框"会踩的坑）。
      //  ★ 进度取自**上一帧**（手势层在本帧绘制之后才跑）⇒ 边框比手柄晚一帧，
      //    120Hz 下 8ms，肉眼不可辨；换来的是"只有一个弹簧实例、绝不会不同步"。
      const aurora::ResizeActivationStyle act =
          aurora::CornerResizeActivationStyle(s_CornerResize);
      if (act.progress > 0.001f) {
        windowStyle.edge.widthDp += act.edgeWidthAddDp;
        windowStyle.edge.softnessDp += act.edgeSoftnessAddDp;
        // 边光颜色的 alpha 字节（常态 0x80）乘增益 ⇒ 边缘高光更亮。
        const unsigned int tintAlpha =
            static_cast<unsigned int>(128.0f * act.edgeTintAlphaGain + 0.5f);
        const unsigned int clamped = tintAlpha > 255u ? 255u : tintAlpha;
        windowStyle.edge.color = (windowStyle.edge.color & 0x00FFFFFFu) | (clamped << 24);
        windowStyle.optics.lensHeightPx *= act.lensHeightGain;
        windowStyle.optics.lensAmountPx *= act.lensAmountGain;
        windowStyle.optics.spectral *= act.dispersionGain;
        if (act.insetOn) {
          windowStyle.insetOn = true;
          windowStyle.inset = lgx::InsetShadow(act.insetReachAddDp, act.insetAlpha);
        }
      }
      // Same bounds, circle radius and clip as the shader, excluding its shadow.
      // ★ 记住"已定型玻璃"的模糊半径：模式 B 的填充前沿要保持这个值，而窗口整体
      //   在过渡期间会被临时降低（见下），所以必须在降低之前取。
      const float settledBlurPx = windowStyle.optics.blurPx;

      // ★★★ 模式 B：松手后的"液态玻璃填充"—— 几何 + 强度 ------------------
      //  三种同步的变化（用户要求的动画模型）：
      //    A. Geometry   真窗口尺寸每帧从旧边界插值到目标边界（不是截图拉伸）
      //    B. Glass Fill 移动前沿那条带上的模糊/折射/色散/透明度渐变（1 → 0）
      //    C. Edge Flow  窗口边缘的柔化与折射随当前尺寸连续更新（0 → 峰值 → 0）
      //  几何在 ResizePreview.h（ComputeResizeRims，有离线回归），这里只提交面板。
      const aurora::ResizeFillEnvelope fillEnv =
          aurora::CornerResizeFillEnvelopeOf(s_CornerResize);
      const aurora::ResizeRims fillRims = aurora::CornerResizeFillRims(
          s_CornerResize, window, WindowCornerRadiusPx());
      const aurora::ResizePreviewParams& previewParams = aurora::ResizeSettingsRef().preview;
      const bool fillActive = s_CornerResize.commit.active && previewParams.fillOn
          && fillEnv.fill > 0.002f && (fillRims.right.valid || fillRims.bottom.valid);
      const float edgeFlow = aurora::CornerResizeEdgeFlow(s_CornerResize);
      if (s_CornerResize.commit.active && edgeFlow > 0.001f) {
        // C. 边缘动画：柔边 + 轻微加厚 + 折射增强，全部随当前尺寸连续变化
        windowStyle.edge.softnessDp += previewParams.edgeFlowSoftnessDp * edgeFlow;
        windowStyle.edge.widthDp += 0.35f * edgeFlow;
        windowStyle.optics.lensAmountPx *= (1.0f + 0.18f * edgeFlow);
      }
      if (fillActive && previewParams.fillBlurDiffDp > 0.0f) {
        // ★ "前沿更糊"是**降低窗口整体模糊**做出来的，不是给前沿加模糊：
        //   窗口模糊默认 10dp 已经是 ytbl 的上限（BLUR_RADIUS_MAX_DP，GlassConfig.h:69），
        //   加上去会被内部夹掉 —— 那会变成一个"转了没反应"的假控件。
        //   降窗口 ⇒ 前沿那条带保持配置值 ⇒ 两者之间出现**真实的模糊差**，
        //   动画收尾时窗口模糊回到配置值，差值自然归零（不留残影）。
        const float diff = previewParams.fillBlurDiffDp * fillEnv.fill * lgx::px(1.0f);
        windowStyle.optics.blurPx =
            windowStyle.optics.blurPx > diff ? windowStyle.optics.blurPx - diff : 0.0f;
      }
      if (open && !window->Hidden) {
        RecordTouchShape(lgx::Box(pos.x, pos.y, max.x, max.y),
                         windowStyle.corners.topLeft);
      }
      { const lgx::Box panelBox(pos.x, pos.y, max.x, max.y);
        ScopedPanelClip clip(windowStyle, panelBox);
        lgx::drawPanel(windowStyle, panelBox);
        // ★ 登记整窗口矩形：选中"整窗口"时高亮整个二级窗口外壳。
        aurora::glass::debughl::TryMarkRect(
            "secondary_window",
            aurora::glass::debughl::Rect{panelBox.left, panelBox.top, panelBox.right, panelBox.bottom});

        // ★★ 均匀"激活描边"层：让**四条边**同步加厚（见 ResizeActivationParams 的说明）
        //   · 同一套圆角 SDF ⇒ 四个圆角连续，不会出现直角/断裂/矩形遮罩；
        //   · 只有边光：背景采样/模糊/投影/表面色全部关闭 ⇒ 开销小、不改变原配方；
        //   · 非激活时（rimAlpha≈0）**整层不提交** ⇒ 常态零成本、零视觉影响。
        if (act.rimAlpha > 0.002f && act.rimWidthDp > 0.001f && !kNoEdge) {
          lgx::PanelStyle rimStyle;
          rimStyle.corners = windowStyle.corners;      // 与外壳同一套圆角
          rimStyle.backdropOn = false;
          rimStyle.shadowOn = false;
          rimStyle.insetOn = false;
          rimStyle.surfaceMode = lgx::SurfaceMode::None;
          rimStyle.alpha = act.rimAlpha;
          rimStyle.edgeOn = true;
          rimStyle.edge = lgx::EdgeLight::plain();
          rimStyle.edge.widthDp = act.rimWidthDp;
          rimStyle.edge.softnessDp = 0.20f;
          lgx::drawPanel(rimStyle, panelBox);
        }

        // ★★★ 模式 B：正在填充的那块玻璃（移动前沿）-------------------------
        //   · **只有贴着前沿的一条带**参与（不是整块新区域）⇒ 窗口中间不会留下
        //     一条矩形接缝/残影；带的内边缘一旦越过老边界就夹在老边界上。
        //   · 面板复用窗口自己的配方（同一套圆角 SDF、同一套背光采样与折射），
        //     只把"还没定型"的那几项按 fill 曲线加上去，随动画收敛回普通玻璃。
        //   · 两块带互不重叠（右下角由横向带负责），所以不会双倍着色。
        //   · 画在**内容之前** ⇒ Demo 永远在最上层，填充动画不会遮挡它。
        if (fillActive) {
          const float fillGain = fillEnv.fill;
          const float refract = 1.0f + (previewParams.fillRefractGain - 1.0f) * fillGain;
          const float spectral =
              1.0f + (previewParams.fillSpectralGain - 1.0f) * fillGain;
          auto drawFrontier = [&](const aurora::ResizeRim& rim) {
            if (!rim.valid) return;
            if (rim.right - rim.left <= 0.5f || rim.bottom - rim.top <= 0.5f) return;
            lgx::PanelStyle style = windowStyle;    // 与外壳同一套配方
            style.corners = lgx::Corners(rim.cornerTopLeft, rim.cornerTopRight,
                                         rim.cornerBottomRight, rim.cornerBottomLeft);
            style.edgeOn = false;                   // 前沿不是窗口外轮廓，不画边光
            style.shadowOn = false;
            style.insetOn = false;
            style.alpha = previewParams.fillAlpha * fillGain;
            // 前沿保持**已定型玻璃**的模糊（窗口整体此刻被临时降低了）
            style.optics.blurPx = settledBlurPx;
            style.optics.lensHeightPx *= (1.0f + (refract - 1.0f) * 0.5f);
            style.optics.lensAmountPx *= refract;
            style.optics.spectral *= spectral;
            const lgx::Box rimBox(rim.left, rim.top, rim.right, rim.bottom);
            ScopedPanelClip clip(style, rimBox);
            lgx::drawPanel(style, rimBox);
          };
          drawFrontier(fillRims.right);
          drawFrontier(fillRims.bottom);

          // 前沿柔光：用 ImGui 的多色矩形做一条**平滑**渐隐（零 GPU 面板成本），
          // 把前沿玻璃带的内边缘化开 —— 看起来是玻璃在流动，而不是贴了一条矩形。
          const float glowAlpha = previewParams.fillGlowAlpha * fillEnv.edgeFlow;
          if (glowAlpha > 0.003f) {
            const float glowWidth = previewParams.fillGlowWidthDp * lgx::px(1.0f);
            ImDrawList* dl = ImGui::GetWindowDrawList();
            const int peak = static_cast<int>(glowAlpha * 255.0f + 0.5f);
            const ImU32 clear = IM_COL32(255, 255, 255, 0);
            const ImU32 solid = IM_COL32(255, 255, 255, peak);
            const aurora::ResizeRim& rightRim = fillRims.right;
            const aurora::ResizeRim& bottomRim = fillRims.bottom;
            if (rightRim.valid) {
              // 渐隐铺在前沿的**内侧**（左→右由透明到峰值），带内再补一层均匀柔光
              dl->AddRectFilledMultiColor(
                  ImVec2(rightRim.left - glowWidth, rightRim.top),
                  ImVec2(rightRim.left, rightRim.bottom), clear, solid, solid, clear);
              dl->AddRectFilled(ImVec2(rightRim.left, rightRim.top),
                                ImVec2(rightRim.right, rightRim.bottom),
                                IM_COL32(255, 255, 255, peak / 3));
            }
            if (bottomRim.valid) {
              dl->AddRectFilledMultiColor(
                  ImVec2(bottomRim.left, bottomRim.top - glowWidth),
                  ImVec2(bottomRim.right, bottomRim.top), clear, clear, solid, solid);
              dl->AddRectFilled(ImVec2(bottomRim.left, bottomRim.top),
                                ImVec2(bottomRim.right, bottomRim.bottom),
                                IM_COL32(255, 255, 255, peak / 3));
            }
          }
        }
      }
    }
    // ★★ 内容可视区裁剪（用户要求：Demo 不许突破窗口圆角轮廓）
    //    外壳是圆角的，而 ImGui 的裁剪只是**矩形 scissor** ⇒ 贴近圆角的组件会按
    //    矩形继续画、突破轮廓。这里把**窗口自己的圆角几何**同时喂给两条绘制路径：
    //      · ImGui 画的内容   → 后端 shader 的 SDF 遮罩（draw callback 切换）
    //      · ytbl 的玻璃面板  → lgx::setContentMask（提交面板时快照进 job）
    //    用 SDF 覆盖率而不是矩形 scissor ⇒ 圆角是**带抗锯齿**的过渡，
    //    也不会在矩形边缘突然切断。
    static float s_ContentMask[5] = {0.0f, 0.0f, 0.0f, 0.0f, -1.0f};
    const float contentRadius = WindowCornerRadiusPx();   // 与外壳、手柄同一套圆角参数
    s_ContentMask[0] = pos.x + size.x * 0.5f;
    s_ContentMask[1] = pos.y + size.y * 0.5f;
    s_ContentMask[2] = size.x * 0.5f;
    s_ContentMask[3] = size.y * 0.5f;
    s_ContentMask[4] = contentRadius;
    ImDrawList* contentList = ImGui::GetWindowDrawList();
    contentList->AddCallback(ContentMaskSetCallback, s_ContentMask);
    lgx::setContentMask(s_ContentMask[0], s_ContentMask[1], s_ContentMask[2], s_ContentMask[3],
                        contentRadius, io.DisplaySize.y);
    // ★★ 内容和实时信息都必须在遮罩**之内**。
    //
    //   用户报的 bug：「窗口里面的字体没有严格限制在圆角范围内，冒出右上角的圆角」。
    //   根因就是下面这一行原来在 setContentMask 清除**之后**：
    //       DrawContents();                                  ← 在遮罩内
    //       lgx::setContentMask(0,0,0,0,-1,0);               ← 清除
    //       DrawHostDiagnostics();                           ← 在遮罩外，所以会出圆角
    //   删掉两条分隔线后实时信息紧贴输入区、更靠上，正好撞到右上角圆角，问题才显形。
    //
    //   现在两者都在遮罩范围内：先画内容，再画实时信息，最后才清除遮罩。
    DrawContents();
    DrawHostDiagnostics();
    lgx::setContentMask(0.0f, 0.0f, 0.0f, 0.0f, -1.0f, 0.0f);   // 外壳/后续不受影响
    contentList->AddCallback(ContentMaskClearCallback, nullptr);
    RunSecondaryWindowGestures(window);

    // 角部缩放手柄：几何每帧从窗口实时矩形现算，进度与**边框加厚**共用同一个
    // 弹簧（s_Activation）⇒ 圆弧展开与边框加厚严格同步、可一起被打断/反向。
    aurora::CornerResizeDrawHandle(s_CornerResize, window, WindowCornerRadiusPx());
    // 模式 B 的虚线预览框：同样画在**前景层**。前景层的裁剪矩形是整个视口
    // （imgui.cpp:4388）⇒ 预览框画到原窗口之外也不会被窗口的矩形缓冲截断。
    aurora::CornerResizeDrawPreview(s_CornerResize, window, WindowCornerRadiusPx());
  }
  ImGui::End();
}

// ★★ 旧的 Draw*Controls / DrawHostControls 已经**整体删除**。
//    它们是"每个组件一组共用 static 变量"时代的产物：五枚按钮共用一组光学参数、
//    开关/滑块/导航栏的配方根本够不着。现在这些控件由液态玻璃调试工作台
//    （GlassWorkbench.cpp）按**注册表**自动生成 —— 一个参数一个控件，
//    每个目标一份独立存储，且控件只在真实绑定了渲染字段的参数上出现。

}  // namespace secondary_imgui
