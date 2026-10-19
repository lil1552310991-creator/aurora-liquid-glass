#pragma once

#include "InputRouter.h"
#include "GestureRouter.h"
#include "HostProtocol.h"
#include "FrameStats.h"
#include "RenderBackend.h"

namespace aurora {

void InitializeLiquidGlass();
void ShutdownLiquidGlass();
void BeginLiquidGlassFrame(float deltaSeconds, float width, float height,
                           float density, const InputSnapshot& input,
                           const BackendDescriptor& backend);
void UpdateLiquidGlassDiagnostics(const HostCapabilities& host,
                                  const BackendDescriptor& backend,
                                  const BackendDescriptor& vulkanProbe,
                                  const FrameStats& frameStats,
                                  const InputSnapshot& input,
                                  const GestureFrame& gesture,
                                  bool textInputRequested);
void DrawLiquidGlassShowcase();
bool LiquidGlassWantsTextInput();
// ★ 用户在主题窗口上按了 × ⇒ 请求**退出整个覆盖层**（不是"收起窗口"）。
//   主循环轮询它，然后走正常收尾路径（退出码 0），和 --max-seconds 到期是同一条路。
bool LiquidGlassQuitRequested();
void SetLiquidGlassVisible(bool visible);
bool LiquidGlassVisible();
TouchRegion LiquidGlassRegion();

}  // namespace aurora
