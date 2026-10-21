#pragma once

// -----------------------------------------------------------------------------
// 调试选中框（DebugHighlight）
//
// 用途：在调试面板里选中某个目标（整窗口 / 窗口缩放）时，在屏幕上那个真实的
//       组件四周画**四个角**的取景框，让用户一眼看出"我现在调的到底是哪个东西"。
//
// 为什么需要单独一个模块：
//   调试面板（主窗口，GlassWorkbench）和组件本身（二级窗口，secondary_imgui）是
//   两棵独立的绘制树，中间还隔着 liquidglass 的第三方代码。要让"面板里的选中状态"
//   影响到"组件位置的高亮"，必须有一个跨模块的共享点。
//
// 数据流（每帧一次，顺序很重要）：
//   1) GlassWorkbench 里「调整窗口」开关为真 → SetAdjustMode(true)，并 SetDebugTarget(id)
//   2) secondary_imgui 画每个组件时 → TryMarkRect(id, rect)
//         · 开关关着时直接返回 false，连矩形都不认领
//         · id 等于当前调试目标才记录矩形
//   3) 二级窗口画完之后 → DrawDebugHighlight()
//         · 用 ImGui **背景**绘制列表把四个角画出来
//   4) 下一帧开始时 → BeginDebugFrame() 清掉上一帧的记录
//
// 设计取舍（都是用户明确提出过的）：
//   · **只在「调整窗口」开关打开时才显示** —— 关掉时屏幕上必须干干净净。
//   · **只画四个角**，不画整圈描边（整圈会和玻璃自身的边光混淆）。
//   · **画在背景层**：调试面板会盖住它。用户原话是"调试页面会把这四个角角给盖住
//     在他们之上，而不是像现在的这四个角角永远在调试界面之上"。
//   · 出现/消失/换目标都有淡入淡出与位置插值，避免生硬跳变。
//   · 只保留**一个**矩形。调试一次只选一个目标，多矩形会让界面更乱。
//   · 命中不到就什么都不画（而不是画到 (0,0)）—— 画错位置比不画更误导。
// -----------------------------------------------------------------------------

namespace aurora::glass::debughl {

struct Rect final {
  float x0 = 0.0f;
  float y0 = 0.0f;
  float x1 = 0.0f;
  float y1 = 0.0f;

  bool Valid() const { return x1 > x0 && y1 > y0; }
};

// 每帧开头调用：清掉上一帧的矩形记录，但**保留**当前选中的目标 id 与开关状态。
void BeginDebugFrame();

// 「调整窗口」开关。关着时 TryMarkRect 一律返回 false、DrawDebugHighlight 淡出。
void SetAdjustMode(bool enabled);
bool AdjustMode();

// 设置当前正在调试的目标 id（由调试面板驱动）。传 nullptr / 空串 = 不高亮。
void SetDebugTarget(const char* targetId);

// 当前选中的目标 id（不会被 BeginDebugFrame 清掉）。
const char* DebugTarget();

// 组件绘制时调用：开关打开、且 id 与当前调试目标一致，就记录这块矩形。
// 返回 true 表示"这个组件就是正在调试的那个"，调用方可以据此做额外处理。
bool TryMarkRect(const char* targetId, const Rect& rect);

// 取本帧记录到的矩形（没命中时 Valid() 为 false）。
Rect MarkedRect();

// 在二级窗口绘制完成后调用：把记录到的矩形画成四个角。
// 内部用 ImGui::GetBackgroundDrawList()，所以会被窗口盖住（这是刻意的层级）。
void DrawDebugHighlight();

// 把目标 id 翻译成人话（"整窗口"、"窗口缩放"…）。
// 找不到对应关系时返回 nullptr。
const char* HumanNameOf(const char* targetId);

}  // namespace aurora::glass::debughl
