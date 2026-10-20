// =============================================================================
//  调试工作台实现（见 GlassWorkbench.h 的三条职责）
// =============================================================================
#include "GlassWorkbench.h"

#include "ParamRowLayout.h"   // ★ 参数行的响应式布局内核（纯逻辑，有离线回归）

#include "glass/GlassHistory.h"
#include "glass/GlassPreset.h"
#include "glass/GlassRegistry.h"
#include "glass/GlassScene.h"
#include "GlassTargets.h"

#include "imgui.h"
#include "imgui_internal.h"

#include <android/log.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <string>
#include <vector>

namespace aurora {
namespace glass {
namespace {

constexpr char kLogTag[] = "AuroraGlass";
constexpr double kAutoSaveDelaySeconds = 2.0;

// AURORA_UI_TRACE=1：把参数行的**实际**布局与控件实际宽度打出来（默认一条都不打）。
//  ★ 为什么要它：这一轮的根因（ImGui 表格的 WidthFixed 列宽只在第一帧生效）光看代码
//    推不出来 —— 它是"窗口拉宽了但滑块纹丝不动"这种**动态**现象。
//    有了它，真机上能直接读到"可用宽度 / 算出的滑块宽度 / 控件实测宽度"三个数，
//    而不是靠截图目测。
bool UiTrace() {
  static const bool on = [] {
    const char* v = std::getenv("AURORA_UI_TRACE");
    return v != nullptr && v[0] == '1';
  }();
  return on;
}

/**
 * 布局诊断可视化（用户任务书第八节）：`AURORA_LAYOUT_DEBUG=1` 时给参数行的四个格子
 * 画出**实际布局矩形**（名称/控件/数值/重置 各一色），并描出整行的可用区边界。
 *
 * 用它一眼就能看出"绘制位置"与"预算位置"是否一致 —— 本轮那个"滑块画回第一行"
 * 的 bug 如果当时有这个开关，截图里会看到滑块矩形**跑到名称那一行**，
 * 不必从"看起来重叠"倒推。
 *
 * 默认关闭；开启判定只做一次静态初始化，正常使用时**零每帧开销**。
 */
bool LayoutDebugEnabled() {
  static const bool on = [] {
    const char* v = std::getenv("AURORA_LAYOUT_DEBUG");
    return v != nullptr && v[0] == '1';
  }();
  return on;
}

/** 画一个诊断矩形（只在 AURORA_LAYOUT_DEBUG=1 时生效）。 */
void LayoutDebugRect(float x, float y, float w, float h, unsigned int color) {
  if (!LayoutDebugEnabled()) return;
  ImGui::GetWindowDrawList()->AddRect(ImVec2(x, y), ImVec2(x + w, y + h), color, 0.0f, 0, 3.0f);
}

// -----------------------------------------------------------------------------
//  参数行的度量：**每个目标量一次**（最长名字 / 最长数值），
//  真正的列宽由 ParamRowLayout.h 按"当前可用宽度"**每帧**算 —— 这是本轮修复的核心。
//
//  ★★ 为什么不再用 ImGui 的四列表格（旧实现就是这么写的、也是坏在这里）：
//    `TableSetupColumn(..., WidthFixed, w)` 的 w **只在表格第一帧生效**
//    （imgui_tables.cpp:1572-1585 整段被 `if (table->IsInitializing)` 包着，
//      而 IsInitializing 只在表格首次创建时置位 :548、在 BeginTable 结束时清掉 :1516）
//    ⇒ 之后每帧算出来的新列宽被**静默忽略**：窗口拉宽，滑块一动不动；
//    ⇒ 而且表格里没有 WidthStretch 列时，余量不会分给任何列（:958-983 只分配给
//      WidthStretch 列）⇒ 加宽窗口只是右边多出一块空白。
//    ⇒ 再加上旧代码在窄窗口下把 `usable` 夹到最小 300px、控件列夹到最小 110px，
//      列宽之和会**大于**可用宽度 ⇒ 最后一列（重置）被表格的裁剪矩形切掉；
//      而列宽又是锁死的 ⇒ 之后把窗口拉宽也不会恢复（用户报的"拉伸也没用"）。
// -----------------------------------------------------------------------------
float WidestParamName(const Target& target) {
  float widest = 0.0f;
  for (const ParamSlot& slot : target.params) {
    const float width = ImGui::CalcTextSize(slot.desc.cn.c_str()).x;
    if (width > widest) widest = width;
  }
  return widest;
}

// -----------------------------------------------------------------------------
// 放不下就换行 —— ImGui **不会**自动换行，一行控件超出窗口宽度就会被右边缘直接切掉
// （用户实测反馈："好多按钮都没有完全显示"）。所有并排按钮都要过这个判据。
//
// ★ 宽度必须用"下一个控件的真实宽度"（文字宽 + 左右 FramePadding），不能写死像素：
//   字体、密度、语言（中/英）都会改变文字宽度。
// ★★ 剩余空间必须用**上一个控件的右边界**去算，绝不能用 GetContentRegionAvail()：
//   Button/Checkbox 返回之后，ImGui 已经把游标挪到**下一行的行首**了，
//   那时 GetContentRegionAvail().x 给的是"整行宽度"而不是"本行剩余宽度"
//   ⇒ 条件恒成立 ⇒ 所有控件照样挤在同一行、照样被切掉。
//   （第一版就是这么写错的：代码改了、编译过了、真机截图里按钮还是被切。）
// -----------------------------------------------------------------------------
void SameLineOrWrap(const char* nextLabel) {
  const ImGuiStyle& style = ImGui::GetStyle();
  const float needed = ImGui::CalcTextSize(nextLabel).x + style.FramePadding.x * 2.0f;
  const float lastRight = ImGui::GetItemRectMax().x;
  const float contentRight = ImGui::GetWindowPos().x + ImGui::GetContentRegionMax().x;
  if (contentRight - lastRight - style.ItemSpacing.x >= needed) ImGui::SameLine();
}

// -----------------------------------------------------------------------------
// 把一段文本**截断到放得下**（超出部分用 "…" 收尾）。
//
//  ★ 为什么必须做：ImGui 的 CollapsingHeader / Text 不会自动换行，也不会缩字号，
//    文字比窗口宽时它只在裁剪矩形处**硬切** —— 真机上看到的就是
//    "背景采样  （已改 3" 这种贴着右边缘的断口（用户报的"文字溢出"）。
//  ★ 按 UTF-8 逐码点回退，保证不会把一个汉字切成半个（那会渲染成问号）。
//  ★ "…" 必须在内嵌字体里有字形 —— 它已在 UiGlyphSet 的检查范围内
//    （tools/gen_ui_glyphs.py --check 会守住这条）。
// -----------------------------------------------------------------------------
std::string FitTextToWidth(const char* text, float maxWidth) {
  if (text == nullptr) return std::string();
  std::string full(text);
  if (maxWidth <= 0.0f) return std::string();
  if (ImGui::CalcTextSize(full.c_str()).x <= maxWidth) return full;

  const char* ellipsis = "…";
  const float ellipsisWidth = ImGui::CalcTextSize(ellipsis).x;
  if (maxWidth <= ellipsisWidth) return std::string(ellipsis);

  // 逐码点累加，直到再加一个字符就放不下为止
  std::size_t cut = 0;
  std::size_t index = 0;
  float width = 0.0f;
  while (index < full.size()) {
    const unsigned char lead = static_cast<unsigned char>(full[index]);
    std::size_t step = 1;
    if ((lead & 0xE0u) == 0xC0u) step = 2;
    else if ((lead & 0xF0u) == 0xE0u) step = 3;
    else if ((lead & 0xF8u) == 0xF0u) step = 4;
    if (index + step > full.size()) break;
    const std::string piece = full.substr(index, step);
    const float pieceWidth = ImGui::CalcTextSize(piece.c_str()).x;
    if (width + pieceWidth + ellipsisWidth > maxWidth) break;
    width += pieceWidth;
    index += step;
    cut = index;
  }
  std::string out = full.substr(0, cut);
  out += ellipsis;
  return out;
}

std::string FormatValue(const ParamSlot& slot) {
  char buffer[96];
  const ParamValue value = slot.Read();
  switch (slot.desc.kind) {
    case ParamKind::Float:
      std::snprintf(buffer, sizeof(buffer), "%.3f", static_cast<double>(value.f));
      break;
    case ParamKind::Int:
      std::snprintf(buffer, sizeof(buffer), "%d", value.i);
      break;
    case ParamKind::Enum: {
      // 枚举显示名字而不是数字 —— 数字在调试时没有意义
      int index = value.i - slot.desc.minI;
      if (index < 0) index = 0;
      int seen = 0;
      std::string label = "?";
      const char* cursor = slot.desc.enumLabels.c_str();
      const char* end = cursor + slot.desc.enumLabels.size();
      while (cursor < end) {
        const std::size_t length = std::strlen(cursor);
        if (seen == index) {
          label = std::string(cursor, length);
          break;
        }
        ++seen;
        cursor += length + 1;
      }
      std::snprintf(buffer, sizeof(buffer), "%s", label.c_str());
      break;
    }
    case ParamKind::Bool:
      std::snprintf(buffer, sizeof(buffer), "%s", value.b ? "开" : "关");
      break;
    case ParamKind::Color:
      // ★ 颜色显示成 `#RRGGBBAA` 而不是"1.00 0.00 0.00 1.00"：
      //   后者 19 个字符在数值列里一定会被裁，而且逐通道浮点对人眼不直观。
      //   完整的 4 通道浮点值在悬停提示里给。
      std::snprintf(buffer, sizeof(buffer), "#%02X%02X%02X%02X",
                    static_cast<int>(value.rgba[0] * 255.0f + 0.5f),
                    static_cast<int>(value.rgba[1] * 255.0f + 0.5f),
                    static_cast<int>(value.rgba[2] * 255.0f + 0.5f),
                    static_cast<int>(value.rgba[3] * 255.0f + 0.5f));
      break;
  }
  return std::string(buffer);
}

// 本目标里最长的"数值显示文本"宽度（数值列的宽度依据）。
// ★ 必须用**真实的 FormatValue 结果**去量，不能按最坏情况（"#RRGGBBAA" 九字符）
//   预留 —— 那样数值列会吃掉 200+ 像素，把控件列挤成一条缝（真机截图确认过：
//   滑块只剩 60px、数值直接不见了）。
float WidestValue(const Target& target) {
  float widest = 0.0f;
  for (const ParamSlot& slot : target.params) {
    if (!slot.desc.supported) continue;
    const float width = ImGui::CalcTextSize(FormatValue(slot).c_str()).x;
    if (width > widest) widest = width;
  }
  return widest;
}

// 把"这一行要用到的字体度量"打包给布局内核（内核是纯逻辑，不认识 ImGui）
ParamRowMetrics MeasureRow(const Target& target) {
  const ImGuiStyle& style = ImGui::GetStyle();
  ParamRowMetrics m;
  m.nameText = WidestParamName(target);
  m.valueText = WidestValue(target);
  m.resetText = ImGui::CalcTextSize("重置").x;
  m.framePaddingX = style.FramePadding.x;
  m.spacing = style.ItemSpacing.x;
  return m;
}

const char* CostLabel(ParamCost cost) {
  switch (cost) {
    case ParamCost::Uniform: return "即时（uniform）";
    case ParamCost::EffectChain: return "帧边界（效果链）";
    case ParamCost::Pipeline: return "帧边界（采样/管线）";
  }
  return "?";
}

// -----------------------------------------------------------------------------
// 工作台上下文：注册表 / 预设 / 历史 / UI 状态
// -----------------------------------------------------------------------------
struct Workbench {
  Registry* registry = nullptr;
  PresetStore* presets = nullptr;
  History* history = nullptr;
  WorkbenchDiagnostics diagnostics;
  bool ready = false;
  bool beginDone = false;

  // UI 状态
  int page = 0;
  int sublayer = 0;
  char search[64] = {};
  bool searchActive = false;
  bool showDiagnostics = false;
  bool showGaps = false;
  int editingParam = -1;         // 正在被"数值输入框"编辑的参数下标
  bool editingValue = false;
  char presetName[48] = {};
  char ioPath[160] = {"/data/local/tmp/aurora_glass_params.json"};
  std::string status;
  double statusStamp = 0.0;
  double lastChangeStamp = 0.0;

  // A/B 对比
  Snapshot slotA;
  Snapshot slotB;
  bool hasA = false;
  bool hasB = false;
  bool showingB = false;

  void SetStatus(const std::string& text);
};

Workbench& WB() {
  static Workbench workbench;
  return workbench;
}

double NowSeconds() {
  struct timespec ts {};
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return static_cast<double>(ts.tv_sec) + static_cast<double>(ts.tv_nsec) / 1.0e9;
}

void HistorySink(void* user, const ParamChange& change) {
  Workbench* workbench = static_cast<Workbench*>(user);
  if (workbench == nullptr || workbench->history == nullptr) return;
  // 撤销回放本身会产生 change（origin == Reset 且正在 replay），
  // History 内部有 replaying_ 守卫，这里再挡一次"非用户意图"的来源。
  workbench->history->Record(change);
  // ★★ 哪些来源算"用户改过、需要落盘"：
  //    Ui        主窗口拖的滑块
  //    Reset     单项 / 整组 / 全部恢复默认
  //    Preset    切换预设、A/B、导入
  //    Component ★ 手指在**二级窗口里**点的开关 / 拖的滑杆 / 点的标签栏
  //  最后这条漏过一次，是端到端验收抓出来的：在二级窗口里把开关点开之后，
  //  参数文件**没有任何变化** —— 用户看到的就是"我改的东西没被记住"。
  //  它和主窗口拖滑块一样是真实的参数变化，必须一起落盘。
  //  （Test 来源是脚本/回归用的，不该把参数文件写脏。）
  if (change.origin != ChangeOrigin::Test) {
    workbench->lastChangeStamp = NowSeconds();
    if (workbench->presets != nullptr) workbench->presets->MarkDirty();
  }
}

void Workbench::SetStatus(const std::string& text) {
  status = text;
  statusStamp = NowSeconds();
}

// -----------------------------------------------------------------------------
// 参数行
// -----------------------------------------------------------------------------
void DrawParamTooltip(const ParamSlot& slot, const SourceRef* source) {
  if (!ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) return;
  ImGui::BeginTooltip();
  ImGui::PushTextWrapPos(ImGui::GetFontSize() * 26.0f);
  ImGui::Text("%s", slot.desc.cn.c_str());
  ImGui::TextDisabled("%s（%s）", slot.desc.en.c_str(), slot.desc.id.c_str());
  ImGui::Separator();
  ImGui::Text("类型    %s%s%s", ParamKindName(slot.desc.kind),
              slot.desc.unit.empty() ? "" : " · 单位 ",
              slot.desc.unit.c_str());
  const std::string range = RangeText(slot.desc);
  if (!range.empty()) ImGui::Text("值域    %s", range.c_str());
  ImGui::Text("默认    %s", slot.def.ToText().c_str());
  ImGui::Text("成本    %s", CostLabel(slot.desc.cost));
  if (!slot.desc.constraint.empty()) ImGui::Text("约束    %s", slot.desc.constraint.c_str());
  if (!slot.desc.help.empty()) {
    ImGui::Separator();
    ImGui::TextWrapped("%s", slot.desc.help.c_str());
  }
  if (source != nullptr && !source->file.empty()) {
    ImGui::Separator();
    ImGui::TextDisabled("实现位置  %s : %s", source->file.c_str(), source->symbol.c_str());
  }
  ImGui::PopTextWrapPos();
  ImGui::EndTooltip();
}

// 返回 true 表示本行发生了修改（用于统计/状态提示）
//
// ★★ 布局是本文件的**核心不变量**（见 ParamRowLayout.h 与离线回归）：
//    每一行都由当前可用宽度现算，四块内容永远待在可视区内：
//      宽屏：名称 | 可伸缩滑块 | 数值 | 重置
//      窄屏：名称 | 数值 | 重置    ← 第一行
//            滑块（占满整行）        ← 第二行
//    画法上用 SetCursorScreenPos 精确摆放每一格（而不是 SameLine 的相对偏移），
//    因为"跟手重排"要求每一格的 x 都由布局算出来，不能依赖上一格的实际宽度。
bool DrawParamRow(const std::string& targetId, Target& target, std::size_t paramIndex,
                  const ParamRowLayout& layout, Workbench& workbench) {
  ParamSlot& slot = target.params[paramIndex];
  Registry& registry = *workbench.registry;
  const ImGuiStyle& style = ImGui::GetStyle();
  bool changed = false;

  ImGui::PushID(static_cast<int>(paramIndex));

  const ImVec2 rowStart = ImGui::GetCursorScreenPos();
  const float lineH = ImGui::GetFrameHeight();          // 一行的高度（滑块/按钮都是这个高）
  const float textH = ImGui::GetTextLineHeight();
  const float textY = rowStart.y + (lineH - textH) * 0.5f;   // 文字与控件垂直居中对齐

  // ---- 名称格（**裁剪在自己的宽度内**：长名字只会被截断，绝不压到滑块上）----
  {
    ImGui::SetCursorScreenPos(ImVec2(rowStart.x, textY));
    ImGui::PushClipRect(ImVec2(rowStart.x, rowStart.y),
                        ImVec2(rowStart.x + layout.name, rowStart.y + lineH), true);
    if (!slot.desc.supported) {
      ImGui::TextDisabled("%s", slot.desc.cn.c_str());
    } else if (slot.Read().Equals(slot.def)) {
      ImGui::TextUnformatted(slot.desc.cn.c_str());
    } else {
      // 偏离默认值的参数用高亮标出来：一眼就能看出"我改过哪些"
      ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.35f, 1.0f), "%s", slot.desc.cn.c_str());
    }
    // 悬停提示要在裁剪矩形**之内**取（IsItemHovered 说的是上一个 item）
    DrawParamTooltip(slot, GetSourceRef(targetId, slot.desc.id));
    ImGui::PopClipRect();
    LayoutDebugRect(rowStart.x, rowStart.y, layout.name, lineH, IM_COL32(255, 90, 90, 220));
  }

  // 不支持的参数（能力缺口）：只读一行说明，没有控件
  if (!slot.desc.supported) {
    ImGui::SetCursorScreenPos(ImVec2(rowStart.x + layout.name + style.ItemSpacing.x + 8.0f,
                                     textY));
    ImGui::PushClipRect(ImVec2(rowStart.x + layout.name + style.ItemSpacing.x + 8.0f, rowStart.y),
                        ImVec2(rowStart.x + layout.available, rowStart.y + lineH), true);
    ImGui::TextDisabled("%s", slot.desc.unsupportedReason.c_str());
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) {
      ImGui::SetTooltip("这条能力在实现里不存在，因此**不提供控件**（而不是给一个转了没反应的滑块）");
    }
    ImGui::PopClipRect();
    ImGui::SetCursorScreenPos(
        ImVec2(rowStart.x, rowStart.y + lineH + style.ItemSpacing.y));
    ImGui::PopID();
    return false;
  }

  const bool followTheme = (slot.desc.id == "fallback" &&
                            target.GetB("fallbackFollowTheme", false)) ||
                           (slot.desc.id == "edgeTint" &&
                            target.GetB("edgePresetTint", false));
  if (followTheme) ImGui::BeginDisabled();

  const ParamValue value = slot.Read();

  // ---- 控件格（滑块 / 下拉 / 开关 / 取色 / 数值直输）----
  //  ★★★ `y` 必须由调用者传进来：两行模式下控件在**第二行**，而它曾经被硬编码成
  //    `rowStart.y`（第一行）—— 于是滑块被画回第一行，**压住参数名与数值框**，
  //    同时第二行留成一片空白。用户报的"滑块与数值重叠 / 不同参数的控件互相覆盖 /
  //    极大的空白区域 / 纵向间距完全不统一"**全部由这一个坐标错误引起**。
  auto drawControl = [&](float x, float width, float y) {
    ImGui::SetCursorScreenPos(ImVec2(x, y));
    LayoutDebugRect(x, y, width, ImGui::GetFrameHeight(), IM_COL32(90, 255, 130, 220));
    if (workbench.editingParam == static_cast<int>(paramIndex) && slot.desc.IsNumeric()) {
      // 数值直接输入模式（点数值进入；回车/失焦提交）
      if (slot.desc.kind == ParamKind::Float) {
        float edited = value.f;
        ImGui::SetNextItemWidth(width);
        const bool done = ImGui::InputFloat("##input", &edited, slot.desc.stepF,
                                            slot.desc.stepF * 10.0f, "%.4f",
                                            ImGuiInputTextFlags_EnterReturnsTrue);
        registry.SetF(targetId, slot.desc.id, edited, ChangeOrigin::Ui);
        changed = true;
        if (done || ImGui::IsItemDeactivated()) workbench.editingParam = -1;
      } else {
        int edited = value.i;
        ImGui::SetNextItemWidth(width);
        const bool done = ImGui::InputInt("##input", &edited, slot.desc.stepI,
                                          slot.desc.stepI * 10,
                                          ImGuiInputTextFlags_EnterReturnsTrue);
        if (registry.SetI(targetId, slot.desc.id, edited, ChangeOrigin::Ui)) changed = true;
        if (done || ImGui::IsItemDeactivated()) workbench.editingParam = -1;
      }
      return;
    }
    switch (slot.desc.kind) {
      case ParamKind::Float: {
        float edited = value.f;
        ImGui::SetNextItemWidth(width);
        if (ImGui::SliderFloat("##slider", &edited, slot.desc.minF, slot.desc.maxF, "%.3f")) {
          registry.SetF(targetId, slot.desc.id, edited, ChangeOrigin::Ui);
          changed = true;
#if defined(__ANDROID__)
          {
            static FILE* uiFile = std::fopen("/data/local/tmp/aurora_probe.log", "a");
            if (uiFile != nullptr) {
              std::fprintf(uiFile, "UI-DRAG %s / %s = %.4f\n", targetId.c_str(),
                           slot.desc.id.c_str(), static_cast<double>(edited));
              std::fflush(uiFile);
            }
          }
#endif
        }
        break;
      }
      case ParamKind::Int: {
        int edited = value.i;
        ImGui::SetNextItemWidth(width);
        if (ImGui::SliderInt("##slider", &edited, slot.desc.minI, slot.desc.maxI)) {
          registry.SetI(targetId, slot.desc.id, edited, ChangeOrigin::Ui);
          changed = true;
        }
        break;
      }
      case ParamKind::Enum: {
        int edited = value.i;
        ImGui::SetNextItemWidth(width);
        if (ImGui::Combo("##combo", &edited, slot.desc.enumLabels.c_str())) {
          registry.SetI(targetId, slot.desc.id, edited, ChangeOrigin::Ui);
          changed = true;
        }
        break;
      }
      case ParamKind::Bool: {
        bool edited = value.b;
        if (ImGui::Checkbox("##check", &edited)) {
          registry.SetB(targetId, slot.desc.id, edited, ChangeOrigin::Ui);
          changed = true;
        }
        break;
      }
      case ParamKind::Color: {
        float edited[4] = {value.rgba[0], value.rgba[1], value.rgba[2], value.rgba[3]};
        ImGui::SetNextItemWidth(width);
        if (ImGui::ColorEdit4("##color", edited,
                              ImGuiColorEditFlags_AlphaBar |
                                  ImGuiColorEditFlags_AlphaPreviewHalf |
                                  ImGuiColorEditFlags_NoInputs)) {
          registry.SetColor(targetId, slot.desc.id, edited[0], edited[1], edited[2],
                            edited[3], ChangeOrigin::Ui);
          changed = true;
        }
        break;
      }
    }
  };

  // ---- 数值格（点击进入精确输入）----
  auto drawValue = [&](float x, float y) {
    ImGui::SetCursorScreenPos(ImVec2(x, y));
    LayoutDebugRect(x, y, layout.value, lineH, IM_COL32(90, 160, 255, 220));
    const std::string text = FormatValue(slot);
    // ★★ 这里**必须**用 Selectable 而不是 Button：
    //    `ImGui::Button(label, ImVec2(w,0))` 里的 w 只是**最小**宽度，文字比它长时
    //    按钮会自己变宽 ⇒ 把后面的控件整个挤出去。Selectable 的 size 是**硬宽度**，
    //    文字超长只会被裁在自己的格子里，绝不撑破布局。
    ImGui::PushStyleVar(ImGuiStyleVar_SelectableTextAlign, ImVec2(0.5f, 0.5f));
    if (ImGui::Selectable(text.c_str(), false, ImGuiSelectableFlags_None,
                          ImVec2(layout.value, lineH))) {
      if (slot.desc.IsNumeric()) workbench.editingParam = static_cast<int>(paramIndex);
    }
    ImGui::PopStyleVar();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) {
      ImGui::SetTooltip(slot.desc.IsNumeric()
                            ? "点击直接输入数值（当前精确值 %s）"
                            : "该类型不支持直接输入（当前值 %s）",
                        slot.Read().ToText().c_str());
    }
  };

  // ---- 重置格（**永远完整可见**：布局已经把它的宽度扣出来了）----
  auto drawReset = [&](float x, float y) {
    ImGui::SetCursorScreenPos(ImVec2(x, y));
    LayoutDebugRect(x, y, layout.reset, lineH, IM_COL32(255, 200, 60, 220));
    const bool modified = !slot.Read().Equals(slot.def);
    if (!modified) ImGui::BeginDisabled();
    if (ImGui::Button("重置", ImVec2(layout.reset, lineH))) {
      registry.ResetParam(targetId, slot.desc.id, ChangeOrigin::Reset);
      changed = true;
    }
    if (!modified) ImGui::EndDisabled();
    // 实测宽度对照（AURORA_UI_TRACE=1）：证明"按钮完整落在安全区内"而不是靠肉眼
    if (UiTrace()) {
      const float actual = ImGui::GetItemRectSize().x;
      if (actual > layout.reset + 0.5f) {
        __android_log_print(ANDROID_LOG_WARN, kLogTag,
                            "[布局] 重置按钮实测 %.1f > 预算 %.1f（可用 %.1f）",
                            actual, layout.reset, layout.available);
      }
    }
    // ★★ 宿主侧硬自检（**始终开启**，每次比较一个浮点数）：整行最右边的控件
    //    必须落在窗口内容区之内。纯逻辑回归（tests/ParamRowLayoutTest）只能证明
    //    "给定宽度算得对"，证明不了"传进去的宽度真的是可用宽度" —— 真机上就是
    //    在这里翻车的（可用宽度被算成光标之后的零头 ⇒ 名称列被压成几十像素）。
    //    所以这一条必须留在渲染路径上：以后谁再传错宽度，日志里立刻能看见。
    {
      static bool warnedOnce = false;
      const float contentRight =
          ImGui::GetWindowPos().x + ImGui::GetWindowContentRegionMax().x;
      const float itemRight = ImGui::GetItemRectMax().x;
      if (!warnedOnce && itemRight > contentRight + 0.5f) {
        warnedOnce = true;
        __android_log_print(ANDROID_LOG_WARN, kLogTag,
                            "[布局] 重置按钮右边界 %.1f 越出内容区 %.1f（预算 重置=%.1f "
                            "名称=%.1f 滑块=%.1f 数值=%.1f 可用=%.1f）→ 说明传给布局的"
                            "可用宽度不对",
                            itemRight, contentRight, layout.reset, layout.name,
                            layout.slider, layout.value, layout.available);
      }
    }
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) {
      ImGui::SetTooltip("恢复为组件注册时的值（%s）", slot.def.ToText().c_str());
    }
  };

  const float spacing = style.ItemSpacing.x;
  if (layout.twoLine) {
    // 窄屏（**当前默认走这条**：本工程字号/内边距都是 2 倍，单行放不下四格）：
    //   第一行 = 参数名（左） + 数值（右对齐） + 重置（最右）
    //   第二行 = 滑块，占满整行可用宽度
    const float resetX = rowStart.x + layout.available - layout.reset;
    const float valueX = resetX - spacing - layout.value;
    drawValue(valueX, rowStart.y);
    drawReset(resetX, rowStart.y);
    const float secondY = rowStart.y + lineH + style.ItemSpacing.y;
    // ★ 滑块画在**第二行**（`secondY`）—— 见上面 drawControl 的注释：
    //   这里原来漏传 y，滑块被画回第一行，是"重叠 + 空白"的唯一根因。
    drawControl(rowStart.x, layout.slider, secondY);
    ImGui::SetCursorScreenPos(
        ImVec2(rowStart.x, secondY + lineH + style.ItemSpacing.y));
  } else {
    // 宽屏：名称 | 滑块 | 数值 | 重置（一行四格，滑块吃掉全部余量）
    float x = rowStart.x + layout.name + spacing;
    drawControl(x, layout.slider, rowStart.y);
    x += layout.slider + spacing;
    drawValue(x, rowStart.y);
    x += layout.value + spacing;
    drawReset(x, rowStart.y);
    ImGui::SetCursorScreenPos(
        ImVec2(rowStart.x, rowStart.y + lineH + style.ItemSpacing.y));
  }

  if (followTheme) ImGui::EndDisabled();

  ImGui::PopID();
  return changed;
}

// -----------------------------------------------------------------------------
// 分组面板（每个分组一个折叠块 + 分组重置）
// -----------------------------------------------------------------------------
bool DrawTargetParams(const std::string& targetId, Target& target, Workbench& workbench) {
  Registry& registry = *workbench.registry;
  bool changed = false;
  const ParamRowMetrics metrics = MeasureRow(target);

  for (const ParamGroup& group : target.groups) {
    if (group.count == 0) continue;
    if (std::strcmp(group.name.c_str(), "能力缺口（只读）") == 0) continue;  // 单独一节显示

    ImGui::PushID(group.name.c_str());
    // 分组标题右边带"本组已改 N 项"，这样折叠着也能看出哪一组被动过
    std::size_t modified = 0;
    for (std::uint32_t i = group.first; i < group.first + group.count; ++i) {
      const ParamSlot& slot = target.params[i];
      if (!slot.desc.supported || slot.ref == nullptr) continue;
      if (!slot.Read().Equals(slot.def)) ++modified;
    }
    char header[192];
    if (modified > 0) {
      std::snprintf(header, sizeof(header), "%s  （已改 %zu 项）", group.name.c_str(), modified);
    } else {
      std::snprintf(header, sizeof(header), "%s  （%u 项）", group.name.c_str(), group.count);
    }
    // ★★ 分组标题也必须**按可用宽度截断**：折叠头的文字比窗口宽时，ImGui 只会在
    //   裁剪矩形处硬切（"背景采样  （已改 3" 这种断口），看起来就是文字溢出。
    //   ★ 这里必须用 `GetContentRegionMax()`（= 内容区总宽度，与光标无关），
    //     而不是 `GetContentRegionAvail()` —— 后者是"光标之后还剩多少宽度"，
    //     而此刻光标正停在「重置本组 / （N 项，已改 M 项）」那一行的右边，
    //     直接用它会把可用宽度算成几百像素的零头，标题与参数名都会被过度截断
    //     （真机截图露馅：参数名显示成"鲜艳度增"、"启用面板"）。见下面 available 的注释。
    const std::string fitted = FitTextToWidth(header, ImGui::GetContentRegionMax().x);
    const bool open = ImGui::CollapsingHeader(fitted.c_str(), ImGuiTreeNodeFlags_DefaultOpen);

    if (open) {
      // 分组重置放在组内第一行（不压在折叠头上：压在头上会和"展开/收起"抢点击）
      if (modified == 0) ImGui::BeginDisabled();
      if (ImGui::SmallButton("重置本组")) {
        registry.ResetGroup(targetId, group.name, ChangeOrigin::Reset);
        changed = true;
      }
      if (modified == 0) ImGui::EndDisabled();
      SameLineOrWrap("（项，已改项）");
      ImGui::TextDisabled("（%u 项，已改 %zu 项）", group.count, modified);

      // ★★★ 响应式参数行：**每帧**按当前可用宽度重算布局。
      //   旧实现是 ImGui 四列表格 + WidthFixed —— 列宽只在表格第一帧生效，
      //   窗口拉宽滑块不动、窄窗口下最后一列被裁，详见文件顶部与 ParamRowLayout.h。
      //
      //   ★★ 可用宽度**必须**取 `GetContentRegionMax().x`（内容区总宽度），
      //      不能用 `GetContentRegionAvail().x`：后者 = 内容区宽度 − **当前光标 x**，
      //      而这里光标刚停在上一行的「重置本组 …（N 项，已改 M 项）」右边，
      //      于是"可用宽度"只剩两三百像素的零头 ⇒ 名称列被压到几十像素、
      //      参数名被裁成"鲜艳度增"/"启用面板"（真机截图抓到的就是这个）。
      //      这一条是纯逻辑层测不出来的（ParamRowLayoutTest 只验"给定宽度算得对"，
      //      不验"传进去的宽度是不是真的可用宽度"）—— 属于宿主接线的错。
      const float available = ImGui::GetContentRegionMax().x;
      const ParamRowLayout layout = ComputeParamRowLayout(available, metrics);
      if (UiTrace()) {
        static int traceTick = 0;
        if ((traceTick++ % 120) == 0) {
          __android_log_print(ANDROID_LOG_INFO, kLogTag,
                              "[布局] 可用=%.0f 模式=%s 名称=%.0f 滑块=%.0f 数值=%.0f 重置=%.0f "
                              "（名称最长文本=%.0f 数值最长文本=%.0f 间距=%.0f）",
                              available, layout.twoLine ? "双行" : "单行", layout.name,
                              layout.slider, layout.value, layout.reset, metrics.nameText,
                              metrics.valueText, metrics.spacing);
        }
      }
      for (std::uint32_t i = group.first; i < group.first + group.count; ++i) {
        if (DrawParamRow(targetId, target, i, layout, workbench)) changed = true;
      }
    }
    ImGui::PopID();
  }

  // 能力缺口：单独一节，只读 + 原因（不做假控件）
  bool hasGaps = false;
  for (const ParamSlot& slot : target.params) {
    if (!slot.desc.supported) hasGaps = true;
  }
  if (hasGaps && workbench.showGaps) {
    ImGui::Spacing();
    ImGui::SeparatorText("能力缺口（参考项目有、本实现没有）");
    for (const ParamSlot& slot : target.params) {
      if (slot.desc.supported) continue;
      ImGui::Bullet();
      ImGui::TextUnformatted(slot.desc.cn.c_str());
      ImGui::SameLine();
      ImGui::TextDisabled("（%s）", slot.desc.en.c_str());
      ImGui::PushTextWrapPos(ImGui::GetCursorPos().x + ImGui::GetContentRegionAvail().x);
      ImGui::TextDisabled("%s", slot.desc.unsupportedReason.c_str());
      ImGui::PopTextWrapPos();
    }
  }
  return changed;
}

// -----------------------------------------------------------------------------
// 分页栏：可横向滚动的标题条
//
//  ★ 为什么不用 ImGui 的 BeginTabBar：它不支持横向滚动，而"标题根据注册表动态生成"
//    意味着标题数量不可控（当前 20+ 个，未来加组件还会变多）。
//    这里用一个受控的滚动偏移 + 左右翻页按钮（触摸友好：按钮 44px 高），
//    并在切换分页时自动把选中的标题滚进可视区。
// -----------------------------------------------------------------------------
struct PagedBarResult {
  int selected = 0;
};

PagedBarResult DrawPagedBar(Workbench& workbench,
                            const std::vector<const Target*>& pages) {
  PagedBarResult result;
  result.selected = workbench.page;
  if (pages.empty()) return result;

  static float scrollX = 0.0f;
  static float scrollTargetX = -1.0f;

  const ImGuiStyle& style = ImGui::GetStyle();
  const float barHeight = ImGui::GetFrameHeight() + 12.0f;
  // ★ 翻页按钮的宽度按**文字真实宽度**算：本工程字体是 2 倍缩放的中文字形，
  //   "上一页"三个字 ≈ 96px，写死 44px 会把它切成一个字（真机截图确认过）。
  //   同时给一个下限，保证触摸目标不小于 44px。
  const float arrowWidth =
      ImGui::CalcTextSize("上一页").x + style.FramePadding.x * 2.0f + 8.0f;
  const float trackWidth = ImGui::GetContentRegionAvail().x - arrowWidth * 2.0f - style.ItemSpacing.x * 2.0f;
  const ImVec2 origin = ImGui::GetCursorScreenPos();

  auto entryWidth = [&](const Target* target) {
    char label[160];
    std::snprintf(label, sizeof(label), "%s", target->desc.name.c_str());
    return ImGui::CalcTextSize(label).x + style.FramePadding.x * 2.0f + 24.0f;
  };

  // 总宽度（用于夹紧滚动偏移）
  float total = 0.0f;
  for (const Target* target : pages) total += entryWidth(target) + style.ItemSpacing.x;
  const float maxScroll = total > trackWidth ? total - trackWidth : 0.0f;

  // 自动把选中项滚进可视区
  if (scrollTargetX >= 0.0f) {
    float offset = 0.0f;
    for (int i = 0; i < static_cast<int>(pages.size()); ++i) {
      const float width = entryWidth(pages[static_cast<std::size_t>(i)]) + style.ItemSpacing.x;
      if (i == result.selected) {
        const float left = offset;
        const float right = offset + width;
        if (left < scrollX) scrollX = left;
        else if (right > scrollX + trackWidth) scrollX = right - trackWidth;
        break;
      }
      offset += width;
    }
    scrollTargetX = -1.0f;
  }
  scrollX = scrollX < 0.0f ? 0.0f : (scrollX > maxScroll ? maxScroll : scrollX);

  // ---- 左翻页 ----
  ImGui::SetCursorScreenPos(origin);
  if (ImGui::Button("上一页", ImVec2(arrowWidth, barHeight))) {
    result.selected = result.selected > 0 ? result.selected - 1 : 0;
    scrollTargetX = 0.0f;
  }

  // ---- 标题轨道（自己裁剪，不用子窗口：子窗口会吃掉触摸事件）----
  ImGui::PushClipRect(ImVec2(origin.x + arrowWidth + style.ItemSpacing.x, origin.y),
                      ImVec2(origin.x + arrowWidth + style.ItemSpacing.x + trackWidth,
                             origin.y + barHeight),
                      true);
  float x = origin.x + arrowWidth + style.ItemSpacing.x - scrollX;
  for (int i = 0; i < static_cast<int>(pages.size()); ++i) {
    const Target* target = pages[static_cast<std::size_t>(i)];
    const float width = entryWidth(target);
    if (x + width >= origin.x + arrowWidth + style.ItemSpacing.x &&
        x <= origin.x + arrowWidth + style.ItemSpacing.x + trackWidth) {
      ImGui::SetCursorScreenPos(ImVec2(x, origin.y + 6.0f));
      ImGui::PushID(i);
      const bool active = (i == result.selected);
      if (active) {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.20f, 0.52f, 1.0f, 0.85f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.24f, 0.58f, 1.0f, 0.95f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.16f, 0.46f, 0.92f, 1.0f));
      } else {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(1.0f, 1.0f, 1.0f, 0.08f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(1.0f, 1.0f, 1.0f, 0.16f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(1.0f, 1.0f, 1.0f, 0.22f));
      }
      if (ImGui::Button(target->desc.name.c_str(), ImVec2(width, ImGui::GetFrameHeight()))) {
        result.selected = i;
        scrollTargetX = 0.0f;
      }
      ImGui::PopStyleColor(3);
      ImGui::PopID();
    }
    x += width + style.ItemSpacing.x;
  }
  ImGui::PopClipRect();

  // 鼠标滚轮在标题条上横向滚动（触摸设备用左右按钮，PC 调试用滚轮）
  if (ImGui::IsWindowHovered() && ImGui::GetIO().MouseWheel != 0.0f) {
    scrollX -= ImGui::GetIO().MouseWheel * 60.0f;
  }

  // ---- 右翻页 ----
  ImGui::SetCursorScreenPos(ImVec2(origin.x + arrowWidth + style.ItemSpacing.x + trackWidth +
                                       style.ItemSpacing.x,
                                   origin.y));
  if (ImGui::Button("下一页", ImVec2(arrowWidth, barHeight))) {
    result.selected = result.selected + 1 < static_cast<int>(pages.size()) ? result.selected + 1
                                                                          : result.selected;
    scrollTargetX = 0.0f;
  }
  // 把游标放回标题条下方，后续控件从这里继续排
  ImGui::SetCursorScreenPos(ImVec2(origin.x, origin.y + barHeight + style.ItemSpacing.y));

  // 触摸友好提示：当前是第几个 / 共几个
  ImGui::TextDisabled("第 %d / %zu 个调试对象", result.selected + 1, pages.size());
  return result;
}

// -----------------------------------------------------------------------------
// 诊断面板（默认关闭 ⇒ 零成本；打开后才读值）
// -----------------------------------------------------------------------------
void DrawDiagnosticsPanel(Registry& registry, PresetStore& presets, History& history,
                          Workbench& workbench) {
  const Target* target = nullptr;
  if (workbench.page >= 0 &&
      workbench.page < static_cast<int>(registry.DisplayOrder().size())) {
    const std::size_t index = registry.DisplayOrder()[static_cast<std::size_t>(workbench.page)];
    target = &registry.TargetAt(index);
  }
  const WorkbenchDiagnostics& d = workbench.diagnostics;

  ImGui::SeparatorText("实时诊断");
  ImGui::Text("当前目标    %s", target != nullptr ? target->desc.id.c_str() : "（无）");
  ImGui::Text("参数版本    编辑 %llu / 生效 %llu %s",
              static_cast<unsigned long long>(registry.EditVersion()),
              static_cast<unsigned long long>(registry.AppliedVersion()),
              registry.FullyApplied() ? "（已全部应用）" : "（有更新待应用）");
  ImGui::Text("待应用项    %zu（合并掉 %llu 次过期更新）", registry.PendingCount(),
              static_cast<unsigned long long>(registry.MergedUpdates()));
  ImGui::Text("注册表      %zu 个目标 / %zu 个参数 / %zu 条缺口 / %zu 项偏离默认",
              registry.TargetCount(), registry.ParamTotal(), registry.UnsupportedCount(),
              registry.ChangedFromDefault());
  ImGui::Text("帧          第 %llu 帧  %.1f FPS  最差 %.2f ms",
              static_cast<unsigned long long>(d.frameCount), d.fps, d.worstFrameMs);
  ImGui::Text("背景采样    %s  纹理=%u  映射=%dx%d%s", d.backdropOrigin, d.backdropTexture,
              d.backdropMapWidth, d.backdropMapHeight,
              d.backdropTransparentHost ? "  （宿主透明：会退化为兜底色）" : "");
  ImGui::Text("裁剪边界    (%.0f,%.0f) - (%.0f,%.0f)  内容圆角遮罩 %.1f",
              static_cast<double>(d.clipLeft), static_cast<double>(d.clipTop),
              static_cast<double>(d.clipRight), static_cast<double>(d.clipBottom),
              static_cast<double>(d.contentMaskRadius));
  if (d.effectChain != nullptr && d.effectChain[0] != 0) {
    ImGui::TextWrapped("效果链      %s", d.effectChain);
  }
  if (d.shaderError != nullptr && d.shaderError[0] != 0) {
    ImGui::TextColored(ImVec4(1.0f, 0.42f, 0.35f, 1.0f), "着色器错误  %s", d.shaderError);
  } else {
    ImGui::TextDisabled("着色器错误  无");
  }
  ImGui::Text("撤销/重做   可撤销 %zu 步 / 可重做 %zu 步（合并掉 %zu 次连续写入）",
              history.UndoDepth(), history.RedoDepth(), history.CoalescedCount());
  ImGui::Text("持久化      %s  %s", presets.Path().c_str(),
              presets.dirty() ? "（有未保存修改，稍后自动保存）" : "（已是最新）");
}

}  // namespace

// =============================================================================
// 对外接口
// =============================================================================
void SetWorkbenchDiagnostics(const WorkbenchDiagnostics& diagnostics) {
  WB().diagnostics = diagnostics;
}

void BeginGlassWorkbench() {
  Workbench& workbench = WB();
  if (workbench.beginDone) return;
  workbench.beginDone = true;
  workbench.registry = &Global();
  static PresetStore store(Global());
  static History history(Global());
  workbench.presets = &store;
  workbench.history = &history;
  Global().Subscribe(&HistorySink, &workbench);

  // 参数文件的默认位置：/data/local/tmp（ELF 本来就以 root 跑，随时可以 pull 出来看）。
  // AURORA_GLASS_PARAMS 可以改路径（例如指到 /sdcard/Download 便于用户自己导出）。
  const char* override = std::getenv("AURORA_GLASS_PARAMS");
  const std::string path = override != nullptr && override[0] != 0
      ? std::string(override)
      : std::string("/data/local/tmp/aurora_glass_params.json");
  store.SetPath(path);
  std::snprintf(workbench.ioPath, sizeof(workbench.ioPath), "%s", path.c_str());
  workbench.ready = true;
}

void FlushGlassWorkbench() {
  Workbench& workbench = WB();
  if (!workbench.ready || workbench.presets == nullptr) return;
  if (!workbench.presets->dirty()) return;
  std::string error;
  if (workbench.presets->Save(&error)) {
    __android_log_print(ANDROID_LOG_INFO, kLogTag, "参数已保存：%s",
                        workbench.presets->Path().c_str());
  } else {
    __android_log_print(ANDROID_LOG_WARN, kLogTag, "参数保存失败：%s", error.c_str());
  }
}

// =============================================================================
//  持久化泵：**只做**两件事 —— 首次从磁盘恢复、之后按去抖自动保存。
//
//  ★ 为什么必须能从 DrawGlassWorkbench 里分出来：
//    恢复与自动保存原来都长在「液态玻璃」页的绘制函数里，于是
//      · 用户停在别的标签页（例如「窗口缩放」）改参数 ⇒ 不会落盘；
//      · 二级窗口里的组件改了参数、但用户没打开那一页 ⇒ 也不会落盘。
//    现在它在**每一帧**都被调用（LiquidGlassRuntime 的帧首），
//    页面画不画都不影响持久化。恢复只做一次（静态标志）。
// =============================================================================
void PumpGlassWorkbenchPersistence() {
  Workbench& workbench = WB();
  if (!workbench.ready || workbench.registry == nullptr || workbench.presets == nullptr) return;
  Registry& registry = *workbench.registry;
  PresetStore& presets = *workbench.presets;

  // ★★ 帧首兜底提交（2026-10-19 修「主窗口调二级玻璃窗口没反应」）。
  //
  //   根因：`Registry::Commit()` 原来**只**在 `secondary_imgui::Draw()` 里被调用
  //   （`if (begun)` 之内）。而那个函数开头有 `if (!s_Visible) return;` ——
  //   用户一旦取消勾选「显示二级液态玻璃窗口」（或窗口被收起），它整段都不执行
  //   ⇒ 提交队列永远不清空 ⇒ 所有**延迟生效**的参数（`blurDp → blurAppliedDp`、
  //     各个 PanelTune 的 deferred 字段…）**再也不落到渲染字段**，
  //     而"直接生效"的参数却照旧工作 ⇒ 表现就是"有的参数能调、有的调了没反应"。
  //
  //   为什么放在帧首也安全：`Commit()` 对空队列是 `if (pending_.empty()) return 0;`
  //   （零成本）；二级窗口可见时它稍后还会再跑一次，那时拿到的是**同一帧**UI 刚写入的
  //   值（保持"零帧延迟"）。这里这一遍只是保证"与窗口可见性无关"。
  registry.Commit();

  // ---- 首次：把注册表建起来（幂等），并尝试从磁盘恢复 ----
  RegisterSecondaryTargets();
  static bool restoreAttempted = false;
  if (!restoreAttempted) {
    restoreAttempted = true;
    std::string error;
    if (presets.Load(&error)) {
      std::vector<std::string> skipped;
      std::vector<std::string> adjusted;
      presets.ApplyPreset(presets.ActiveName(), &skipped, &adjusted);
      registry.Commit();
      char buffer[256];
      std::snprintf(buffer, sizeof(buffer), "已从 %s 恢复预设「%s」（跳过 %zu 项，夹紧 %zu 项）",
                    presets.Path().c_str(), presets.ActiveName().c_str(), skipped.size(),
                    adjusted.size());
      workbench.SetStatus(buffer);
      __android_log_print(ANDROID_LOG_INFO, kLogTag, "%s", buffer);
    } else if (!error.empty()) {
      workbench.SetStatus("参数文件读取失败：" + error);
      __android_log_print(ANDROID_LOG_WARN, kLogTag, "参数文件读取失败：%s", error.c_str());
    } else {
      // ★ 首次运行（文件还不存在）：**主动写一份默认参数文件**。
      //   为什么值得写：它是一份**可读可改可分享**的模板 —— 用户可以自己编辑
      //   /data/local/tmp/aurora_glass_params.json 再重启，也能直接发给别人。
      //   文件本来就该是"当前状态的快照"，而现在的状态就是全部默认值。
      //   （原子写：先写 .tmp 再 rename，写坏了也不会留下半份文件。）
      presets.MarkDirty();
      std::string saveError;
      if (presets.Save(&saveError)) {
        workbench.SetStatus("首次运行：已生成默认参数文件 " + presets.Path());
        __android_log_print(ANDROID_LOG_INFO, kLogTag, "首次运行：已生成默认参数文件 %s",
                            presets.Path().c_str());
      }
    }
  }

  // ---- 自动保存（去抖）：改完 2 秒没有新动作才落盘，避免高频拖动时反复写文件 ----
  //
  // ★★★ 必须走**后台写**：`Save()` 里除了 JSON 序列化还有 fopen/fsync/rename，
  //   在真机上 fsync 可达几毫秒到几十毫秒。它原来就长在这条每帧绘制路径里
  //   （停手 2 秒就写一次）⇒ 表现为"改参数时每隔两秒卡一下"。
  //   现在序列化仍在这里（只读 registry、无 I/O），写盘交给后台线程。
  if (presets.dirty() && workbench.lastChangeStamp > 0.0 &&
      NowSeconds() - workbench.lastChangeStamp > kAutoSaveDelaySeconds) {
    std::string error;
    if (presets.SaveAsync(&error)) {
      workbench.lastChangeStamp = 0.0;
      const std::string asyncError = PresetStore::LastAsyncError();
      if (!asyncError.empty() && NowSeconds() - workbench.statusStamp > 5.0) {
        workbench.SetStatus("后台保存失败：" + asyncError);
      }
    } else if (presets.Save(&error)) {
      // 后台线程不可用（极罕见）：回退成同步写，至少不丢数据。
      workbench.lastChangeStamp = 0.0;
    } else if (workbench.status.empty() || NowSeconds() - workbench.statusStamp > 5.0) {
      workbench.SetStatus("自动保存失败：" + error);
    }
  }
}

WorkbenchStats CurrentWorkbenchStats() {
  Workbench& workbench = WB();
  WorkbenchStats stats;
  if (workbench.registry == nullptr) return stats;
  Registry& registry = *workbench.registry;
  stats.targets = static_cast<int>(registry.TargetCount());
  stats.params = static_cast<int>(registry.ParamTotal());
  stats.unsupported = static_cast<int>(registry.UnsupportedCount());
  stats.changed = static_cast<int>(registry.ChangedFromDefault());
  if (workbench.presets != nullptr) {
    stats.presets = static_cast<int>(workbench.presets->Count());
    stats.dirty = workbench.presets->dirty();
    stats.loadedFromDisk = workbench.presets->loadedFromDisk();
    stats.activePreset = workbench.presets->ActiveName().c_str();
    stats.path = workbench.presets->Path().c_str();
  }
  return stats;
}

void DrawGlassWorkbench() {
#if defined(__ANDROID__)
    {
        static int wbProbe = 0;
        if (wbProbe < 3) {
            ++wbProbe;
            static FILE* wbFile = std::fopen("/data/local/tmp/aurora_probe.log", "a");
            if (wbFile != nullptr) {
                std::fprintf(wbFile, "WORKBENCH entered (call %d)\n", wbProbe);
                std::fflush(wbFile);
            }
            __android_log_print(ANDROID_LOG_INFO, "AuroraParam",
                                "WORKBENCH entered (call %d)", wbProbe);
        }
    }
#endif
  BeginGlassWorkbench();
  Workbench& workbench = WB();
  if (!workbench.ready || workbench.registry == nullptr) {
    ImGui::TextDisabled("工作台未初始化");
    return;
  }
  Registry& registry = *workbench.registry;
  PresetStore& presets = *workbench.presets;
  History& history = *workbench.history;

  // ---- 持久化泵（首次从磁盘恢复 + 去抖自动保存）----
  //  ★ 已经抽成独立函数：主窗口的「窗口缩放」页也要用它 —— 否则用户改完
  //    缩放方式后如果一直停在那一页（没进「液态玻璃」页），参数就不会落盘。
  PumpGlassWorkbenchPersistence();


  // ---- 顶部：全局操作 ----
  {
    // 悬停提示要在点之前就告诉用户"这一步会撤掉什么"
    const bool canUndo = history.CanUndo();
    const std::string undoLabel = history.LastLabel();
    if (!canUndo) ImGui::BeginDisabled();
    if (ImGui::Button("撤销")) history.Undo();
    if (!canUndo) ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) {
      if (canUndo) ImGui::SetTooltip("撤销：%s", undoLabel.c_str());
      else ImGui::SetTooltip("没有可撤销的操作（拖一次滑块只算一步）");
    }
  }
  SameLineOrWrap("重做");
  {
    const bool canRedo = history.CanRedo();
    const std::string redoLabel = history.NextLabel();
    if (!canRedo) ImGui::BeginDisabled();
    if (ImGui::Button("重做")) history.Redo();
    if (!canRedo) ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) {
      if (canRedo) ImGui::SetTooltip("重做：%s", redoLabel.c_str());
      else ImGui::SetTooltip("没有可重做的操作");
    }
  }
  SameLineOrWrap("保存全部参数");
  if (ImGui::Button("保存全部参数")) {
    // ★ 不去覆盖「默认」预设：那个名字的语义是"注册时的默认值"（隐式的），
    //   把它写成当前值会让"恢复默认"再也回不去。
    //   导出时文件里那份「当前工作区」预设已经携带了屏幕上的值 ⇒ 保存不会丢东西。
    std::string error;
    presets.MarkDirty();
    if (presets.Save(&error)) {
      workbench.SetStatus("已保存全部参数：" + presets.Path());
    } else {
      workbench.SetStatus("保存失败：" + error);
    }
  }
  SameLineOrWrap("重新载入");
  if (ImGui::Button("重新载入")) {
    std::string error;
    if (presets.Load(&error)) {
      std::vector<std::string> skipped;
      presets.ApplyPreset(presets.ActiveName(), &skipped, nullptr);
      registry.Commit();
      workbench.SetStatus("已重新载入：" + presets.Path());
    } else {
      workbench.SetStatus(error.empty() ? "文件不存在，无需载入" : "载入失败：" + error);
    }
  }
  SameLineOrWrap("诊断");
  ImGui::Checkbox("诊断", &workbench.showDiagnostics);
  SameLineOrWrap("显示缺口");
  ImGui::Checkbox("显示缺口", &workbench.showGaps);
  if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) {
    ImGui::SetTooltip("显示每个目标的「参考项目有、本实现没有」的能力（只读 + 原因）");
  }

  // ---- 全局重置 / 预设 / 导入导出 / A-B ----
  if (ImGui::Button("全部恢复默认")) {
    history.BeginBatch("全部恢复默认");
    registry.ResetAll(ChangeOrigin::Reset);
    history.EndBatch();
    registry.Commit();
    workbench.SetStatus("已把全部玻璃参数恢复为组件注册时的默认值");
  }
  SameLineOrWrap("导出参数文本");
  if (ImGui::Button("导出参数文本")) {
    std::snprintf(workbench.ioPath, sizeof(workbench.ioPath), "%s", presets.Path().c_str());
    std::string error;
    const std::string text = presets.ExportText();
    if (SaveTextAtomic(workbench.ioPath, text, &error)) {
      char buffer[256];
      std::snprintf(buffer, sizeof(buffer), "已导出 %zu 字节到 %s", text.size(),
                    workbench.ioPath);
      workbench.SetStatus(buffer);
    } else {
      workbench.SetStatus("导出失败：" + error);
    }
  }
  SameLineOrWrap("导入参数文本");
  if (ImGui::Button("导入参数文本")) {
    std::string text;
    std::string error;
    if (!ReadTextFile(workbench.ioPath, &text, &error)) {
      workbench.SetStatus("导入失败：" + error);
    } else {
      ImportReport report;
      if (presets.ImportText(text, &report, true)) {
        std::vector<std::string> skipped;
        std::vector<std::string> adjusted;
        presets.ApplyPreset(presets.ActiveName(), &skipped, &adjusted);
        registry.Commit();
        char buffer[288];
        std::snprintf(buffer, sizeof(buffer),
                      "已导入：现有预设 %zu 个，跳过 %zu 项，夹紧 %zu 项，源版本 v%d%s",
                      presets.Count(), skipped.size(), adjusted.size(),
                      report.sourceVersion, report.migratedFrom != 0 ? "（经过旧版迁移）" : "");
        workbench.SetStatus(buffer);
      } else {
        workbench.SetStatus("导入失败：" + report.error);
      }
    }
  }

  // 预设选择
  {
    ImGui::SetNextItemWidth(200.0f);
    std::string current = presets.ActiveName();
    if (ImGui::BeginCombo("预设", current.c_str())) {
      for (std::size_t i = 0; i < presets.Count(); ++i) {
        const bool selected = presets.At(i).name == current;
        ImGui::PushID(static_cast<int>(i));
        if (ImGui::Selectable(presets.At(i).name.c_str(), selected)) {
          history.BeginBatch("应用预设：" + presets.At(i).name);
          std::vector<std::string> skipped;
          std::vector<std::string> adjusted;
          presets.ApplyPreset(presets.At(i).name, &skipped, &adjusted);
          history.EndBatch();
          registry.Commit();
          char buffer[224];
          std::snprintf(buffer, sizeof(buffer), "已应用预设「%s」（跳过 %zu 项，夹紧 %zu 项）",
                        presets.At(i).name.c_str(), skipped.size(), adjusted.size());
          workbench.SetStatus(buffer);
        }
        ImGui::PopID();
      }
      ImGui::EndCombo();
    }
    SameLineOrWrap("新预设名");
    ImGui::SetNextItemWidth(160.0f);
    ImGui::InputTextWithHint("##presetname", "新预设名", workbench.presetName,
                             sizeof(workbench.presetName));
    SameLineOrWrap("另存为预设");
    if (ImGui::Button("另存为预设")) {
      if (workbench.presetName[0] == 0) {
        workbench.SetStatus("请先填一个预设名");
      } else if (presets.CaptureAs(workbench.presetName, true)) {
        std::string error;
        presets.Save(&error);
        workbench.SetStatus(std::string("已保存预设「") + workbench.presetName + "」");
      }
    }
  }

  // A/B 对比
  {
    if (ImGui::Button("存为 A")) {
      workbench.slotA = registry.Capture();
      workbench.hasA = true;
      workbench.SetStatus("已记录当前参数为 A");
    }
    SameLineOrWrap("存为 B");
    if (ImGui::Button("存为 B")) {
      workbench.slotB = registry.Capture();
      workbench.hasB = true;
      workbench.SetStatus("已记录当前参数为 B");
    }
    SameLineOrWrap("切到 A");
    if (!workbench.hasA) ImGui::BeginDisabled();
    if (ImGui::Button("切到 A")) {
      history.BeginBatch("切到 A");
      registry.Apply(workbench.slotA, ChangeOrigin::Preset, nullptr);
      history.EndBatch();
      registry.Commit();
      workbench.SetStatus("已切到 A");
    }
    if (!workbench.hasA) ImGui::EndDisabled();
    SameLineOrWrap("切到 B");
    if (!workbench.hasB) ImGui::BeginDisabled();
    if (ImGui::Button("切到 B")) {
      history.BeginBatch("切到 B");
      registry.Apply(workbench.slotB, ChangeOrigin::Preset, nullptr);
      history.EndBatch();
      registry.Commit();
      workbench.SetStatus("已切到 B");
    }
    if (!workbench.hasB) ImGui::EndDisabled();
    if (workbench.hasA && workbench.hasB) {
      std::size_t diff = 0;
      for (const SnapshotEntry& entry : workbench.slotA.entries) {
        const SnapshotEntry* other = workbench.slotB.Find(entry.target, entry.param);
        if (other == nullptr || !other->value.Equals(entry.value)) ++diff;
      }
      SameLineOrWrap("A/B 差异");
      ImGui::TextDisabled("A/B 差异 %zu 项", diff);
    }
  }

  if (!workbench.status.empty()) {
    ImGui::TextDisabled("%s", workbench.status.c_str());
  }

  ImGui::Separator();

  // ---- 检索：目标/参数一多就找不到，所以留一个关键词入口 ----
  ImGui::SetNextItemWidth(220.0f);
  if (ImGui::InputTextWithHint("##search", "搜索组件或参数（例如 折射 / toggle / blur）",
                               workbench.search, sizeof(workbench.search))) {
    workbench.page = 0;
  }
  SameLineOrWrap("只看结果");
  ImGui::Checkbox("只看结果", &workbench.searchActive);
  if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) {
    ImGui::SetTooltip("把分页栏限制成搜索结果（默认关：分页栏始终显示全部对象）");
  }

  // ---- 分页栏 ----
  // 分页对象 = 顶层目标（parent == secondary_window）；子层在页面内用一排按钮选择。
  // 这样分页数量可控，又不会漏掉任何子层（它们都是注册表里的独立目标）。
  std::vector<const Target*> pages;
  if (workbench.searchActive && workbench.search[0] != 0) {
    for (const Target* target : registry.Search(workbench.search)) pages.push_back(target);
  } else {
    for (std::size_t index : registry.DisplayOrder()) {
      const Target& target = registry.TargetAt(index);
      if (target.desc.parent == "secondary_window") pages.push_back(&target);
    }
  }
  if (pages.empty()) {
    ImGui::TextDisabled("没有匹配的调试对象");
    return;
  }
  if (workbench.page >= static_cast<int>(pages.size())) workbench.page = 0;

  const PagedBarResult bar = DrawPagedBar(workbench, pages);
  workbench.page = bar.selected;
  const Target* page = pages[static_cast<std::size_t>(workbench.page)];
  Target* target = registry.MutableTarget(page->desc.id);

  ImGui::Separator();

  // ---- 当前调试对象提示 ----
  {
    ImGui::Text("当前调试对象");
    SameLineOrWrap(target->desc.name.c_str());
    ImGui::TextColored(ImVec4(0.42f, 0.78f, 1.0f, 1.0f), "%s", target->desc.name.c_str());
    SameLineOrWrap("· id · type · 参数");
    ImGui::TextDisabled("· %s · %s · %zu 个参数%s", target->desc.id.c_str(),
                        target->desc.type.c_str(), target->params.size(),
                        target->desc.live ? "" : "（不支持实时修改）");
    if (!target->desc.note.empty()) {
      ImGui::PushTextWrapPos(ImGui::GetCursorPos().x + ImGui::GetContentRegionAvail().x);
      ImGui::TextDisabled("%s", target->desc.note.c_str());
      ImGui::PopTextWrapPos();
    }
  }

  // ---- 子层选择（一个组件有多个玻璃层时）----
  const std::vector<const Target*> children = registry.ChildrenOf(target->desc.id);
  Target* active = target;
  if (!children.empty()) {
    ImGui::TextDisabled("这个组件有 %zu 个玻璃子层，选择一个来调：", children.size());
    SameLineOrWrap("整体");
    if (ImGui::Button("整体")) workbench.sublayer = 0;
    for (std::size_t i = 0; i < children.size(); ++i) {
      SameLineOrWrap(children[i]->desc.name.c_str());
      ImGui::PushID(static_cast<int>(i) + 9000);
      const bool selected = workbench.sublayer == static_cast<int>(i) + 1;
      if (selected) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.20f, 0.52f, 1.0f, 0.85f));
      if (ImGui::Button(children[i]->desc.name.c_str())) {
        workbench.sublayer = static_cast<int>(i) + 1;
      }
      if (selected) ImGui::PopStyleColor();
      ImGui::PopID();
    }
    if (workbench.sublayer > static_cast<int>(children.size())) workbench.sublayer = 0;
    if (workbench.sublayer > 0) {
      active = registry.MutableTarget(children[static_cast<std::size_t>(workbench.sublayer - 1)]
                                          ->desc.id);
      ImGui::TextColored(ImVec4(0.42f, 0.78f, 1.0f, 1.0f), "正在调试子层：%s",
                         active->desc.name.c_str());
    }
  }

  // ---- 本目标的快捷操作 ----
  if (ImGui::Button("重置本组件")) {
    history.BeginBatch("重置 " + active->desc.name);
    registry.ResetTarget(active->desc.id, ChangeOrigin::Reset);
    history.EndBatch();
    registry.Commit();
    workbench.SetStatus("已重置 " + active->desc.name);
  }
  SameLineOrWrap("重置本页全部子层");
  if (ImGui::Button("重置本页全部子层")) {
    history.BeginBatch("重置本页");
    registry.ResetTarget(target->desc.id, ChangeOrigin::Reset);
    for (const Target* child : children) registry.ResetTarget(child->desc.id, ChangeOrigin::Reset);
    history.EndBatch();
    registry.Commit();
    workbench.SetStatus("已重置本页全部子层");
  }
  SameLineOrWrap("（改动会立即作用到二级窗口，无需重启）");
  ImGui::TextDisabled("（改动会立即作用到二级窗口，无需重启）");

  if (workbench.showDiagnostics) {
    DrawDiagnosticsPanel(registry, presets, history, workbench);
  }

  ImGui::Separator();

  // ---- 参数列表 ----
  // 直接画在标签页里，靠主题窗口自己的滚动 —— 再嵌一层子窗口会出现"两个滚动条
  // 叠在一起、手指不知道该滚哪个"的问题（触摸设备上尤其明显）。
  DrawTargetParams(active->desc.id, *active, workbench);
}

}  // namespace glass
}  // namespace aurora
