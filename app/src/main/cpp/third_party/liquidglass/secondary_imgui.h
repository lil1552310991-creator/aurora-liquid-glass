#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <imgui.h>

#include "UiTouchGeometry.h"

namespace secondary_imgui {

struct HostInfo final {
  std::string renderer;
  std::string vulkanState;
  std::string vulkanDiagnostic;
  std::string host;
  bool imeAvailable = false;
  bool surfaceReady = false;
  bool backgroundInput = false;
  bool liquidGlass = false;
  bool textInputRequested = false;
  std::uint32_t vulkanDevices = 0;
  std::uint32_t surfaceFormats = 0;
  std::uint32_t presentModes = 0;
  std::uint64_t frameCount = 0;
  float fps = 0.0f;
  float worstFrameMilliseconds = 0.0f;
  float safeLeft = 0.0f;
  float safeTop = 0.0f;
  float safeRight = 0.0f;
  float safeBottom = 0.0f;
  std::size_t activePointers = 0;
  float gesturePanX = 0.0f;
  float gesturePanY = 0.0f;
  float gestureScale = 1.0f;
  float gestureRotation = 0.0f;
  // 系统输入法弹层矩形（物理像素；全 0 = 未知/已收起）。
  //   ★ 二级窗口用它做避让：我们的图层 z 序在 WMS 子树之上，
  //     不避让就会把键盘压在玻璃下面（并且触摸也被我们接走）。
  int imeLeft = 0;
  int imeTop = 0;
  int imeRight = 0;
  int imeBottom = 0;
};

void InstallGlassBackend();

// These callbacks draw only the glass jobs included in the frame's visual
// snapshot. Unknown callbacks may have side effects and must still execute.
bool IsPanelDrawCallback(ImDrawCallback callback);

void SetLanguage(bool chinese);
// Diagnostic only: match the user's clear-window screenshot for frame-age comparisons.
void UseLatencyComparisonWindowOptics();
bool WantsTextInput();
// ★ 每帧绘制前调用一次：把"想要文字输入"清零，由本帧真正画到的输入框重新置上。
//   （否则窗口折叠/隐藏的那几帧会沿用上一帧的 true ⇒ 键盘的拉起上升沿永远不来。）
void BeginTextInputFrame();

// ===========================================================================
//  输入框 ↔ 系统输入法 的**镜像同步**
// ===========================================================================
//  用户反馈原话："那个输入的地方的操作逻辑跟正常的对话框差太多了 … 而且莫名其妙
//  多打一个字上去"。根因是**两份互不相干的文本状态**：
//    · 系统输入法那边是 Java 的 EditText（权威的编辑对象）；
//    · 我们这边是 ImGui 的 InputText（另一份缓冲 + 自己的光标）。
//  旧实现只用"追加字符 + 退格键"去**猜**两边的差异 ⇒ 一旦有一侧先动过
//  （用户选了字、剪贴板粘贴、输入法上屏一整句），两边的基准就永久错开，
//  表现就是"多一个字 / 少一个字 / 光标乱跳"。
//
//  正确做法是**镜像**：系统输入法回传的整段已提交文本 + 选区直接**设置**到
//  输入框（ImGui 官方为此提供了 `ImGuiInputTextState::ReloadUserBuf`，
//  见 imgui_internal.h:1110-1117 的说明），而不是做增量对齐。
//  ★ 选区偏移用 **UTF-16 码元**（ImGui 的 ImWchar 是 16 位，与之同口径）。
void SetImeText(const char* utf8Text, int selectionStartUtf16, int selectionEndUtf16);
/** 输入框当前内容（宿主把它作为输入法会话的初始文本，保证两边起点一致）。 */
const char* GetImeText();

void SetHostInfo(const HostInfo& info);

void SetVisible(bool visible);
bool IsVisible();

void SetAnchorRect(float x, float y, float w, float h);

void GetTouchRect(float outRect[4]);

// Rendering-thread snapshot of the secondary window's actual decorations and
// clipped glass body. Zero means the window is not part of this frame.
std::uint32_t TouchWindowId();
void AppendTouchShapes(std::vector<aurora::VisibleUiShape>& shapes);

void Draw();

void DrawHostControls();

}
