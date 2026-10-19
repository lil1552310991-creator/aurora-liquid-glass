// =============================================================================
//  Liquid Glass Inspector —— 调试工作台（主窗口「主题」页的内容）
//
//  这一层只做三件事：
//    ① 从**注册表**生成分页栏与参数控件（没有一条手写的组件参数清单）；
//    ② 把用户操作翻译成注册表调用（set / reset / 快照 / 预设 / 撤销）；
//    ③ 提供一个可关闭的诊断面板。
//
//  ★ 它**不认识任何具体组件**：不知道有几个按钮、开关叫什么、哪个参数是 dp。
//    要加一个新组件，只需要在注册时多写几行绑定（GlassTargets.cpp），
//    分页栏与控件会自动出现 —— 这正是"支持动态发现组件"的实现方式。
// =============================================================================
#pragma once

namespace aurora {
namespace glass {

// 宿主每帧填一次的诊断快照（背景采样来源、裁剪边界、帧耗时…）。
// 这些量住在渲染器里，工作台只负责显示，因此由宿主注入而不是自己去问。
struct WorkbenchDiagnostics {
  const char* backdropOrigin = "unknown";   // PlanBackdrop 给出的来源名
  unsigned int backdropTexture = 0;
  int backdropMapWidth = 0;
  int backdropMapHeight = 0;
  bool backdropTransparentHost = false;
  float clipLeft = 0.0f;                    // 当前目标的裁剪边界（物理像素）
  float clipTop = 0.0f;
  float clipRight = 0.0f;
  float clipBottom = 0.0f;
  float contentMaskRadius = -1.0f;          // 内容圆角遮罩半径（<0 = 关闭）
  double fps = 0.0;
  double worstFrameMs = 0.0;
  unsigned long long frameCount = 0;
  const char* shaderError = nullptr;        // 非空 ⇒ 显示为红色
  const char* effectChain = nullptr;        // 当前目标的效果链摘要（可选）
};

void SetWorkbenchDiagnostics(const WorkbenchDiagnostics& diagnostics);

// 画工作台（在主题窗口的「液态玻璃」标签页里调用）。
void DrawGlassWorkbench();

// 启动/收尾钩子：启动时尝试从磁盘恢复参数，收尾时把未保存的改动落盘。
void BeginGlassWorkbench();     // 幂等；第一次绘制前调用即可
void FlushGlassWorkbench();     // 退出前调用（原子保存）

// ★★ 持久化泵：首次从磁盘恢复（只做一次）+ 之后按去抖自动保存。
//    **每帧调用一次**（宿主在帧首调），与"当前画的是哪一页"无关 ——
//    否则用户停在别的标签页改参数就不会落盘（这条正是被端到端验收抓出来的）。
void PumpGlassWorkbenchPersistence();

// 供诊断与外部工具（例如未来的 AI Agent）查询的统计
struct WorkbenchStats {
  int targets = 0;
  int params = 0;
  int unsupported = 0;
  int changed = 0;
  int presets = 0;
  bool dirty = false;
  bool loadedFromDisk = false;
  const char* activePreset = "";
  const char* path = "";
};
WorkbenchStats CurrentWorkbenchStats();

}  // namespace glass
}  // namespace aurora
