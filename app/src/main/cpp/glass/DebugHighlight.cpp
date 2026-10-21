#include "DebugHighlight.h"

#include <cstring>
#include <string>

#include <imgui.h>

namespace aurora::glass::debughl {
namespace {

// 当前调试目标 id。跨帧保留（面板选中状态），BeginDebugFrame 不动它。
std::string& TargetSlot() {
  static std::string id;
  return id;
}

// 本帧命中到的矩形。BeginDebugFrame 会清掉。
Rect& RectSlot() {
  static Rect rect;
  return rect;
}

// 「调整窗口」开关：只有它为真时才画取景框。
// 用户原话：「当你在主题窗口的第一个液态玻璃分页开启调整窗口的时候，
// 才会显示那个正在调试的选着界面」—— 关掉时屏幕上必须干干净净。
bool& AdjustEnabled() {
  static bool enabled = false;
  return enabled;
}

// 淡入淡出用的动画量：0 = 完全隐藏，1 = 完全显示。
// 直接跳变会显得很生硬（用户原话「看起来不会那么生硬」）。
float& FadeSlot() {
  static float fade = 0.0f;
  return fade;
}

// 上一帧画过的矩形：切换目标/关闭开关时，让框从旧位置滑到新位置而不是瞬移。
Rect& PreviousRectSlot() {
  static Rect rect;
  return rect;
}

// 高亮配色：亮青色描边。刻意选一个**与玻璃主题色都不同**的颜色，
// 保证在任何背景（深色/浅色/彩色条纹）上都看得见。
constexpr unsigned int kEdgeColor = 0xFF47E0FFu;  // ABGR: 不透明、偏青

constexpr float kEdgeThickness = 3.0f;
constexpr float kCornerLength = 26.0f;   // 四角的短线长度，比画整圈更像"取景框"
constexpr float kFadeSpeed = 8.0f;       // 每秒收敛速度（约 125ms 到位）
constexpr float kCornerRadius = 2.0f;

inline float Clamp01(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }

// 把颜色按透明度缩放（IM_COL32 的 alpha 在最高字节）。
inline unsigned int WithAlpha(unsigned int abgr, float alpha) {
  const unsigned int a = static_cast<unsigned int>(Clamp01(alpha) * 255.0f + 0.5f) & 0xFFu;
  return (abgr & 0x00FFFFFFu) | (a << 24);
}

}  // namespace

void BeginDebugFrame() { RectSlot() = Rect{}; }

void SetDebugTarget(const char* targetId) {
  std::string& slot = TargetSlot();
  if (targetId == nullptr || targetId[0] == '\0') {
    slot.clear();
    return;
  }
  slot.assign(targetId);
}

const char* DebugTarget() { return TargetSlot().c_str(); }

void SetAdjustMode(bool enabled) { AdjustEnabled() = enabled; }

bool AdjustMode() { return AdjustEnabled(); }

bool TryMarkRect(const char* targetId, const Rect& rect) {
  if (targetId == nullptr || rect.Valid() == false) return false;
  // 开关关着就根本不认领矩形 —— 连计算都省掉。
  if (!AdjustEnabled()) return false;
  const std::string& target = TargetSlot();
  if (target.empty() || target != targetId) return false;
  RectSlot() = rect;
  return true;
}

Rect MarkedRect() { return RectSlot(); }

void DrawDebugHighlight() {
  // ---- 动画：目标出现/消失都靠 fade 过渡，避免闪烁式的生硬切换 ----
  const Rect rect = RectSlot();
  const bool wantVisible = AdjustEnabled() && rect.Valid();

  float& fade = FadeSlot();
  const float dt = ImGui::GetIO().DeltaTime;
  const float step = (dt > 0.0f ? dt : 1.0f / 60.0f) * kFadeSpeed;
  fade += wantVisible ? step : -step;
  fade = Clamp01(fade);

  if (fade <= 0.001f) {
    // 完全隐藏时清掉上一帧矩形，下次出现从头淡入（不从旧位置滑过来）
    PreviousRectSlot() = Rect{};
    return;
  }
  if (!rect.Valid()) return;

  // ---- 位置平滑：目标尺寸/位置变化时插值过去，而不是跳过去 ----
  Rect& prev = PreviousRectSlot();
  Rect shown = rect;
  if (prev.Valid()) {
    const float k = Clamp01(step);
    shown.x0 = prev.x0 + (rect.x0 - prev.x0) * k;
    shown.y0 = prev.y0 + (rect.y0 - prev.y0) * k;
    shown.x1 = prev.x1 + (rect.x1 - prev.x1) * k;
    shown.y1 = prev.y1 + (rect.y1 - prev.y1) * k;
  }
  prev = shown;

  // ★★ 画在**背景层**而不是前景层。
  //
  //   用户原话：「假如当时我的焦点是在那个调试页面，调试页面会把这四个选中的
  //   角角给盖住在他们之上，而不是像现在的这四个角角永远在调试界面之上」。
  //
  //   前景层（GetForegroundDrawList）永远在最后一个窗口之上，所以调试面板
  //   挡不住它。改到背景层之后，任何窗口都会盖住它 —— 这正是要的层级：
  //   四角属于"屏幕上的目标"，不属于"界面"，界面在它上面。
  ImDrawList* draw = ImGui::GetBackgroundDrawList();
  if (draw == nullptr) return;

  const unsigned int edge = WithAlpha(kEdgeColor, fade);
  const ImVec2 a(shown.x0, shown.y0);
  const ImVec2 b(shown.x1, shown.y1);

  // 四角取景框：只画每角两条短线（用户要求"只保留选中的四个边角"）。
  // 不画整圈描边 —— 那会和玻璃自身的边光混淆，也显得笨重。
  const float cx = kCornerLength * (0.6f + 0.4f * fade);   // 出现时长出来一点点
  const float t = kEdgeThickness;
  // 左上
  draw->AddLine(ImVec2(a.x, a.y), ImVec2(a.x + cx, a.y), edge, t);
  draw->AddLine(ImVec2(a.x, a.y), ImVec2(a.x, a.y + cx), edge, t);
  // 右上
  draw->AddLine(ImVec2(b.x - cx, a.y), ImVec2(b.x, a.y), edge, t);
  draw->AddLine(ImVec2(b.x, a.y), ImVec2(b.x, a.y + cx), edge, t);
  // 左下
  draw->AddLine(ImVec2(a.x, b.y - cx), ImVec2(a.x, b.y), edge, t);
  draw->AddLine(ImVec2(a.x, b.y), ImVec2(a.x + cx, b.y), edge, t);
  // 右下
  draw->AddLine(ImVec2(b.x - cx, b.y), ImVec2(b.x, b.y), edge, t);
  draw->AddLine(ImVec2(b.x, b.y - cx), ImVec2(b.x, b.y), edge, t);

  (void)kCornerRadius;
}

const char* HumanNameOf(const char* targetId) {
  if (targetId == nullptr) return nullptr;
  struct Entry {
    const char* id;
    const char* name;
  };
  // 只列**当前真实存在**的调试目标。
  static const Entry kTable[] = {
      {"secondary_window", "整窗口"},
      {"secondary_window.resize", "窗口缩放"},
  };
  for (const Entry& entry : kTable) {
    if (std::strcmp(entry.id, targetId) == 0) return entry.name;
  }
  return nullptr;
}

}  // namespace aurora::glass::debughl
