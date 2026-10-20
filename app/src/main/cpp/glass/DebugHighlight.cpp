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

// 高亮配色：亮青色描边 + 淡填充。刻意选一个**与玻璃主题色都不同**的颜色，
// 保证在任何背景（深色/浅色/彩色条纹）上都看得见。
constexpr unsigned int kEdgeColor = 0xFF47E0FFu;  // ABGR: 不透明、偏青
constexpr unsigned int kFillColor = 0x2A47E0FFu;  // ABGR: 低 alpha 填充

constexpr float kEdgeThickness = 3.0f;
constexpr float kCornerLength = 26.0f;   // 四角的短线长度，比画整圈更像"取景框"
constexpr float kLabelPadding = 6.0f;

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

bool TryMarkRect(const char* targetId, const Rect& rect) {
  if (targetId == nullptr || rect.Valid() == false) return false;
  const std::string& target = TargetSlot();
  if (target.empty() || target != targetId) return false;
  RectSlot() = rect;
  return true;
}

Rect MarkedRect() { return RectSlot(); }

void DrawDebugHighlight() {
  const Rect rect = RectSlot();
  if (rect.Valid() == false) return;

  ImDrawList* draw = ImGui::GetForegroundDrawList();
  if (draw == nullptr) return;

  const ImVec2 a(rect.x0, rect.y0);
  const ImVec2 b(rect.x1, rect.y1);

  // 淡填充压在内容上，但 alpha 很低，不会真的挡住底下
  draw->AddRectFilled(a, b, kFillColor, 0.0f, 0);

  // 四角取景框：只画每角两条短线。比整圈描边干净，也不会和玻璃自身的边光混淆。
  const float cx = kCornerLength;
  const float t = kEdgeThickness;
  // 左上
  draw->AddLine(ImVec2(a.x, a.y), ImVec2(a.x + cx, a.y), kEdgeColor, t);
  draw->AddLine(ImVec2(a.x, a.y), ImVec2(a.x, a.y + cx), kEdgeColor, t);
  // 右上
  draw->AddLine(ImVec2(b.x - cx, a.y), ImVec2(b.x, a.y), kEdgeColor, t);
  draw->AddLine(ImVec2(b.x, a.y), ImVec2(b.x, a.y + cx), kEdgeColor, t);
  // 左下
  draw->AddLine(ImVec2(a.x, b.y - cx), ImVec2(a.x, b.y), kEdgeColor, t);
  draw->AddLine(ImVec2(a.x, b.y), ImVec2(a.x + cx, b.y), kEdgeColor, t);
  // 右下
  draw->AddLine(ImVec2(b.x - cx, b.y), ImVec2(b.x, b.y), kEdgeColor, t);
  draw->AddLine(ImVec2(b.x, b.y - cx), ImVec2(b.x, b.y), kEdgeColor, t);

  // 左上角贴一个标签，写清楚"调试中：XXX"
  const char* name = HumanNameOf(TargetSlot().c_str());
  if (name == nullptr) name = TargetSlot().c_str();
  if (name == nullptr || name[0] == '\0') return;

  char label[192];
  std::snprintf(label, sizeof label, "调试中：%s", name);

  const ImVec2 textSize = ImGui::CalcTextSize(label);
  const ImVec2 labelMin(a.x, a.y - textSize.y - kLabelPadding * 2.0f);
  const ImVec2 labelMax(a.x + textSize.x + kLabelPadding * 2.0f, a.y);
  // 标签可能跑到屏幕外（目标贴顶边）⇒ 挪到框内侧
  const bool inside = labelMin.y < 0.0f;
  const ImVec2 bgMin = inside ? ImVec2(a.x, a.y) : labelMin;
  const ImVec2 bgMax = inside ? ImVec2(a.x + textSize.x + kLabelPadding * 2.0f,
                                        a.y + textSize.y + kLabelPadding * 2.0f)
                              : labelMax;
  draw->AddRectFilled(bgMin, bgMax, kEdgeColor, 0.0f, 0);
  draw->AddText(ImVec2(bgMin.x + kLabelPadding, bgMin.y + kLabelPadding),
                IM_COL32(10, 20, 26, 255), label);
}

const char* HumanNameOf(const char* targetId) {
  if (targetId == nullptr) return nullptr;
  // 只覆盖会对屏幕产生可见区域的目标；其余返回 nullptr，调用方回退到 id。
  struct Entry {
    const char* id;
    const char* name;
  };
  static const Entry kTable[] = {
      {"secondary_window", "整窗口"},
      {"secondary_window.card", "液态玻璃卡片"},
      {"secondary_window.playground", "玻璃实验场"},
      {"secondary_window.resize", "窗口缩放"},
      {"secondary_window.menu", "弹出菜单"},
      {"secondary_window.navigation", "导航栏"},
      {"pill_button_0", "按钮 1"},
      {"pill_button_1", "按钮 2"},
      {"pill_button_2", "按钮 3"},
      {"pill_button_3", "按钮 4"},
  };
  for (const Entry& entry : kTable) {
    if (std::strcmp(entry.id, targetId) == 0) return entry.name;
  }
  // 芯片 / 开关 / 滑杆这类批量生成的 id 用前缀识别
  struct Prefix {
    const char* prefix;
    const char* name;
  };
  static const Prefix kPrefixes[] = {
      {"glass_chip_", "玻璃芯片"},
      {"glass_toggle_", "开关"},
      {"glass_slider_", "滑杆"},
  };
  for (const Prefix& prefix : kPrefixes) {
    if (std::strncmp(prefix.prefix, targetId, std::strlen(prefix.prefix)) == 0) {
      return prefix.name;
    }
  }
  return nullptr;
}

}  // namespace aurora::glass::debughl
