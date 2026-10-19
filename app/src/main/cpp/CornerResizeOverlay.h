#pragma once

// ===========================================================================
//  「右下角长按 ⇒ 拖拽改窗口尺寸」的可复用接线（主窗口与二级窗口共用同一套）
// ===========================================================================
//  为什么抽成模块：主窗口（主题）与二级窗口都要这个交互，而"长按内核 + 手柄几何 +
//  激活反馈 + 触觉"这四件事必须是**同一个状态、同一个进度、同一次震动**（用户
//  明确要求三反馈同步）。各写一份接线的话，任何一处调参都会让两个窗口手感不一致。
//
//  ★★ 两种缩放方式（用户要求，可随时切换、共享同一套触发）：
//    模式 A 实时缩放     —— 拖动期间窗口宽高**实时改变**（原有实现，保持不变）；
//    模式 B 虚线预览缩放 —— 拖动期间窗口一个像素都不变，只更新**圆角虚线预览框**；
//                           松手后才用弹簧把真实窗口过渡到预览尺寸，
//                           并在前沿做液态玻璃填充（模糊/折射/透明度渐变）。
//    两者共用：同一个长按阈值、同一个判定区、同一套状态机、同一个手柄/边框/震动反馈、
//    同一套尺寸夹紧。区别只在"拖动期间改什么"与"松手后要不要过渡动画"。
//
//  分层（谁也不越界）：
//    ResizeGesture.h        长按状态机 / 判定区 / 尺寸夹紧（纯逻辑，可离线回归）
//    CornerResizeHandle.h   手柄几何与动画 + 边框激活参数（纯逻辑，可离线回归）
//    ResizePreview.h        两种模式的参数 / 虚线路径 / 提交动画 / 填充前沿（纯逻辑）
//    Haptics.h              触觉的"只放行一次"门（纯逻辑）
//    本文件                 把上面四者接到 ImGui 的一个窗口上（宿主接线）
//
//  ★ 四条来自真机的硬约束（改动前先读）：
//    ① 按住时长用**真实时钟**（steady_clock），不用 io.DeltaTime 累加 ——
//       Renderer 把 DeltaTime 夹在 [1/240, 0.05]，卡顿时按帧累加会把阈值拖成两倍。
//    ② 判定区里压着组件时**不是放弃**，而是"长按优先"：满阈值 ⇒ 缩放接管并在接管
//       那一刻 ClearActiveID()；提前松手 / 移动超容差 ⇒ 整笔还给组件。
//    ③ 新尺寸在 Begin() **之前**用 SetNextWindowSize 应用（左上角固定）⇒ 当帧生效，
//       不落后一帧。模式 B 的提交动画同样走这条路 —— 每帧都是**真实窗口尺寸**，
//       不是把旧窗口截图拉伸。
//    ④ 模式在**会话开始时锁定**：手势/动画进行中切换模式不会改变正在进行的这一次
//       操作，新模式在本次操作结束后生效（用户要求，见 effectiveMode/sessionMode）。
// ===========================================================================

#include "CornerResizeHandle.h"
#include "Haptics.h"
#include "ResizeGesture.h"
#include "ResizePreview.h"

#include <imgui.h>
#include <imgui_internal.h>   // ImGuiWindow（内部类型）

#include <chrono>

namespace aurora {

// 一个窗口的缩放运行时（每个窗口一份，互不影响）
struct CornerResizeRuntime {
    ResizeHold hold;
    ResizeActivation activation;      // ★ 唯一的激活状态 + 唯一的进度
    HapticGate haptic;                // 只在"未激活→激活"那一次放行
    bool hapticLogged = false;

    ImVec2 startSize{0.0f, 0.0f};     // 激活那一刻的尺寸
    bool hasPendingSize = false;
    ImVec2 pendingSize{0.0f, 0.0f};
    bool loggedActive = false;

    // ---- ★ 双模式 ----
    // effectiveMode：下一次缩放会话要用的模式（= 用户的选择，空闲时才更新）
    // sessionMode  ：**本次会话锁定的模式**（激活那一刻取一次，整个会话不变）
    ResizeMode effectiveMode = ResizeMode::Live;
    ResizeMode sessionMode = ResizeMode::Live;
    bool sessionHeld = false;         // 这一笔触摸是否已经进入过缩放会话（用于识别松手）

    // ---- ★ 模式 B：虚线预览 ----
    bool previewVisible = false;
    float previewWidth = 0.0f;        // 预览框的目标宽高（= 松手后要过渡到的尺寸）
    float previewHeight = 0.0f;

    // ---- ★ 模式 B：松手后的几何 + 玻璃填充过渡 ----
    ResizeCommitAnim commit;
    float commitBaseW = 0.0f;         // 松手那一刻的真实窗口尺寸（新区域的内侧边界）
    float commitBaseH = 0.0f;

    bool trace = false;               // AURORA_RESIZE_TRACE=1 逐帧探针
    int traceTick = 0;
    float appliedW = 0.0f;            // 上一帧真正应用的尺寸（对照"是否生效"）
    float appliedH = 0.0f;

    // 按下计时（真实时钟）
    bool pressClockValid = false;
    std::chrono::steady_clock::time_point pressClock;
};

// 宿主每帧喂进来的事实（模块不自己去猜这些）
struct CornerResizeFrameInput {
    bool pointerDown = false;        // ImGui 的左键按下（★ 不要用 activePointers，见②）
    float pointerX = 0.0f;
    float pointerY = 0.0f;
    float deltaSeconds = 1.0f / 120.0f;
    bool componentHovered = false;   // 指针是否压在本窗口的组件上（组件优先）
    float cornerRadiusPx = 28.0f;    // 窗口圆角（手柄贴着它画）
    const char* label = "窗口";      // 只用于日志
    // 用户当前选择的缩放方式（来自「主题 → 窗口缩放」页；手势中会被锁定）
    ResizeMode mode = ResizeMode::Live;
    // 这个窗口自己的长按/判定区/最小最大尺寸。留默认值即用全局默认。
    //   ★ 主窗口（调试面板）与二级窗口的内容密度不同，最小尺寸必须能分别设，
    //     否则要么"缩得太小没法用"，要么"想缩也缩不下去"。
    ResizeHoldParams params{};
};

// ① 在 ImGui::Begin() **之前**调用：把上一帧算出的新尺寸用掉
void CornerResizeApplyPending(CornerResizeRuntime& runtime);

// ② 在 Begin() 之后每帧调用一次。
//    返回 true 表示"这一笔触摸已经被缩放接管"，宿主必须跳过自己的拖动/滚动/点击。
bool CornerResizeUpdate(CornerResizeRuntime& runtime, ImGuiWindow* window,
                        const CornerResizeFrameInput& input);

// ③ 画手柄（画在**前景层**，可在 End() 之后调用；progress 为 0 时一个顶点都不产生）
void CornerResizeDrawHandle(const CornerResizeRuntime& runtime, ImGuiWindow* window,
                            float cornerRadiusPx);

// ③b 画虚线预览框 + 尺寸数值（同样画在**前景层**）。
//     ★ 前景层的裁剪矩形是**整个视口**（imgui.cpp:4388 PushClipRect(viewport)），
//       所以预览框画到原窗口外面也不会被窗口的裁剪矩形/缓冲区截断 ——
//       这正是"预览范围不得受原窗口 Surface 矩形缓冲错误裁剪"的落点。
void CornerResizeDrawPreview(const CornerResizeRuntime& runtime, ImGuiWindow* window,
                             float cornerRadiusPx);

// ④ 边框激活反馈（"加厚 + 提亮 + 折射增强"的样式增量），与手柄共用同一个进度
ResizeActivationStyle CornerResizeActivationStyle(const CornerResizeRuntime& runtime);
float CornerResizeProgress(const CornerResizeRuntime& runtime);

// ⑤ 模式 B 的提交动画：填充前沿几何 / 强度 / 边缘流动（宿主拿去提交玻璃面板）
ResizeRims CornerResizeFillRims(const CornerResizeRuntime& runtime, ImGuiWindow* window,
                                float cornerRadiusPx);
ResizeFillEnvelope CornerResizeFillEnvelopeOf(const CornerResizeRuntime& runtime);
float CornerResizeEdgeFlow(const CornerResizeRuntime& runtime);
// 当前会话状态（用户给的状态机命名；日志与验收都用它）
ResizeSessionState CornerResizeState(const CornerResizeRuntime& runtime);
// 提交动画是否在跑（宿主据此决定要不要画填充前沿）
bool CornerResizeCommitting(const CornerResizeRuntime& runtime);

// 三类参数 + 触觉参数（常量集中在这里，便于与离线回归对齐）
const ResizeHoldParams& CornerResizeHoldParams();
const CornerHandleParams& CornerResizeHandleParams();
const ResizeActivationParams& CornerResizeActivationParams();
const HapticParams& CornerResizeHapticParams();

}  // namespace aurora
