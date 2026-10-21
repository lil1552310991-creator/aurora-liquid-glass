// ===========================================================================
//  右下角长按缩放的宿主接线（实现，见头文件的五条 API 与四条真机约束）
//
//  ★★ 本轮新增：模式 B「虚线预览缩放」
//     · 复用**同一套**长按状态机（ResizeHold）与同一个判定区 —— 不许两套触控逻辑；
//     · 拖动期间只更新 previewWidth/Height（真实窗口一个像素都不动）；
//     · 松手后由 ResizeCommitAnim 用**实际帧时间**把窗口尺寸过渡到预览目标，
//       同时在移动前沿提交"正在填充的玻璃"（几何在 ResizePreview.h，面板在宿主）。
// ===========================================================================
#include "CornerResizeOverlay.h"

#include "third_party/liquidglass/ytbl/GlassUI.h"   // lgx::px（dp → px）

#include <android/log.h>

#include <cstdarg>
#include <cstdio>
#include <cstdlib>

namespace aurora {
namespace {

[[maybe_unused]] constexpr char kLogTag[] = "Aurora";

void ResizeLog(const char* format, ...) {
#if AURORA_SILENT
    // 纯净版：连 vsnprintf 都不做。这个函数在缩放拖拽时可能每帧被调用。
    (void)format;
#else
    char buffer[512];
    va_list args;
    va_start(args, format);
    std::vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    __android_log_print(ANDROID_LOG_INFO, kLogTag, "%s", buffer);
#endif
}

bool EnvOn(const char* name) {
    const char* value = std::getenv(name);
    return value != nullptr && value[0] == '1';
}

float PxPerDp() { return lgx::px(1.0f); }

const ResizePreviewParams& PreviewParams() { return ResizeSettingsRef().preview; }

}  // namespace

const ResizeHoldParams& CornerResizeHoldParams() {
    static const ResizeHoldParams kParams;
    return kParams;
}

const CornerHandleParams& CornerResizeHandleParams() {
    static const CornerHandleParams kParams;
    return kParams;
}

const ResizeActivationParams& CornerResizeActivationParams() {
    static const ResizeActivationParams kParams;
    return kParams;
}

// 触觉参数：常量可用环境变量覆盖，方便真机上直接 A/B（不用重编）。
//   AURORA_HAPTIC_CONSTANT=25 → 更重（HEAVY_CLICK）
//   AURORA_HAPTIC_CONSTANT=-1 → 关闭震动
const HapticParams& CornerResizeHapticParams() {
    static const HapticParams kParams = [] {
        HapticParams params;
        const char* value = std::getenv("AURORA_HAPTIC_CONSTANT");
        if (value != nullptr && value[0] != '\0') params.constant = std::atoi(value);
        return params;
    }();
    return kParams;
}

void CornerResizeApplyPending(CornerResizeRuntime& runtime) {
    if (!runtime.hasPendingSize) return;
    // ★ 在 Begin() 之前应用 ⇒ 当帧就用新尺寸绘制（与"拖动窗口"同一套路，不落后一帧）
    ImGui::SetNextWindowSize(runtime.pendingSize);
    runtime.appliedW = runtime.pendingSize.x;
    runtime.appliedH = runtime.pendingSize.y;
    runtime.hasPendingSize = false;
}

bool CornerResizeUpdate(CornerResizeRuntime& runtime, ImGuiWindow* window,
                       const CornerResizeFrameInput& input) {
    if (window == nullptr) {
        return false;
    }
    if (window->Collapsed) {
        // 收起 = 这一笔触摸不再有意义：**安全清理**，别让状态卡在"会话中"
        runtime.hold.Reset();
        runtime.sessionHeld = false;
        runtime.previewVisible = false;
        runtime.commit.Reset();
        return false;
    }

    // ---- 按下时钟：新的一笔按下 ⇒ 重新起表；松手 ⇒ 停表 ----
    if (!input.pointerDown) {
        runtime.pressClockValid = false;
    } else if (!runtime.pressClockValid) {
        runtime.pressClock = std::chrono::steady_clock::now();
        runtime.pressClockValid = true;
    }
    const float heldMs = runtime.pressClockValid
        ? std::chrono::duration<float>(std::chrono::steady_clock::now() - runtime.pressClock)
              .count() * 1000.0f
        : 0.0f;

    if (!runtime.trace) runtime.trace = EnvOn("AURORA_RESIZE_TRACE");

    const ResizeHoldParams& params = input.params;
    const float pxPerDp = PxPerDp();
    const ResizePreviewParams& previewParams = PreviewParams();

    // ---- ★ 模式切换只在"本次操作结束之后"生效（用户要求）------------------
    //   会话进行中（长按中 / 拖动中 / 提交动画中）一律使用 sessionMode，
    //   所以动画期间用户去主题窗口改模式，不会打断当前这次操作、也不会半途换语义。
    const bool busy = runtime.hold.Holding() || runtime.commit.active;
    if (!busy) runtime.effectiveMode = input.mode;

    // =====================================================================
    //  ⓪ 模式 B：松手后的尺寸 + 玻璃填充过渡（在长按状态机之前处理）
    // =====================================================================
    if (runtime.commit.active) {
        // 动画期间手柄/边框**保持完全展开**（收尾留给 RECOVERING）——
        // 与用户给的状态机一致：COMMIT_ANIMATING 之后才是 RECOVERING。
        runtime.activation.Step(true, input.deltaSeconds);
        const bool running = runtime.commit.Step(input.deltaSeconds);
        runtime.hasPendingSize = true;
        if (running) {
            runtime.pendingSize = ImVec2(runtime.commit.CurrentWidth(), runtime.commit.CurrentHeight());
        } else {
            // 精确落到预览目标（不是"差一点点"）⇒ 满足"动画结束时新尺寸与预览一致"
            runtime.pendingSize = ImVec2(runtime.commit.toW, runtime.commit.toH);
            runtime.previewVisible = false;
            ResizeLog("[缩放] %s：填充动画结束，最终 %.0fx%.0f（用时 %.0fms%s），"
                      "虚线框已消失",
                      input.label, runtime.commit.toW, runtime.commit.toH,
                      runtime.commit.settleMs, runtime.commit.forced ? "，被兜底时限收尾" : "");
        }
        if (runtime.trace && (runtime.traceTick++ % 10) == 0) {
            ResizeLog("[缩放] %s 填充动画 p=%.3f 当前=%.0fx%.0f 目标=%.0fx%.0f",
                      input.label, runtime.commit.Progress(), runtime.commit.CurrentWidth(),
                      runtime.commit.CurrentHeight(), runtime.commit.toW, runtime.commit.toH);
        }
        // ★ 一致性规则：动画期间不接受**新的**缩放手势（否则会出现"动画还没做完
        //   又从半路开始一次会话"的竞争）。这一笔触摸交还给宿主当作普通触摸。
        runtime.hold.Reset();
        runtime.sessionHeld = false;
        return true;
    }

    // =====================================================================
    //  ① 长按状态机（两种模式**完全共用**这一段）
    // =====================================================================
    // ★★ 判定区必须**让开滚动条**（两个方向都要）。
    //
    //   踩过的坑：判定区原本是"从窗口右下角向内 44dp"，把右侧 14px 的滚动条也盖住了。
    //   而 ImGui 的滚动条是在 **Begin() 内部**处理的 —— 按在滚动条的空白处会
    //   **立即按比例跳转**（不是相对拖动）。于是"长按右下角改窗口大小"会顺带
    //   把内容滚到几百像素以外，而且 ClearActiveID 根本来不及（Begin 已经处理完了）。
    //   真机截图铁证：刚启动、什么都没做的窗口，内容却停在"内阴影"那一组。
    //
    //   让开之后：滚动条上的长按 = 正常翻页（符合直觉），
    //   滚动条左边 44dp 内的长按 = 缩放（判定区仍有 74px 宽，够用）。
    //   ★ 二级窗口有 NoScrollbar ⇒ ScrollbarSizes 为 0 ⇒ 它的行为一个像素都不变。
    const float scrollbarW = window->ScrollbarY ? window->ScrollbarSizes.x : 0.0f;
    const float scrollbarH = window->ScrollbarX ? window->ScrollbarSizes.y : 0.0f;
    const bool inZone = CornerResizeZoneContains(
        window->Pos.x, window->Pos.y,
        window->Pos.x + window->Size.x - scrollbarW,
        window->Pos.y + window->Size.y - scrollbarH,
        lgx::px(params.zoneDp), input.pointerX, input.pointerY);
    const bool justArmed =
        runtime.hold.Update(input.pointerDown, inZone, input.pointerX, input.pointerY, heldMs,
                            params, lgx::px(params.moveToleranceDp), lgx::px(params.rebaseJumpDp));

    // ★★★ 唯一的激活状态 + 唯一的状态转换点 -------------------------------
    //  ①白色圆弧展开 ②窗口边框加厚/光学增强 ③一次轻震 —— 全部挂在这一处，
    //  绝不允许各自计时（否则三者会不同步，用户能看出来）。
    //  ★ 模式 B 的提交动画期间**保持**激活反馈（手柄/边框不提前收回），
    //    动画结束才进入 RECOVERING ⇒ 与用户给的状态机一致。
    const float progress =
        runtime.activation.Step(runtime.hold.Active() || runtime.commit.active,
                                input.deltaSeconds);
    if (runtime.haptic.Step(runtime.hold.Active()) && CornerResizeHapticParams().constant >= 0) {
        const bool sent = HapticTap(CornerResizeHapticParams());
        if (!runtime.hapticLogged) {
            runtime.hapticLogged = true;
            ResizeLog("[缩放] 震动：%s（constant=%d，可用=%d）", sent ? "已交给系统" : "不可用（安全降级）",
                      CornerResizeHapticParams().constant, static_cast<int>(HapticAvailable()));
        }
    }
    (void)progress;

    if (justArmed) {
        // 起始尺寸只在**激活那一刻**取一次，之后每帧 = 起始 + 手指位移（不做增量累加）
        runtime.startSize = window->Size;
        runtime.loggedActive = true;
        runtime.sessionHeld = true;                  // 这一笔触摸进入缩放会话
        // ★ 模式锁定：本次会话全程使用这一个模式
        runtime.sessionMode = runtime.effectiveMode;
        runtime.commit.Reset();
        runtime.previewVisible = (runtime.sessionMode == ResizeMode::DashedPreview);
        runtime.previewWidth = window->Size.x;
        runtime.previewHeight = window->Size.y;
        // 组件让位：不触发点击、不拖滑条、也不残留按下态
        if (input.componentHovered) ImGui::ClearActiveID();
        ResizeLog("[缩放] %s：长按 %.0fms 达成（真实按住 %.0fms，判定区 %.0fdp，组件让位=%d，"
                  "方式=%s）→ 圆弧展开 + 边框加厚 + 震动，尺寸起始 %.0fx%.0f",
                  input.label, params.longPressMs, heldMs, params.zoneDp,
                  static_cast<int>(input.componentHovered), ResizeModeName(runtime.sessionMode),
                  window->Size.x, window->Size.y);
    }

    if (runtime.hold.Active()) {
        // 左上角固定：新尺寸 = 起始尺寸 + 手指位移（夹到参数范围，且不超过屏幕余量）
        //   ★ 两种模式**共用**这一段尺寸计算 —— 最小/最大/屏幕余量约束不可能不一致。
        const ImVec2 screen = ImGui::GetIO().DisplaySize;
        const float maxW = screen.x - window->Pos.x;
        const float maxH = screen.y - window->Pos.y;
        const ResizeSize want =
            ResizeSizeFromDrag(runtime.startSize.x, runtime.startSize.y, runtime.hold.dx(),
                               runtime.hold.dy(), params, pxPerDp, maxW, maxH);
        runtime.sessionHeld = true;
        if (runtime.sessionMode == ResizeMode::Live) {
            // ---- 模式 A：真实窗口尺寸实时跟手（原有实现，一个字节都没改）----
            runtime.pendingSize = ImVec2(want.width, want.height);
            runtime.hasPendingSize = true;
        } else {
            // ---- 模式 B：只更新虚线预览框；**不许**碰 pendingSize ----
            //   ★ 显式清掉 pending：这是"拖动期间真实窗口尺寸一个像素都不变"的硬保证，
            //     不依赖"上一帧恰好没有残留"。
            runtime.hasPendingSize = false;
            runtime.previewVisible = true;
            runtime.previewWidth = want.width;
            runtime.previewHeight = want.height;
        }
        if (runtime.trace && (runtime.traceTick++ % 10) == 0) {
            ResizeLog("[缩放] %s 拖动中 d=(%.0f,%.0f) 起始=%.0fx%.0f → 目标=%.0fx%.0f "
                      "当前=%.0fx%.0f 上一帧应用=%.0fx%.0f 方式=%s",
                      input.label, runtime.hold.dx(), runtime.hold.dy(), runtime.startSize.x,
                      runtime.startSize.y, want.width, want.height, window->Size.x,
                      window->Size.y, runtime.appliedW, runtime.appliedH,
                      ResizeModeName(runtime.sessionMode));
        }
        return true;   // 缩放独占这一笔触摸
    }

    // =====================================================================
    //  ② 会话结束（松手）：模式 B 在这里启动"尺寸 + 玻璃填充"过渡
    // =====================================================================
    if (runtime.sessionHeld) {
        runtime.sessionHeld = false;
        if (runtime.sessionMode == ResizeMode::DashedPreview) {
            // ★ 起点 = **真实窗口当前尺寸**（预览期间它一直没变过），终点 = 预览目标。
            //   所以放大/缩小走的是同一条路径，没有两套逻辑。
            runtime.commitBaseW = window->Size.x;
            runtime.commitBaseH = window->Size.y;
            if (runtime.commit.Start(window->Size.x, window->Size.y, runtime.previewWidth,
                                     runtime.previewHeight, previewParams)) {
                // 本帧就开始推进几何（下一帧 Begin 时生效），并让手柄/边框继续展开
                runtime.hasPendingSize = true;
                runtime.pendingSize =
                    ImVec2(runtime.commit.CurrentWidth(), runtime.commit.CurrentHeight());
                ResizeLog("[缩放] %s：虚线预览松手 → 液态玻璃填充动画 %.0fx%.0f → %.0fx%.0f"
                          "（k=%.0f c=%.0f ζ=%.2f 参考时长 %.0fms，前沿 %s）",
                          input.label, runtime.commitBaseW, runtime.commitBaseH,
                          runtime.commit.toW, runtime.commit.toH, previewParams.springStiffness,
                          previewParams.springDamping,
                          previewParams.springDamping
                              / (2.0f * std::sqrt(previewParams.springStiffness > 1.0f
                                                      ? previewParams.springStiffness : 1.0f)),
                          EstimateCommitDurationMs(previewParams),
                          (runtime.commit.toW >= runtime.commitBaseW
                           && runtime.commit.toH >= runtime.commitBaseH) ? "扩展" : "收缩/混合");
            } else {
                // 尺寸没变（长按之后没拖）：直接结束，虚线框消失，不做动画
                runtime.previewVisible = false;
            }
        }
    }
    if (runtime.loggedActive && !runtime.hold.Holding()) {
        runtime.loggedActive = false;
        ResizeLog("[缩放] %s 结束，最终尺寸 %.0fx%.0f", input.label, window->Size.x,
                  window->Size.y);
    }
    // 长按还在数时间：也不让宿主的手势抢先提交（否则会一边滚动一边激活缩放）
    return runtime.hold.Holding();
}

void CornerResizeDrawHandle(const CornerResizeRuntime& runtime, ImGuiWindow* window,
                            float cornerRadiusPx) {
    if (window == nullptr || window->Collapsed) return;
    const float progress = runtime.activation.Progress();
    if (!(progress > 0.002f)) return;   // 完全收起 ⇒ 一个顶点都不产生（不留残影）
    ImDrawList* drawList = ImGui::GetForegroundDrawList();
    if (drawList == nullptr) return;

    const ImVec2 pos = window->Pos;
    const ImVec2 size = window->Size;
    const CornerHandleGeometry geometry = ComputeCornerHandle(
        pos.x, pos.y, pos.x + size.x, pos.y + size.y, cornerRadiusPx, PxPerDp(),
        CornerResizeHandleParams(), progress);
    if (!geometry.visible) return;

    float xy[kCornerHandleMaxPoints * 2];
    const int count = BuildCornerHandleOutline(geometry, xy, kCornerHandleMaxPoints * 2);
    if (count < 3) return;

    ImVec2 points[kCornerHandleMaxPoints];
    for (int i = 0; i < count; ++i) points[i] = ImVec2(xy[i * 2], xy[i * 2 + 1]);
    const int alpha = static_cast<int>(geometry.alpha * 255.0f + 0.5f);
    // 一条闭合多边形一次填充：没有折线接头、没有端帽叠色，AA 由 fringe 一次算好
    drawList->AddConcavePolyFilled(points, count, IM_COL32(255, 255, 255, alpha));
}

// ===========================================================================
//  模式 B：虚线预览框
// ===========================================================================
void CornerResizeDrawPreview(const CornerResizeRuntime& runtime, ImGuiWindow* window,
                             float cornerRadiusPx) {
    if (window == nullptr || window->Collapsed) return;
    if (runtime.sessionMode != ResizeMode::DashedPreview && !runtime.commit.active) return;
    if (!runtime.previewVisible && !runtime.commit.active) return;

    ImDrawList* drawList = ImGui::GetForegroundDrawList();
    if (drawList == nullptr) return;

    const ResizePreviewParams& p = PreviewParams();
    const float pxPerDp = PxPerDp();

    // 出现/消失的透明度：与手柄、边框加厚租用**同一个弹簧进度**（三反馈同步）；
    // 提交动画期间再乘一个"解体"曲线 ⇒ 窗口长到目标范围附近才平滑消失。
    float alpha = runtime.activation.Progress();
    if (runtime.commit.active) {
        alpha *= PreviewDissolveAlpha(runtime.commit.Progress(), p);
        if (alpha <= 0.004f) return;
    }
    if (!(alpha > 0.004f)) return;

    const float left = window->Pos.x;
    const float top = window->Pos.y;
    const float width = runtime.previewWidth > 1.0f ? runtime.previewWidth : window->Size.x;
    const float height = runtime.previewHeight > 1.0f ? runtime.previewHeight : window->Size.y;
    const float right = left + width;
    const float bottom = top + height;
    const float radius = PreviewCornerRadius(width, height, cornerRadiusPx, p, pxPerDp);

    const float lineWidth = p.lineWidthDp * pxPerDp;
    const float dashLen = p.dashLengthDp * pxPerDp;
    const float gapLen = p.dashGapDp * pxPerDp;

    static DashSegment segments[kMaxDashSegments];
    const int count = BuildDashedRoundedRect(left, top, right, bottom, radius, dashLen, gapLen,
                                            0.0f, segments, kMaxDashSegments);
    if (count <= 0) return;

    const int mainAlpha = static_cast<int>(p.lineAlpha * alpha * 255.0f + 0.5f);
    const int underAlpha = static_cast<int>(p.underlayAlpha * alpha * 255.0f + 0.5f);
    // 下衬一条略粗的**暗线**：亮背景（白页/浅色 App）上白虚线会看不见，
    // 垫一层暗线之后明暗背景都可辨（用户要求"明亮和暗色背景下保持可见"）。
    const float underWidth = lineWidth + p.underlayExtraDp * pxPerDp;
    for (int pass = 0; pass < 2; ++pass) {
        const ImU32 color = pass == 0 ? IM_COL32(0, 0, 0, underAlpha)
                                      : IM_COL32(255, 255, 255, mainAlpha);
        const float thickness = pass == 0 ? underWidth : lineWidth;
        for (int i = 0; i < count; ++i) {
            drawList->AddLine(ImVec2(segments[i].x0, segments[i].y0),
                              ImVec2(segments[i].x1, segments[i].y1), color, thickness);
        }
    }

    // ---- 预计宽高数值（画在预览框**外面**，不遮挡窗口内部的 Demo）----
    if (!p.showSizeLabel) return;
    char label[64];
    FormatPreviewSizeLabel(width, height, label, static_cast<int>(sizeof(label)));
    ImFont* font = ImGui::GetFont();
    if (font == nullptr) return;
    const float fontSize = ImGui::GetFontSize() * (p.labelScale > 0.3f ? p.labelScale : 0.72f);
    const ImVec2 textSize = font->CalcTextSizeA(fontSize, 1.0e6f, 0.0f, label);
    const float padX = 6.0f * pxPerDp * 0.5f;
    const float padY = 3.0f * pxPerDp * 0.5f;
    const float offset = p.labelOffsetDp * pxPerDp;

    float labelX = right + offset;
    float labelY = bottom + offset;
    const ImVec2 screen = ImGui::GetIO().DisplaySize;
    if (labelX + textSize.x + padX * 2.0f > screen.x - 4.0f) {
        labelX = right - offset - textSize.x - padX * 2.0f;   // 右边放不下 ⇒ 挪到框内
    }
    if (labelY + textSize.y + padY * 2.0f > screen.y - 4.0f) {
        labelY = bottom - offset - textSize.y - padY * 2.0f;  // 下边放不下 ⇒ 挪到框内
    }
    if (labelX < 4.0f) labelX = 4.0f;
    if (labelY < 4.0f) labelY = 4.0f;

    const float badgeRadius = 4.0f * pxPerDp * 0.5f;
    const int badgeAlpha = static_cast<int>(0.45f * alpha * 255.0f + 0.5f);
    drawList->AddRectFilled(ImVec2(labelX, labelY),
                            ImVec2(labelX + textSize.x + padX * 2.0f,
                                   labelY + textSize.y + padY * 2.0f),
                            IM_COL32(0, 0, 0, badgeAlpha), badgeRadius);
    const int textAlpha = static_cast<int>(0.96f * alpha * 255.0f + 0.5f);
    drawList->AddText(font, fontSize, ImVec2(labelX + padX, labelY + padY),
                      IM_COL32(255, 255, 255, textAlpha), label);
}

// ===========================================================================
//  模式 B：提交动画的填充前沿 / 强度 / 边缘流动
// ===========================================================================
bool CornerResizeCommitting(const CornerResizeRuntime& runtime) { return runtime.commit.active; }

ResizeFillEnvelope CornerResizeFillEnvelopeOf(const CornerResizeRuntime& runtime) {
    if (!runtime.commit.active) return ResizeFillEnvelope{};
    return ComputeResizeFill(runtime.commit.Progress());
}

float CornerResizeEdgeFlow(const CornerResizeRuntime& runtime) {
    if (!runtime.commit.active) return 0.0f;
    const ResizePreviewParams& p = PreviewParams();
    return ComputeResizeFill(runtime.commit.Progress()).edgeFlow * p.edgeFlowGain;
}

ResizeRims CornerResizeFillRims(const CornerResizeRuntime& runtime, ImGuiWindow* window,
                                float cornerRadiusPx) {
    if (window == nullptr || !runtime.commit.active) return ResizeRims{};
    const ResizePreviewParams& p = PreviewParams();
    if (!p.fillOn) return ResizeRims{};
    return ComputeResizeRims(window->Pos.x, window->Pos.y, window->Size.x, window->Size.y,
                             runtime.commitBaseW, runtime.commitBaseH, cornerRadiusPx,
                             p.fillRimWidthDp * PxPerDp());
}

ResizeSessionState CornerResizeState(const CornerResizeRuntime& runtime) {
    return ClassifyResizeSession(runtime.hold.Holding(), runtime.hold.Active(),
                                 runtime.hold.phase == ResizePhase::Dragging,
                                 runtime.commit.active, runtime.activation.Progress(),
                                 runtime.sessionMode);
}

ResizeActivationStyle CornerResizeActivationStyle(const CornerResizeRuntime& runtime) {
    return ComputeResizeActivation(CornerResizeActivationParams(), runtime.activation.Progress());
}

float CornerResizeProgress(const CornerResizeRuntime& runtime) {
    return runtime.activation.Progress();
}

}  // namespace aurora
