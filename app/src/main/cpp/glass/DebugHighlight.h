#pragma once

// -----------------------------------------------------------------------------
// 调试选中框（DebugHighlight）
//
// 用途：在调试面板里选中某个目标（整窗口 / 卡片 / 按钮 / 芯片…）时，在屏幕上那个
//       真实的组件周围画一圈高亮框，让用户一眼看出"我现在调的到底是哪个东西"。
//
// 为什么需要单独一个模块：
//   调试面板（主窗口，GlassWorkbench）和组件本身（二级窗口，secondary_imgui）是
//   两棵独立的绘制树，中间还隔着 liquidglass 的第三方代码。要让"面板里的选中状态"
//   影响到"组件位置的高亮"，必须有一个跨模块的共享点。
//
// 数据流（每帧一次，顺序很重要）：
//   1) GlassWorkbench 选中某目标 → SetDebugTarget(id)
//   2) secondary_imgui 画每个组件时 → TryMarkRect(id, rect)
//         · 若 id 等于当前调试目标，就把这块矩形记为"要高亮的区域"
//   3) 二级窗口画完之后 → DrawDebugHighlight()
//         · 用 ImGui 前景绘制列表把矩形描出来（画在所有内容之上）
//   4) 下一帧开始时 → BeginDebugFrame() 清掉上一帧的记录
//
// 设计取舍：
//   · 只保留**一个**矩形。调试一次只选一个目标，多矩形会让界面更乱。
//   · 命中不到就什么都不画（而不是画到 (0,0)）—— 有些目标当前不可见（窗口折叠、
//     分页没打开），画错位置比不画更误导。
//   · 纯 ImGui 前景层实现，不碰玻璃渲染管线，零性能影响、零画面干扰。
// -----------------------------------------------------------------------------

namespace aurora::glass::debughl {

struct Rect final {
  float x0 = 0.0f;
  float y0 = 0.0f;
  float x1 = 0.0f;
  float y1 = 0.0f;

  bool Valid() const { return x1 > x0 && y1 > y0; }
};

// 每帧开头调用：清掉上一帧的矩形记录，但**保留**当前选中的目标 id。
void BeginDebugFrame();

// 设置当前正在调试的目标 id（由调试面板驱动）。传 nullptr / 空串 = 不高亮。
void SetDebugTarget(const char* targetId);

// 当前选中的目标 id（不会被 BeginDebugFrame 清掉）。
const char* DebugTarget();

// 组件绘制时调用：id 与当前调试目标一致就记录这块矩形。
// 返回 true 表示"这个组件就是正在调试的那个"，调用方可以据此做额外处理。
bool TryMarkRect(const char* targetId, const Rect& rect);

// 取本帧记录到的矩形（没命中时 Valid() 为 false）。
Rect MarkedRect();

// 在二级窗口绘制完成后调用：把记录到的矩形描出来。
// 内部用 ImGui::GetForegroundDrawList()，画在所有内容之上。
void DrawDebugHighlight();

// 把目标 id 翻译成人话（"整窗口"、"液态玻璃卡片"、"按钮 2"…），给高亮框的标签用。
// 找不到对应关系时返回 nullptr，调用方就不显示标签。
const char* HumanNameOf(const char* targetId);

}  // namespace aurora::glass::debughl
