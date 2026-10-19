#pragma once

#include <cstdint>

namespace aurora {

enum class HostMode : std::uint8_t { Activity = 0, ApplicationOverlay = 1, RootNative = 2 };
enum class TouchPolicy : std::uint8_t { Passthrough = 0, PanelOnly = 1, Modal = 2 };

struct TouchRegion final {
    float left = 0.0f;
    float top = 0.0f;
    float right = 0.0f;
    float bottom = 0.0f;

    bool contains(float x, float y) const {
        return x >= left && x < right && y >= top && y < bottom;
    }
};

// ===========================================================================
// 触摸拦截区尺寸 —— **必须全局唯一一份**
//
// 为什么放在这里而不是各自的 .cpp 里：这个数有三处必须一致
//   1. `aurora_overlay.cpp` 调 `attachInputWindow(name, W, H)` —— 真正设 region 的地方
//   2. `LiquidGlassRuntime.cpp` 把两个 ImGui 窗口**夹进**这个框（见下面的说明）
//   3. 启动自检 `selfCheck(&check, W, H)` —— 核对真实 region 是否等于它
// 曾经因为三处各写一份而对不上：窗口被拖出框外就"点不动"。
//
// ★ 为什么窗口要被夹住：`Region` 替换符号已按 [未核实-2] 禁用 ⇒
//   **拦截区只能在启动时设一次**，无法跟随窗口拖动。窗口一旦被拖出去，
//   超出部分就点不动（用户实测报告过）。夹住 = 保留拖动但拖不出去。
//
// ★ 尺寸依据：两个窗口都是 760 宽（二级窗口 760x700 @ (16,16)，
//   主题窗口 760x1400 @ (16,730)）⇒ 并集 (16,16)-(776,2130) ⇒ 框到 800x2160。
//   宽 800 而不是 900：右侧留出穿透带，否则**空白区也被拦截**、用户滑不动
//   （用户实测报告过「空白的没窗口遮挡的地方我滑动不了」）。
// ===========================================================================
inline constexpr int kLiquidGlassTouchBoxW = 800;
inline constexpr int kLiquidGlassTouchBoxH = 2160;

struct HostCapabilities final {
    HostMode mode = HostMode::Activity;
    TouchPolicy touchPolicy = TouchPolicy::PanelOnly;
    bool hasSurface = false;
    bool supportsBackgroundTexture = false;
    bool supportsIme = false;
    bool supportsSystemOverlay = false;
    bool supportsRootNative = false;
    std::int32_t apiLevel = 0;
    float density = 1.0f;
    TouchRegion interactiveRegion{};
    TouchRegion safeRegion{};
    // ★ 系统输入法弹层在屏幕上的矩形（物理像素；全 0 = 未知/已收起）。
    //   由 Renderer 每帧填（数据来自 Java 侧的 WindowInsets.Type.ime()），
    //   宿主界面层用它做"窗口避让"，见 ImeAvoid.h。
    int imeLeft = 0;
    int imeTop = 0;
    int imeRight = 0;
    int imeBottom = 0;
};

}  // namespace aurora
