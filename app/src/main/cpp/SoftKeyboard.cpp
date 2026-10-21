#include "SoftKeyboard.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include <imgui.h>

#include "ytbl/GlassUI.h"

namespace aurora::softkb {
namespace {

bool& VisibleSlot() {
  static bool visible = false;
  return visible;
}

// 上一次点击输入框的时间：点在同一次按压里不要立刻又被"点外面收起"关掉。
double& LastRequestTime() {
  static double t = -1000.0;
  return t;
}

// 键盘面板的矩形（屏幕坐标）：Draw 时记录，供"点外面收起"和测试使用。
float& PanelTop() {
  static float v = 0.0f;
  return v;
}

constexpr float kMinRatio = 0.26f;   // 太小点不准
constexpr float kMaxRatio = 0.42f;   // 太大挡内容

// 键盘按键的视觉风格：用玻璃 UI 的圆角矩形，半透明深色底 + 白字。
constexpr ImU32 kKeyBg = IM_COL32(38, 40, 46, 232);
constexpr ImU32 kKeyBgActive = IM_COL32(70, 110, 170, 245);
constexpr ImU32 kKeyBgFunc = IM_COL32(28, 30, 36, 232);
constexpr ImU32 kKeyText = IM_COL32(238, 240, 245, 255);

// 一行里最多几个键：决定单个键的宽度。数字行 10 个最宽，QWERTY 行 10 个。
constexpr int kMaxKeys = 10;

struct Key final {
  const char* label;   // 显示的字符
  const char* shift;   // Shift 状态下的字符（nullptr = 与 label 相同）
  float fx;            // 相对宽度（1.0 = 一个标准键）
  int action;          // 0 = 字符，1 = 退格，2 = 回车，3 = 清空，4 = 收起，5 = 空格
};

enum KeyAction { kChar = 0, kBackspace, kEnter, kClear, kHide, kSpace };

// 布局：数字行在最上面（调试参数以数字为主），下面是 QWERTY。
// fx 是相对宽度，用于让退格/回车这类功能键更宽。
struct Row final {
  const Key* keys;
  int count;
};

const Key kRowDigits[] = {
    {"1", "!", 1, kChar}, {"2", "@", 1, kChar}, {"3", "#", 1, kChar}, {"4", "$", 1, kChar},
    {"5", "%", 1, kChar}, {"6", "^", 1, kChar}, {"7", "&", 1, kChar}, {"8", "*", 1, kChar},
    {"9", "(", 1, kChar}, {"0", ")", 1, kChar},
};

const Key kRowQwer[] = {
    {"q", "Q", 1, kChar}, {"w", "W", 1, kChar}, {"e", "E", 1, kChar}, {"r", "R", 1, kChar},
    {"t", "T", 1, kChar}, {"y", "Y", 1, kChar}, {"u", "U", 1, kChar}, {"i", "I", 1, kChar},
    {"o", "O", 1, kChar}, {"p", "P", 1, kChar},
};

const Key kRowAsdf[] = {
    {"a", "A", 1, kChar}, {"s", "S", 1, kChar}, {"d", "D", 1, kChar}, {"f", "F", 1, kChar},
    {"g", "G", 1, kChar}, {"h", "H", 1, kChar}, {"j", "J", 1, kChar}, {"k", "K", 1, kChar},
    {"l", "L", 1, kChar},
};

const Key kRowZxcv[] = {
    {"Shift", nullptr, 1.7f, kChar},   // Shift 键（用文字，字体里没有 ⇧ 字形）
    {"z", "Z", 1, kChar}, {"x", "X", 1, kChar}, {"c", "C", 1, kChar}, {"v", "V", 1, kChar},
    {"b", "B", 1, kChar}, {"n", "N", 1, kChar}, {"m", "M", 1, kChar},
    {".", ">", 1, kChar}, {"-", "_", 1, kChar},
};

const Key kRowBottom[] = {
    {"清空", nullptr, 1.4f, kClear},
    {"空格", nullptr, 4.0f, kSpace},
    {"退格", nullptr, 1.6f, kBackspace},
    {"回车", nullptr, 1.6f, kEnter},
    {"收起", nullptr, 1.4f, kHide},
};

const Row kRows[] = {
    {kRowDigits, static_cast<int>(std::size(kRowDigits))},
    {kRowQwer, static_cast<int>(std::size(kRowQwer))},
    {kRowAsdf, static_cast<int>(std::size(kRowAsdf))},
    {kRowZxcv, static_cast<int>(std::size(kRowZxcv))},
    {kRowBottom, static_cast<int>(std::size(kRowBottom))},
};

constexpr int kRowCount = static_cast<int>(std::size(kRows));

bool& ShiftSlot() {
  static bool shift = false;
  return shift;
}

// ---- 把一次按键送进 ImGui 的输入队列 ----
//
// 为什么不是直接改字符串：ImGui 的 InputText 维护自己的编辑状态
// （光标位置、选区、undo 栈、滚动）。绕过它改 s_ImeText 会让光标错位，
// 也会让 Ctrl+Z 失效。正确做法是让它以为"用户按了一个键"。
void EmitChar(unsigned int c) {
  ImGuiIO& io = ImGui::GetIO();
  io.AddInputCharacter(c);
}

void EmitKey(ImGuiKey key) {
  ImGuiIO& io = ImGui::GetIO();
  io.AddKeyEvent(key, true);
  io.AddKeyEvent(key, false);
}

// 画一个键，返回是否被按下。手写矩形 + 文字，不用 ImGui::Button：
// 目的是让键盘在玻璃窗口之外、用固定配色，不受工作台主题影响。
bool DrawKey(const char* label, ImVec2 pos, ImVec2 size, bool isFunc, bool isActive) {
  ImDrawList* dl = ImGui::GetForegroundDrawList();
  if (dl == nullptr) return false;

  const ImVec2 a = pos;
  const ImVec2 b(pos.x + size.x, pos.y + size.y);
  const ImVec2 mouse = ImGui::GetIO().MousePos;
  const bool hover = mouse.x >= a.x && mouse.x <= b.x && mouse.y >= a.y && mouse.y <= b.y;

  // 圆角 8px：和窗口外壳的圆角语言一致，但小一号（键很小）。
  const float rounding = std::min(8.0f, std::min(size.x, size.y) * 0.22f);
  const ImU32 bg = isActive ? kKeyBgActive : (isFunc ? kKeyBgFunc : kKeyBg);
  const ImU32 bgHover = isActive ? kKeyBgActive : (isFunc ? kKeyBgFunc : kKeyBg);
  dl->AddRectFilled(a, b, hover ? bgHover : bg, rounding);

  // 按下时整体压暗一点，给"按下感"
  const ImVec2 ts = ImGui::CalcTextSize(label);
  const ImVec2 tp(a.x + (size.x - ts.x) * 0.5f, a.y + (size.y - ts.y) * 0.5f);
  dl->AddText(tp, kKeyText, label);

  // 命中形：整个矩形都算，不需要精确判定（手指不可能点到 1px 误差）
  return hover && ImGui::IsMouseClicked(ImGuiMouseButton_Left);
}

}  // namespace

bool Visible() { return VisibleSlot(); }

void RequestShow() {
  VisibleSlot() = true;
  LastRequestTime() = ImGui::GetTime();
}

void Hide() {
  VisibleSlot() = false;
  ShiftSlot() = false;
}

int MaxKeysPerRow() { return kMaxKeys; }

float HeightRatioFor(int screenW, int screenH) {
  if (screenW <= 0 || screenH <= 0) return kMinRatio;
  // 竖屏时屏幕窄而高 ⇒ 键按宽度算会很矮，用大一点的比例；
  // 横屏时相反，键本来就宽，比例小一点省空间。
  const float aspect = static_cast<float>(screenH) / static_cast<float>(screenW);
  const float r = (aspect > 1.6f) ? 0.36f : 0.30f;
  return std::clamp(r, kMinRatio, kMaxRatio);
}

void Draw() {
  if (!VisibleSlot()) return;

  const ImGuiIO& io = ImGui::GetIO();
  const float screenW = io.DisplaySize.x;
  const float screenH = io.DisplaySize.y;
  if (screenW <= 0.0f || screenH <= 0.0f) return;

  const float ratio = HeightRatioFor(static_cast<int>(screenW), static_cast<int>(screenH));
  const float panelH = screenH * ratio;
  const float panelY = screenH - panelH;
  PanelTop() = panelY;

  ImDrawList* dl = ImGui::GetForegroundDrawList();
  if (dl == nullptr) return;

  // ---- 背板：一整块半透明深色圆角矩形，把屏幕底部盖住 ----
  dl->AddRectFilled(ImVec2(0.0f, panelY), ImVec2(screenW, screenH),
                    IM_COL32(16, 17, 20, 246), 18.0f, ImDrawFlags_RoundCornersTop);

  // ---- 布局 ----
  const float padX = 8.0f;
  const float padY = 8.0f;
  const float innerW = screenW - padX * 2.0f;
  const float rowGap = 5.0f;
  const float rowH = (panelH - padY * 2.0f - rowGap * (kRowCount - 1)) / kRowCount;
  if (rowH <= 4.0f) return;   // 屏幕太小，画不了

  bool shift = ShiftSlot();
  float y = panelY + padY;

  for (int r = 0; r < kRowCount; ++r) {
    // 每行的总相对宽度：用来把 fx 换算成实际像素宽
    float totalFx = 0.0f;
    for (int k = 0; k < kRows[r].count; ++k) totalFx += kRows[r].keys[k].fx;
    const float gap = 4.0f;
    const float usable = innerW - gap * (kRows[r].count - 1);
    if (totalFx <= 0.0f) continue;

    float x = padX;
    for (int k = 0; k < kRows[r].count; ++k) {
      const Key& key = kRows[r].keys[k];
      const float w = usable * (key.fx / totalFx);

      // Shift 键只在第 4 行第 0 个位置（label 是 ⇧）
      const bool isShiftKey = (r == 3 && k == 0);
      const bool isFunc = (key.action != kChar) || isShiftKey;
      const char* label = key.label;
      if (key.action == kChar && shift && key.shift != nullptr && !isShiftKey) {
        label = key.shift;
      }

      const bool pressed = DrawKey(label, ImVec2(x, y), ImVec2(w, rowH), isFunc,
                                   isShiftKey && shift);
      if (pressed) {
        // ---- 按键处理 ----
        switch (key.action) {
          case kChar:
            if (isShiftKey) {
              shift = !shift;
            } else if (key.label != nullptr && key.label[0] != '\0') {
              const char* ch = (shift && key.shift != nullptr) ? key.shift : key.label;
              // 只取第一个字节：本键盘的字符都是单字节 ASCII。
              // 中文不在这里（需要输入法引擎，见头文件说明）。
              EmitChar(static_cast<unsigned int>(static_cast<unsigned char>(ch[0])));
              // Shift 后自动回弹（和手机键盘一致）
              if (shift) shift = false;
            }
            break;
          case kBackspace:
            EmitKey(ImGuiKey_Backspace);
            break;
          case kEnter:
            EmitKey(ImGuiKey_Enter);
            break;
          case kSpace:
            EmitChar(' ');
            break;
          case kClear: {
            // Ctrl+A 然后 Backspace：交给 InputText 自己处理，保住 undo 栈
            ImGuiIO& eio = ImGui::GetIO();
            eio.AddKeyEvent(ImGuiMod_Ctrl, true);
            eio.AddKeyEvent(ImGuiKey_A, true);
            eio.AddKeyEvent(ImGuiKey_A, false);
            eio.AddKeyEvent(ImGuiMod_Ctrl, false);
            EmitKey(ImGuiKey_Backspace);
            break;
          }
          case kHide:
            Hide();
            break;
          default:
            break;
        }
      }
      x += w + gap;
    }
    y += rowH + rowGap;
  }
  ShiftSlot() = shift;

  // ---- 点击键盘以外的地方 ⇒ 收起 ----
  //   用"上一帧的鼠标按下"而不是这一帧：输入框被点中的那一帧也会走到这里，
  //   若按这一帧判定，点输入框会立刻把刚弹出的键盘关掉。
  if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
    const ImVec2 m = io.MousePos;
    const bool insidePanel = (m.y >= panelY);
    if (!insidePanel) {
      // 但要避开"刚刚才点输入框"的那一帧（同一帧内请求 + 判定会互相打架）
      if (ImGui::GetTime() - LastRequestTime() > 0.25) {
        Hide();
      }
    }
  }
}

}  // namespace aurora::softkb
