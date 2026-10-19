#include "Renderer.h"
#include "ThreadTuning.h"   // 调度优化：通用 + 按本机 CPU 拓扑的设备深度优化
#include <android/log.h>

#include "LiquidGlassRuntime.h"

#include "imgui.h"
#include "imgui_internal.h"
#include "third_party/liquidglass/secondary_imgui.h"

#include "MouseEdgeSequencer.h"
#include "third_party/liquidglass/ytbl/GlassUI.h"

#include <algorithm>
#include <cmath>
#include <chrono>
#include <utility>
#include <cstdio>
#include <cstdlib>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <GLES3/gl3.h>

namespace aurora {

// 按键边沿定序器（见 MouseEdgeSequencer.h 的完整动机）。
//
// 放在文件作用域而不是 renderFrame 内的 static，是因为**输入通道重建时
// 也要能 reset()** —— Renderer.cpp:117 / :162 的 input_.cancelAll() 那两处。
// 若把等待中的抬起留在定序器里跨过通道重建，那一次抬起就会被发到新的通道上。
//
// renderFrame 只被单一渲染线程调用，不需要额外同步。
static ButtonEdgeSequencer g_buttonSequencer;

static bool LateLatchEnabled() {
    // Keep the original early-import path available for same-device A/B runs.
    static const bool enabled = [] {
        const char* value = std::getenv("AURORA_LATE_LATCH");
        return value == nullptr || value[0] != '0';
    }();
    return enabled;
}

// ★ 提前开画（AURORA_EARLY_RENDER=0 关闭）：
//
// 原来的循环是"睡到固定 deadline 才开画"。背景帧由采集线程随时送达，如果它
// 恰好落在 deadline 之后几毫秒，这一帧就赶不上本次绘制 —— 于是本次用的仍是
// **上一帧**背景，玻璃里的内容凭空老了一整帧（120Hz 下 8.3ms）。
//
// 现在：睡到 `deadline - 半个周期`，若这半个周期内**新背景帧到了**就立刻开画
// （最多提前半个周期），并把下一个 deadline 重设成"本次开画 + 一个周期"，
// 所以帧率上限仍是目标帧率、不会变成 240fps。
// 提前的那次绘制仍然在同一个 vsync 之前完成，因此**不会**改变上屏时刻，
// 只是把"已到手的新背景"用上，减少玻璃里内容的年龄。
static bool EarlyRenderEnabled() {
    static const bool enabled = [] {
        const char* value = std::getenv("AURORA_EARLY_RENDER");
        return value == nullptr || value[0] != '0';
    }();
    return enabled;
}

// 部分接管：只铺玻璃面板的包围盒，而不是整屏。
//
// ★ 默认关闭 —— 用户验收结论：矩形边界上**有明显接缝**，滑动时肉眼可见；
//   GPU 负载确实从 646MHz 降到 222MHz（峰值 1050→222），但**用户感知不到流畅度提升**。
//   视觉代价确定、收益不确定，所以不作为默认路径。
//   代码保留：`AURORA_PARTIAL_TAKEOVER=1` 可开启做对照实验或将来复用。
static bool PartialTakeoverEnabled() {
    static const bool enabled = [] {
        const char* value = std::getenv("AURORA_PARTIAL_TAKEOVER");
        return value != nullptr && value[0] == '1';
    }();
    return enabled;
}

static void RefreshLateBackdrop(const BackendDescriptor& descriptor,
                               int width, int height, bool glassVisible) {
    const BackdropPlan plan = PlanBackdrop(
        BackdropInputsFor(descriptor, width, height, glassVisible));
    if (plan.texture != 0 && plan.mappingWidth > 0 && plan.mappingHeight > 0) {
        lgx::bindBackdropTexture(static_cast<unsigned int>(plan.texture),
                                 plan.mappingWidth, plan.mappingHeight);
    } else {
        lgx::releaseBackdropTexture();
    }
    lgx::setBackdropEnabled(true);
}

static void DumpFrame(const std::string& path, int width, int height) {
    if (path.empty() || width <= 0 || height <= 0) return;
    std::vector<std::uint8_t> rgba(static_cast<std::size_t>(width) * height * 4u);
    GLint oldPackAlignment = 4;
    glGetIntegerv(GL_PACK_ALIGNMENT, &oldPackAlignment);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());
    glPixelStorei(GL_PACK_ALIGNMENT, oldPackAlignment);
    const int outWidth = (width + 3) / 4;
    const int outHeight = (height + 3) / 4;
    std::vector<std::uint8_t> rgb(static_cast<std::size_t>(outWidth) * outHeight * 3u);
    for (int y = 0; y < outHeight; ++y) {
        for (int x = 0; x < outWidth; ++x) {
            const std::size_t src = (static_cast<std::size_t>(height - 1 - y * 4) * width + x * 4) * 4u;
            const std::size_t dst = (static_cast<std::size_t>(y) * outWidth + x) * 3u;
            std::copy_n(rgba.data() + src, 3u, rgb.data() + dst);
        }
    }
    // A diagnostic path is explicit. Refuse symlinks and keep screenshot data
    // private even when the process's umask would otherwise permit other readers.
    const int fd = open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC | O_NOFOLLOW, 0600);
    if (fd < 0) return;
    if (fchmod(fd, 0600) != 0) { close(fd); return; }
    FILE* file = fdopen(fd, "wb");
    if (file == nullptr) { close(fd); return; }
    fprintf(file, "P6\n%d %d\n255\n", outWidth, outHeight);
    fwrite(rgb.data(), 1, rgb.size(), file);
    fclose(file);
}

static std::vector<VisibleUiShape> GatherVisibleTouchShapes(float displayScale,
                                                            int imeLeft, int imeTop,
                                                            int imeRight, int imeBottom) {
    std::vector<VisibleUiShape> shapes;
    secondary_imgui::AppendTouchShapes(shapes);
    const ImGuiID secondaryWindowId = secondary_imgui::TouchWindowId();
    const ImGuiContext& context = *ImGui::GetCurrentContext();
    for (const ImGuiWindow* window : context.Windows) {
        if (window->ID == secondaryWindowId || !window->Active || window->Hidden ||
            window->LastFrameActive != context.FrameCount ||
            (window->Flags & (ImGuiWindowFlags_NoMouseInputs | ImGuiWindowFlags_Tooltip))) {
            continue;
        }
        // Child windows and popups use their own clipped visible rectangles;
        // never expand their parent or swallow the gap between separate windows.
        const ImRect bounds = window->Collapsed ? window->TitleBarRect() : window->Rect();
        if (bounds.GetWidth() <= 0.0f || bounds.GetHeight() <= 0.0f) continue;
        VisibleUiShape shape;
        shape.left = bounds.Min.x;
        shape.top = bounds.Min.y;
        shape.right = bounds.Max.x;
        shape.bottom = bounds.Max.y;
        shape.radius = window->WindowRounding;
        shape.shape = shape.radius > 0.0f ? VisibleUiShapeKind::RoundedRectangle
                                         : VisibleUiShapeKind::Rectangle;
        shape.clipLeft = window->OuterRectClipped.Min.x;
        shape.clipTop = window->OuterRectClipped.Min.y;
        shape.clipRight = window->OuterRectClipped.Max.x;
        shape.clipBottom = window->OuterRectClipped.Max.y;
        shapes.push_back(shape);
    }
    for (VisibleUiShape& shape : shapes) {
        shape.left *= displayScale;
        shape.top *= displayScale;
        shape.right *= displayScale;
        shape.bottom *= displayScale;
        shape.radius *= displayScale;
        shape.clipLeft *= displayScale;
        shape.clipTop *= displayScale;
        shape.clipRight *= displayScale;
        shape.clipBottom *= displayScale;
    }
    // ★★ 把**输入法弹层**那一块从拦截区里挖掉。
    //
    //  我们的图层挂在 SurfaceFlinger 的 mRoot 下、是 WMS Display Root 的**兄弟**，
    //  z 序在整棵 WMS 子树（含系统输入法弹层）之上 ⇒ 落在键盘上的触摸会被我们
    //  这层先接走：键盘能弹出来、但按键点不动。Java 侧把 WindowInsets.Type.ime()
    //  的真实矩形回传上来之后，这里按"屏幕 − 键盘"裁剪每个形状：
    //    · 键盘是贴着屏幕底部的横条 ⇒ 把 bottom 夹到键盘顶边即可（保留上方区域）；
    //    · 夹完高度 ≤ 0 的形状直接丢掉（整块都被键盘盖住 ⇒ 本来也不该拦触摸）。
    //  ★ 未知矩形（全 0）时保持原行为，绝不因为拿不到 insets 就把拦截区搞没。
    if (imeRight > imeLeft && imeBottom > imeTop) {
        const float keyboardTop = static_cast<float>(imeTop);
        const float keyboardBottom = static_cast<float>(imeBottom);
        const float keyboardLeft = static_cast<float>(imeLeft);
        const float keyboardRight = static_cast<float>(imeRight);
        std::vector<VisibleUiShape> kept;
        kept.reserve(shapes.size());
        for (VisibleUiShape& shape : shapes) {
            const bool overlaps = shape.right > keyboardLeft && shape.left < keyboardRight &&
                                  shape.bottom > keyboardTop && shape.top < keyboardBottom;
            if (!overlaps) {
                kept.push_back(shape);
                continue;
            }
            if (shape.top >= keyboardTop) continue;          // 整块都在键盘里 ⇒ 丢弃
            shape.bottom = keyboardTop;                      // 否则夹到键盘顶边
            shape.clipBottom = std::min(shape.clipBottom, keyboardTop);
            if (shape.bottom - shape.top <= 0.5f) continue;
            kept.push_back(shape);
        }
        shapes.swap(kept);
    }
    return shapes;
}

static bool CaptureDrawDamage(const ImDrawData* drawData, RenderDamageState& state) {
    if (!drawData || !drawData->Valid) return false;
    auto& bytes = state.bytes;
    bytes.clear();
    AppendRenderDamageScalar(bytes, drawData->CmdListsCount);
    AppendRenderDamageScalar(bytes, drawData->DisplayPos.x);
    AppendRenderDamageScalar(bytes, drawData->DisplayPos.y);
    AppendRenderDamageScalar(bytes, drawData->DisplaySize.x);
    AppendRenderDamageScalar(bytes, drawData->DisplaySize.y);
    AppendRenderDamageScalar(bytes, drawData->FramebufferScale.x);
    AppendRenderDamageScalar(bytes, drawData->FramebufferScale.y);
    // The project's default vertex layout is fully packed (no undefined padding)
    // and the entire GPU input must compare exactly, without hash collisions.
    static_assert(sizeof(ImDrawVert) == 20 && offsetof(ImDrawVert, pos) == 0 &&
                  offsetof(ImDrawVert, uv) == 8 && offsetof(ImDrawVert, col) == 16);
    bool complete = true;
    const ImTextureID fontTexture = ImGui::GetIO().Fonts->TexID;
    for (int index = 0; index < drawData->CmdListsCount; ++index) {
        const ImDrawList& list = *drawData->CmdLists[index];
        AppendRenderDamageScalar(bytes, list.VtxBuffer.Size);
        AppendRenderDamageBytes(bytes, list.VtxBuffer.Data,
                                static_cast<std::size_t>(list.VtxBuffer.Size) * sizeof(ImDrawVert));
        AppendRenderDamageScalar(bytes, list.IdxBuffer.Size);
        AppendRenderDamageBytes(bytes, list.IdxBuffer.Data,
                                static_cast<std::size_t>(list.IdxBuffer.Size) * sizeof(ImDrawIdx));
        AppendRenderDamageScalar(bytes, list.CmdBuffer.Size);
        for (const ImDrawCmd& command : list.CmdBuffer) {
            AppendRenderDamageScalar(bytes, command.ClipRect.x);
            AppendRenderDamageScalar(bytes, command.ClipRect.y);
            AppendRenderDamageScalar(bytes, command.ClipRect.z);
            AppendRenderDamageScalar(bytes, command.ClipRect.w);
            AppendRenderDamageScalar(bytes, reinterpret_cast<std::uintptr_t>(command.TextureId));
            AppendRenderDamageScalar(bytes, command.VtxOffset);
            AppendRenderDamageScalar(bytes, command.IdxOffset);
            AppendRenderDamageScalar(bytes, command.ElemCount);
            AppendRenderDamageScalar(bytes, reinterpret_cast<std::uintptr_t>(command.UserCallback));
            AppendRenderDamageScalar(bytes, reinterpret_cast<std::uintptr_t>(command.UserCallbackData));
            if (command.UserCallback) {
                if (command.UserCallback != ImDrawCallback_ResetRenderState &&
                    !secondary_imgui::IsPanelDrawCallback(command.UserCallback)) complete = false;
            } else if (command.ElemCount && command.TextureId != fontTexture) {
                // External textures may change while vertex/command data remains
                // identical. Only our font and snapshotted panels are proven.
                complete = false;
            }
        }
    }
    lgx::appendPanelVisualSnapshot(bytes);
    return complete;
}

Renderer::Renderer() {
    vulkanProbe_ = ProbeVulkanBackend();
    thread_ = std::thread(&Renderer::run, this);
}

void Renderer::setHostMode(HostMode mode) {
    std::lock_guard lock(mutex_);
    hostCapabilities_.mode = mode;
    hostCapabilities_.supportsSystemOverlay = mode == HostMode::ApplicationOverlay;
    // RootNative（纯 ELF 自建的 SurfaceFlinger 图层）同样可以拉起系统输入法 ——
    // 这一点已由 ime-bridge 真机验证（research/ime_result.md §十一 +
    // research/ime_physical_run/README.md 的像素级证据）。原先写成
    // `mode != HostMode::RootNative` 会让运行时信息面板显示「IME unavailable」，
    // 与我们正在演示的能力矛盾。
    hostCapabilities_.supportsIme = true;
    hostCapabilities_.supportsRootNative = mode == HostMode::RootNative;
    hostCapabilities_.touchPolicy = TouchPolicy::PanelOnly;
    // ★ 透明底：只有 Activity（独占一个 Activity 的 Surface）才需要不透明背景。
    //   RootNative 走的是自建 SF 图层，`GlesBackend::beginFrame` 在 opaque 分支会
    //   `glClearColor(0.035f, 0.045f, 0.07f, 1.0f)` —— 那会把整块 1080x2376 图层
    //   涂成不透明深蓝，直接盖满用户屏幕（实测前发现的缺陷）。
    //   覆盖层必须走 transparent 分支的 `glClearColor(0,0,0,0)`。
    backend_.setTransparentSurface(mode != HostMode::Activity);
}

void Renderer::setBackendKind(BackendKind kind) {
    requestedBackendKind_.store(kind, std::memory_order_release);
}

void Renderer::setInitialTab(int tab) {
    std::lock_guard lock(mutex_);
    ui_.setInitialTab(tab);
}

void Renderer::setTargetFrameRate(float framesPerSecond) {
    if (!std::isfinite(framesPerSecond)) return;
    targetFrameRate_.store(std::clamp(framesPerSecond, 1.0f, 240.0f), std::memory_order_release);
    condition_.notify_all();
}

FramePerfSnapshot Renderer::framePerfSnapshot() {
    std::lock_guard lock(mutex_);
    return framePerfSnapshot_;
}

void Renderer::setLiquidGlassVisible(bool visible) {
    SetLiquidGlassVisible(visible);
}

TouchRegion Renderer::interactiveRegion() {
    std::lock_guard lock(mutex_);
    if (!surfaceAvailable_ || paused_ || width_ <= 0 || height_ <= 0) {
        return {};
    }
    const float logicalWidth = static_cast<float>(width_) / density_;
    const float logicalHeight = static_cast<float>(height_) / density_;
    const TouchRegion region = hostCapabilities_.interactiveRegion;
    return {region.left / logicalWidth, region.top / logicalHeight,
            region.right / logicalWidth, region.bottom / logicalHeight};
}

std::vector<VisibleUiShape> Renderer::visibleTouchShapes(int expectedWidth, int expectedHeight) {
    std::lock_guard lock(mutex_);
    if (!surfaceAvailable_ || paused_ || width_ <= 0 || height_ <= 0) return {};
    if ((expectedWidth > 0 && expectedWidth != width_) || (expectedHeight > 0 && expectedHeight != height_)) return {};
    return visibleTouchShapes_;
}

IRenderBackend& Renderer::activeBackend() {
    return requestedBackendKind_.load(std::memory_order_acquire) == BackendKind::Vulkan
        ? static_cast<IRenderBackend&>(vulkanBackend_)
        : static_cast<IRenderBackend&>(backend_);
}

const IRenderBackend& Renderer::activeBackend() const {
    return requestedBackendKind_.load(std::memory_order_acquire) == BackendKind::Vulkan
        ? static_cast<const IRenderBackend&>(vulkanBackend_)
        : static_cast<const IRenderBackend&>(backend_);
}

Renderer::~Renderer() {
    {
        std::lock_guard lock(mutex_);
        stopping_ = true;
        condition_.notify_all();
    }
    if (thread_.joinable()) {
        thread_.join();
    }
    if (pendingProjectionHardwareBuffer_ != nullptr) {
        AHardwareBuffer_release(pendingProjectionHardwareBuffer_);
        pendingProjectionHardwareBuffer_ = nullptr;
    }
    if (pendingProjectionReadComplete_) {
        std::exchange(pendingProjectionReadComplete_, {})();
    }
}

void Renderer::surfaceCreated(ANativeWindow* window) {
    std::lock_guard lock(mutex_);
    if (pendingWindow_ != nullptr) {
        ANativeWindow_release(pendingWindow_);
    }
    pendingWindow_ = window;
    if (pendingWindow_ != nullptr) {
        ANativeWindow_acquire(pendingWindow_);
    }
    surfaceAvailable_ = pendingWindow_ != nullptr;
    hostCapabilities_.hasSurface = surfaceAvailable_;
    ++surfaceGeneration_;
    condition_.notify_all();
}

void Renderer::surfaceChanged(int width, int height, float density) {
    input_.cancelAll(0.0);
    AHardwareBuffer* superseded = nullptr;
    std::function<void()> completed;
    {
    std::lock_guard lock(mutex_);
    width_ = std::max(1, width);
    height_ = std::max(1, height);
    density_ = std::max(0.1f, density);
    hostCapabilities_.density = density_;
    hostCapabilities_.interactiveRegion = {};
    visibleTouchShapes_.clear();
    ++viewportGeneration_;
    resetPointerForViewport_ = true;
    pendingProjectionRgba_.clear();
    projectionFramePending_ = false;
    superseded = std::exchange(pendingProjectionHardwareBuffer_, nullptr);
    completed = std::exchange(pendingProjectionReadComplete_, {});
    pendingProjectionSourceTimestampNs_ = 0;
    }
    if (superseded) AHardwareBuffer_release(superseded);
    if (completed) completed();
    condition_.notify_all();
}

void Renderer::setSafeRegion(float left, float top, float right, float bottom) {
    std::lock_guard lock(mutex_);
    hostCapabilities_.safeRegion = TouchRegion{left, top, right, bottom};
}

void Renderer::surfaceDestroyed() {
    input_.cancelAll(0.0);
    // 丢掉等待中的抬起：它属于即将销毁的那条输入通道，不能泄漏到新通道上。
    g_buttonSequencer.reset();
    std::lock_guard lock(mutex_);
    if (pendingWindow_ != nullptr) {
        ANativeWindow_release(pendingWindow_);
        pendingWindow_ = nullptr;
    }
    surfaceAvailable_ = false;
    hostCapabilities_.hasSurface = false;
    hostCapabilities_.interactiveRegion = {};
    condition_.notify_all();
}

void Renderer::touch(int action, int pointerId, float x, float y, float pressure, float size,
                     int toolType, double timeSeconds) {
    input_.submit(static_cast<TouchAction>(action), pointerId, x, y, pressure, size, toolType, timeSeconds);
}

void Renderer::textInput(const char* text) {
    if (text == nullptr) {
        return;
    }
    std::lock_guard lock(mutex_);
    pendingText_ += text;
}

void Renderer::deleteBackward() {
    std::lock_guard lock(mutex_);
    ++pendingBackspaces_;
}

void Renderer::setImeInsetRect(int left, int top, int right, int bottom) {
    std::lock_guard lock(mutex_);
    pendingImeLeft_ = left;
    pendingImeTop_ = top;
    pendingImeRight_ = right;
    pendingImeBottom_ = bottom;
}

void Renderer::requestTextInputEnd() {
    std::lock_guard lock(mutex_);
    textInputEndRequested_ = true;
}

void Renderer::setImeText(const char* text, int selectionStartUtf16, int selectionEndUtf16) {
    std::lock_guard lock(mutex_);
    pendingImeText_ = text != nullptr ? text : "";
    pendingImeSelStart_ = selectionStartUtf16;
    pendingImeSelEnd_ = selectionEndUtf16;
    pendingImeTextSet_ = true;
}

std::string Renderer::textInputText() {
    std::lock_guard lock(mutex_);
    return imeTextSnapshot_;
}

void Renderer::projectionFrame(const std::uint8_t* rgba, std::size_t byteCount,
                               int width, int height) {
    if (rgba == nullptr || width <= 0 || height <= 0 || width > 4096 || height > 4096 ||
        byteCount != static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4u) {
        return;
    }
    std::lock_guard lock(mutex_);
    if (pendingProjectionHardwareBuffer_ != nullptr) {
        AHardwareBuffer_release(pendingProjectionHardwareBuffer_);
        pendingProjectionHardwareBuffer_ = nullptr;
    }
    if (pendingProjectionReadComplete_) std::exchange(pendingProjectionReadComplete_, {})();
    pendingProjectionRgba_.assign(rgba, rgba + byteCount);
    pendingProjectionWidth_ = width;
    pendingProjectionHeight_ = height;
    projectionFramePending_ = true;
    condition_.notify_all();
}

void Renderer::projectionFrame(std::vector<std::uint8_t>&& rgba, int width, int height) {
    if (width <= 0 || height <= 0 || width > 4096 || height > 4096 ||
        rgba.size() != static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4u) {
        return;
    }
    std::lock_guard lock(mutex_);
    if (pendingProjectionHardwareBuffer_ != nullptr) {
        AHardwareBuffer_release(pendingProjectionHardwareBuffer_);
        pendingProjectionHardwareBuffer_ = nullptr;
    }
    if (pendingProjectionReadComplete_) std::exchange(pendingProjectionReadComplete_, {})();
    pendingProjectionRgba_ = std::move(rgba);
    pendingProjectionWidth_ = width;
    pendingProjectionHeight_ = height;
    projectionFramePending_ = true;
    condition_.notify_all();
}

void Renderer::projectionHardwareBuffer(AHardwareBuffer* buffer,
                                         std::function<void()> onGpuReadComplete,
                                         std::int64_t sourceTimestampNs) {
    if (buffer == nullptr) {
        if (onGpuReadComplete) onGpuReadComplete();
        return;
    }
    AHardwareBuffer_acquire(buffer);
    AHardwareBuffer* replacedBuffer = nullptr;
    std::function<void()> replacedReadComplete;
    {
        std::lock_guard lock(mutex_);
        replacedBuffer = pendingProjectionHardwareBuffer_;
        replacedReadComplete = std::exchange(pendingProjectionReadComplete_, {});
        pendingProjectionHardwareBuffer_ = buffer;
        pendingProjectionReadComplete_ = std::move(onGpuReadComplete);
        pendingProjectionSourceTimestampNs_ = sourceTimestampNs;
        pendingProjectionRgba_.clear();
        projectionFramePending_ = false;
    }
    if (replacedBuffer != nullptr) {
        AHardwareBuffer_release(replacedBuffer);
        replacedPendingFrames_.fetch_add(1, std::memory_order_relaxed);
    }
    // Returning an ImageReader lease may send on a socket. Never hold the
    // renderer mutex across it: the render thread must still consume the newest
    // frame, and a producer callback may itself submit a replacement.
    if (replacedReadComplete) replacedReadComplete();
    condition_.notify_all();
}

ProjectionDiagnostics Renderer::projectionDiagnostics() {
    std::lock_guard lock(mutex_);
    return projectionDiagnostics_;
}

void Renderer::projectionSourceAlive() {
    std::lock_guard lock(mutex_);
    projectionSourceHeartbeatPending_ = true;
    condition_.notify_all();
}

void Renderer::requestFrameDump(std::string path) {
    std::lock_guard lock(mutex_);
    pendingFrameDumpPath_ = std::move(path);
}

void Renderer::pause() {
    input_.cancelAll(0.0);
    // 同上：暂停跨越了输入上下文，等待中的抬起作废。
    g_buttonSequencer.reset();
    std::lock_guard lock(mutex_);
    paused_ = true;
    condition_.notify_all();
}

void Renderer::resume() {
    std::lock_guard lock(mutex_);
    paused_ = false;
    condition_.notify_all();
}

void Renderer::destroyGraphics() {
    if (activeWindow_ != nullptr) {
        ANativeWindow_release(activeWindow_);
        activeWindow_ = nullptr;
    }
}

void Renderer::run() {
    // -----------------------------------------------------------------------
    //  调度：一次性初始化（命名 + 归零），此后**按场景动态调整**
    //
    //  ★ 为什么不是"上来就把优先级拉满、绑死 prime 核"：
    //    这个覆盖层不是手机上唯一在跑的东西 —— 用户会在**游戏**里开它、刷视频时开它、
    //    也让它在桌面挂着。骁龙 8 Elite 只有 2 个 prime 核（cpu6-7），相机/游戏/系统 UI
    //    同样在抢。死绑那 2 个核 = 把调度器的手绑起来，谁都没好处；固定要高性能档
    //    在静止/视频场景是纯浪费电。
    //    ⇒ 默认**不绑核**（EAS 自己决定），只按"我们是否需要 + 系统是否繁忙"升降档，
    //      并且在系统繁忙或别人也要高刷时**主动让路**。策略本体在
    //      ThreadTuning.h 的 ComputeProfile()（纯逻辑，有离线回归）。
    // -----------------------------------------------------------------------
    aurora::tuning::TuningProfile schedProfile{};
    std::chrono::steady_clock::time_point schedLastApply = std::chrono::steady_clock::now();
    std::chrono::steady_clock::time_point vulkanWatchdogStart = std::chrono::steady_clock::now();
    bool vulkanWatchdogDone = false;
    aurora::tuning::CpuTimes schedCpuPrev{};
    int schedBusyPercent = 0;
    bool schedEnabled = true;
    bool schedAllowPin = false;
    {
        auto envInt = [](const char* name, int fallback) {
            const char* raw = std::getenv(name);
            if (raw == nullptr || raw[0] == '\0') return fallback;
            return std::atoi(raw);
        };
        schedEnabled = envInt("AURORA_SCHED_TUNING", 1) != 0;
        // ★ 绑核必须是显式选择，不是默认行为（见上面的理由）。
        schedAllowPin = envInt("AURORA_SCHED_PIN", 0) != 0;
        if (schedEnabled) {
            const aurora::tuning::TuningResult init = aurora::tuning::Initialize("aurora-render");
            schedCpuPrev = aurora::tuning::ReadCpuTimes();
            __android_log_print(ANDROID_LOG_INFO, "Aurora",
                                "调度: soc=%s 拓扑=%s 初始化=[%s] 绑核=%s",
                                init.soc.empty() ? "未知" : init.soc.c_str(),
                                init.topology.empty() ? "未知" : init.topology.c_str(),
                                init.actions.c_str(), schedAllowPin ? "显式开启" : "关闭(默认)");
        } else {
            __android_log_print(ANDROID_LOG_INFO, "Aurora", "调度: 已按 AURORA_SCHED_TUNING=0 关闭");
        }
    }
    auto nextDeadline = std::chrono::steady_clock::now();
    float appliedFrameRate = 0.0f;
    while (true) {
        ANativeWindow* nextWindow = nullptr;
        bool shouldRecreate = false;
        bool canRender = false;
        int nextWidth = 1;
        int nextHeight = 1;
        bool nextPaused = false;
        {
            std::unique_lock lock(mutex_);
            condition_.wait_for(lock, std::chrono::milliseconds(8), [this] {
                return stopping_ || surfaceAvailable_ != (activeWindow_ != nullptr) ||
                       (surfaceAvailable_ && (pendingWindow_ != activeWindow_ || surfaceGeneration_ != activeGeneration_)) ||
                       (!paused_ && surfaceAvailable_ && activeWindow_ != nullptr);
            });
            if (stopping_) {
                if (pendingProjectionHardwareBuffer_ != nullptr) {
                    AHardwareBuffer_release(pendingProjectionHardwareBuffer_);
                    pendingProjectionHardwareBuffer_ = nullptr;
                }
                if (pendingProjectionReadComplete_) std::exchange(pendingProjectionReadComplete_, {})();
                ShutdownLiquidGlass();
                backend_.shutdown();
                vulkanBackend_.shutdown();
                destroyGraphics();
                return;
            }
            shouldRecreate = surfaceAvailable_ != (activeWindow_ != nullptr) ||
                             (surfaceAvailable_ && (pendingWindow_ != activeWindow_ || surfaceGeneration_ != activeGeneration_));
            canRender = !paused_ && activeWindow_ != nullptr;
            nextWidth = width_;
            nextHeight = height_;
            nextPaused = paused_;
            if (shouldRecreate) {
                nextWindow = surfaceAvailable_ ? pendingWindow_ : nullptr;
                if (nextWindow != nullptr) {
                    ANativeWindow_acquire(nextWindow);
                }
            }
        }

        if (shouldRecreate) {
            ShutdownLiquidGlass();
            backend_.shutdown();
            vulkanBackend_.shutdown();
            destroyGraphics();
            appliedFrameRate = 0.0f;
            lastFrameTime_ = {};
            perfWindow_ = {};
            renderDamage_.invalidate();
            {
                std::lock_guard lock(mutex_);
                activeWindow_ = nextWindow;
                activeGeneration_ = surfaceAvailable_ ? surfaceGeneration_ : 0;
            }
            if (activeWindow_ != nullptr) {
                const BackendKind requestedKind = requestedBackendKind_.load(std::memory_order_acquire);
                bool initialized = activeBackend().initialize(activeWindow_, nextWidth, nextHeight);
                if (!initialized && requestedKind == BackendKind::Vulkan) {
                    const BackendDescriptor vulkanFailure = vulkanBackend_.descriptor();
                    requestedBackendKind_.store(BackendKind::Gles3, std::memory_order_release);
                    initialized = backend_.initialize(activeWindow_, nextWidth, nextHeight);
                    vulkanProbe_ = vulkanFailure;
                } else if (requestedKind == BackendKind::Vulkan) {
                    vulkanProbe_ = vulkanBackend_.descriptor();
                }
                if (initialized) {
                    ui_.resetForGraphicsContext();
                    InitializeLiquidGlass();
                } else {
                    graphicsFailed_.store(true, std::memory_order_release);
                }
            }
            canRender = !nextPaused && activeWindow_ != nullptr;
        }

        if (activeBackend().ready() && canRender) {
            const float requested = targetFrameRate_.load(std::memory_order_acquire);
            // ★ 出帧率就是宿主要求的帧率，**不做"跟随屏幕"的降频**。
            //   这里曾经用"我们自己上报的 present fps"去判断屏幕刷新率并降频，那是错的：
            //   present fps 是我们的输出，不是屏幕的刷新率。于是形成正反馈死锁——
            //   渲染 60 帧 → 判定"屏幕只有 60Hz" → 把出帧率限制到 60 → 永远回不到 120。
            //   实测症状就是"有时候会锁 60Hz"，而 `dumpsys` 里 activeMode 明明是 120Hz。
            //   宁可屏幕真在 60Hz 时多出一些不会被显示的帧（只是浪费一点 GPU），
            //   也不能把自己的帧率锁死。
            const float target = requested;
            if (target != appliedFrameRate) {
                activeBackend().setTargetFrameRate(target);
                appliedFrameRate = target;
                nextDeadline = std::chrono::steady_clock::now();
            }
            const float safeTarget = target > 0.5f ? target : 60.0f;
            const auto interval = std::chrono::duration_cast<std::chrono::steady_clock::duration>(
                std::chrono::duration<double>(1.0 / safeTarget));
            const bool earlyRender = EarlyRenderEnabled();
            {
                std::unique_lock lock(mutex_);
                // ★ 会话有效性只看**宿主请求的**帧率，不看被屏幕限制后的有效帧率：
                //   否则屏幕一降频就会被误判成"宿主改了帧率"，把会话反复重建。
                const auto sessionInvalid = [this, requested] {
                    return stopping_ || paused_ || !surfaceAvailable_ ||
                           surfaceGeneration_ != activeGeneration_ ||
                           targetFrameRate_.load(std::memory_order_acquire) != requested;
                };
                if (earlyRender) {
                    // 先睡到"最早允许开画"的时刻；这 1/4 个周期内到了新背景帧就提前开画。
                    // ★ 这里**只**决定"能多早开画"，不设"最小间隔"—— 帧率的长期平均
                    //   由下面 nextDeadline 的绝对网格保证（见那一段的注释）。
                    condition_.wait_until(lock, nextDeadline - interval / 4, sessionInvalid);
                    if (sessionInvalid()) continue;
                    if (std::chrono::steady_clock::now() < nextDeadline) {
                        condition_.wait_until(lock, nextDeadline, [&] {
                            return sessionInvalid() ||
                                   pendingProjectionHardwareBuffer_ != nullptr;
                        });
                        if (sessionInvalid()) continue;
                    }
                } else {
                    condition_.wait_until(lock, nextDeadline, sessionInvalid);
                    if (sessionInvalid()) continue;
                }
            }
            renderFrame();
            // ★★ 帧率靠**绝对时间网格**保证，而不是靠"两次开画的最小间隔"：
            //
            //   网格每轮固定 +interval；早开画只是"这一轮提前，下一轮照旧回网格"，
            //   所以长期平均严格 = target，而单帧可以提前最多 interval/4 用上刚到的
            //   新背景帧。两者兼得。
            //
            //   踩过的两个坑（都在真机上量到过，不是推测）：
            //   ① 早开画时写 `nextDeadline = frameStart + interval` ⇒ 网格跟着提前
            //      一起往前跑，提前被累积，render 实测 123~129fps（白烧 GPU/电量）；
            //   ② 反过来加"两次开画最小间隔 = interval"的硬闸 ⇒ 提前被完全禁掉，
            //      submit 没变但背景 lag 从 0.80 帧涨到 1.14 帧（延迟白丢）。
            nextDeadline += interval;
            {
                const auto finished = std::chrono::steady_clock::now();
                // 网格落后（掉帧/被抢占）才重锚，且不允许锚到过去。
                if (nextDeadline < finished) {
                    nextDeadline = finished;
                }
            }
        }

        // ---- Vulkan 看门狗：黑屏兜底 ----
        // 实测：Vulkan 后端能完成初始化、能持续跑渲染循环（render=120），但
        // presentedFramesPerSecond 恒为 0 —— **一帧都没真正提交过**，屏幕全黑。
        // 这跟"初始化失败"是两回事，所以上面 !initialized 那条回退兜不住它。
        // 后果极坏：图层仍占着触摸区，用户看不到界面也点不到东西，只能强杀进程。
        // 所以：选了 Vulkan 却到 4 秒还没有任何帧被 present，就强制退回 GLES，
        // 并顺便把 surface 换代，让它用 GLES 重新初始化（只改 kind 不重建是没用的）。
        if (!vulkanWatchdogDone &&
            requestedBackendKind_.load(std::memory_order_acquire) == BackendKind::Vulkan) {
            if (std::chrono::steady_clock::now() - vulkanWatchdogStart >= std::chrono::seconds(4)) {
                vulkanWatchdogDone = true;
                float presentFps = 0.0f;
                {
                    std::lock_guard lock(mutex_);
                    presentFps = framePerfSnapshot_.presentedFramesPerSecond;
                }
                if (presentFps <= 0.5f) {
                    // 把后端的实时诊断带出来：state 与 diagnostic 记录了
                    // beginFrame/endFrame 里第一个失败点，没有它只能靠猜。
                    const BackendDescriptor live = vulkanBackend_.descriptor();
                    requestedBackendKind_.store(BackendKind::Gles3, std::memory_order_release);
                    {
                        std::lock_guard lock(mutex_);
                        ++surfaceGeneration_;   // 强制重建，走 GLES 初始化
                    }
                    appliedFrameRate = 0.0f;
                    condition_.notify_all();
                    __android_log_print(ANDROID_LOG_WARN, "Aurora",
                                        "Vulkan 看门狗: 4 秒内没有一帧被 present"
                                        "（present=%.1f）state=%d 诊断=[%s]，已自动退回 GLES",
                                        presentFps, static_cast<int>(live.state),
                                        live.diagnostic.c_str());
                }
            }
        }


        // ---- 场景自适应的调度档位：每秒重估一次 ----
        //   刻意放在帧循环里而不是只在启动时定一次：用户的场景是会变的
        //   （桌面 → 刷视频 → 进游戏），档位必须跟着变 —— 尤其是"让路"那一档，
        //   它是为了保护游戏/相机/系统 UI，不是在保护我们自己。
        if (schedEnabled) {
            const auto schedNow = std::chrono::steady_clock::now();
            if (schedNow - schedLastApply >= std::chrono::seconds(1)) {
                schedLastApply = schedNow;
                const aurora::tuning::CpuTimes cur = aurora::tuning::ReadCpuTimes();
                const int busy = aurora::tuning::BusyPercentDelta(schedCpuPrev, cur);
                if (busy >= 0) schedBusyPercent = busy;
                schedCpuPrev = cur;

                float presentFps = 0.0f;
                float uploadFps = 0.0f;
                {
                    std::lock_guard lock(mutex_);
                    presentFps = framePerfSnapshot_.presentedFramesPerSecond;
                    uploadFps = framePerfSnapshot_.uploadedFramesPerSecond;
                }
                aurora::tuning::SceneContext ctx;
                ctx.glassActive = canRender;
                ctx.targetFps = targetFrameRate_.load(std::memory_order_relaxed);
                // "在动"的两个来源：背景在更新（刷视频/滑动）或屏幕本身在高刷。
                ctx.interacting = uploadFps > 5.0f || presentFps > 90.0f;
                ctx.systemBusyPercent = schedBusyPercent;
                ctx.otherHighRefreshClient = false;   // 预留：将来可从 SF 的投票推断
                const aurora::tuning::ProfileDecision decision =
                    aurora::tuning::ApplySceneProfile(ctx, &schedProfile, schedAllowPin);
                if (decision.changed) {
                    __android_log_print(
                        ANDROID_LOG_INFO, "Aurora",
                        "调度档位: nice=%d uclamp=%d%% 绑核=%s 系统忙=%d%% 在动=%d 屏=%.0f 背景=%.0f -> %s",
                        decision.profile.nice, decision.profile.uclampPercent,
                        decision.profile.pinToPrime ? "是" : "否", schedBusyPercent,
                        ctx.interacting ? 1 : 0, presentFps, uploadFps,
                        decision.profile.reason);
                }
            }
        }
    }
}

void Renderer::renderFrame() {
    const auto frameStart = std::chrono::steady_clock::now();
    ImGuiIO& io = ImGui::GetIO();
    int width = 1;
    int height = 1;
    float density = 1.0f;
    std::uint64_t viewportGeneration = 0;
    bool resetPointer = false;
    std::string pendingText;
    int pendingBackspaces = 0;
    int imeLeft = 0, imeTop = 0, imeRight = 0, imeBottom = 0;
    bool textInputEnd = false;
    std::string imeTextToSet;
    int imeSelStart = -1, imeSelEnd = -1;
    bool imeTextToSetValid = false;
    std::vector<std::uint8_t> projectionRgba;
    int projectionWidth = 0;
    int projectionHeight = 0;
    AHardwareBuffer* projectionHardwareBuffer = nullptr;
    std::function<void()> projectionReadComplete;
    std::int64_t projectionSourceTimestampNs = 0;
    bool projectionSourceAlive = false;
    {
        std::lock_guard lock(mutex_);
        width = width_;
        height = height_;
        density = density_;
        viewportGeneration = viewportGeneration_;
        resetPointer = std::exchange(resetPointerForViewport_, false);
        pendingText.swap(pendingText_);
        pendingBackspaces = pendingBackspaces_;
        pendingBackspaces_ = 0;
        imeLeft = pendingImeLeft_;
        imeTop = pendingImeTop_;
        imeRight = pendingImeRight_;
        imeBottom = pendingImeBottom_;
        // 同一帧里宿主（DrawLiquidGlassShowcase）要通过 imeInsetRect() 读它做避让，
        // 所以在这里就落到成员上（渲染线程内，无并发问题）。
        imeLeft_ = imeLeft;
        imeTop_ = imeTop;
        imeRight_ = imeRight;
        imeBottom_ = imeBottom;
        textInputEnd = std::exchange(textInputEndRequested_, false);
        if (pendingImeTextSet_) {
            imeTextToSet = pendingImeText_;
            imeSelStart = pendingImeSelStart_;
            imeSelEnd = pendingImeSelEnd_;
            imeTextToSetValid = true;
            pendingImeTextSet_ = false;
        }
        if (projectionFramePending_) {
            projectionRgba.swap(pendingProjectionRgba_);
            projectionWidth = pendingProjectionWidth_;
            projectionHeight = pendingProjectionHeight_;
            projectionFramePending_ = false;
        }
        projectionHardwareBuffer = pendingProjectionHardwareBuffer_;
        pendingProjectionHardwareBuffer_ = nullptr;
        projectionReadComplete = std::exchange(pendingProjectionReadComplete_, {});
        projectionSourceTimestampNs = pendingProjectionSourceTimestampNs_;
        pendingProjectionSourceTimestampNs_ = 0;
        projectionSourceAlive = projectionSourceHeartbeatPending_;
        projectionSourceHeartbeatPending_ = false;
    }
    if (resetPointer) {
        g_buttonSequencer.reset();
        io.AddMouseButtonEvent(0, false);
        renderDamage_.invalidate();
    }
    const InputSnapshot input = input_.snapshot();
    const GestureFrame gesture = gestures_.update(input);
    for (int index = 0; index < pendingBackspaces; ++index) {
        if (LiquidGlassVisible()) {
            io.AddKeyEvent(ImGuiKey_Backspace, true);
            io.AddKeyEvent(ImGuiKey_Backspace, false);
        } else {
            ui_.deleteBackward();
        }
    }
    if (!pendingText.empty()) {
        if (LiquidGlassVisible()) {
            io.AddInputCharactersUTF8(pendingText.c_str());
        } else {
            ui_.appendText(pendingText.c_str());
        }
    }
    const float logicalWidth = static_cast<float>(width) / density;
    const float logicalHeight = static_cast<float>(height) / density;
    const bool liquidShowcase = LiquidGlassVisible();
    const float displayWidth = liquidShowcase ? static_cast<float>(width) : logicalWidth;
    const float displayHeight = liquidShowcase ? static_cast<float>(height) : logicalHeight;
    const float displayScale = liquidShowcase ? 1.0f : density;
    io.DisplaySize = ImVec2(displayWidth, displayHeight);
    io.DisplayFramebufferScale = ImVec2(displayScale, displayScale);
    const auto now = std::chrono::steady_clock::now();
    const float realDelta = lastFrameTime_ == std::chrono::steady_clock::time_point{}
        ? 1.0f / targetFrameRate_.load(std::memory_order_relaxed)
        : std::chrono::duration<float>(now - lastFrameTime_).count();
    const float delta = std::clamp(realDelta, 1.0f / 240.0f, 0.05f);
    lastFrameTime_ = now;
    frameStats_.tick(realDelta);
    // Rate/counter text is a monitor, not a glass animation. Update its displayed
    // reading four times a second; keep the real timing and spring step at the
    // full target rate so visible diagnostic text does not force 120 identical
    // background/blur recompositions a second on an otherwise static scene.
    if (displayedFrameStatsStamp_ == std::chrono::steady_clock::time_point{} ||
        now - displayedFrameStatsStamp_ >= std::chrono::milliseconds(250)) {
        displayedFrameStats_ = frameStats_;
        displayedFrameStatsStamp_ = now;
    }
    io.DeltaTime = delta;
    const ImVec2 mousePosition(input.primary.x / displayScale, input.primary.y / displayScale);
    // =========================================================================
    // 触摸 → ImGui 鼠标事件的投递
    //
    // ★ 关掉输入事件「涓流」。
    //   实测（真机，带时间戳对齐）：开启涓流时 io.MousePos 一直是对的，
    //   但 io.MouseDown[0] **恒为 false**；关掉后立刻变成 1。
    //
    //   为什么这很关键：ytbl 组件的按压判定走的是
    //     secondary_imgui.cpp:874  ImGui::InvisibleButton(...)
    //     secondary_imgui.cpp:877  probe.held = ImGui::IsItemActive()
    //   而 ButtonBehavior 读的就是 io.MouseDown[0]。所以 io.MouseDown[0] 恒 false
    //   ⇒ ytbl 的 Toggle/Slider/TabBar/PillButton 的 TrackMotion 永不 press()
    //   ⇒ AMDL 的 M-11/M-12/M-13/M-14/M-15 全部不可见。
    //
    //   ⚠️ 诚实记录：**具体是哪一条涓流规则挡住的，我没有定论。**
    //   一度以为是「每帧的位置事件把按键事件挤到队尾」，但读过 imgui.cpp:1607-1611
    //   后这个解释站不住 —— AddMousePosEvent 会过滤重复位置，静止按压时
    //   根本不会入队位置事件。触摸屏专有那条规则（imgui.cpp:9507）也不适用，
    //   因为 InputEventsNextMouseSource 默认就是 ImGuiMouseSource_Mouse
    //   （imgui_internal.h:2259）。结论：现象确凿、机制待查，
    //   设备有电时可用 io.ConfigInputTrickleEventQueue 的 A/B 对比 + 事件转储定位。
    //
    // ★★ 关掉涓流的**代价**必须一起补掉（imgui.cpp:9468 明确写着）：
    //     trickle=false 时「同一帧内的 down/up 会折叠成最终状态 ⇒ 点击丢失」。
    //     InputRouter 的两个边沿标志（InputRouter.cpp:58/84）在同一帧**可能同时为真**
    //     —— 帧卡顿期间完成一次快速点击就会这样；而本项目主循环里还有
    //     screencap + 降采样（需求②真实反射桌面），帧时间本来就会抖。
    //     所以这里**每帧最多投递一个按键事件、DOWN 优先、UP 顺延到下一帧**，
    //     从结构上保证 down 与 up 永不共享同一帧。
    // =========================================================================
    io.ConfigInputTrickleEventQueue = false;

    // ★★★ 只有**标题栏**能拖动窗口。
    //
    // 踩过的坑（真机日志铁证）：主题窗口的面板很长，要在里面上下滑动滚动 ——
    // 而 ImGui 默认 `ConfigWindowsMoveFromTitleBarOnly = false`，意味着
    // **点在窗口的任何空白处都能把窗口拖走**。于是用户想滚页面时，
    // 手指落在表格间隙/分组之间的空白上，整个窗口就跟着手指跑了。
    //
    // 取证：滑动 (400,1700)→(400,900)（位移 -800），`--dump` 的
    // `界面=(16,16)-(776,2130)` 变成 `界面=(16,-70)-(776,1330)` ——
    // 下边界 2130→1330 正好是 -800，窗口整体被拖走 800px，内容一点没滚。
    //
    // 打开这个开关后：标题栏拖动照旧（用户抓窗口的地方本来就是标题栏），
    // 内容区滑动 → 滚动页面。二级窗口不受影响（它是 `NoMove` + 自己的手势层）。
    io.ConfigWindowsMoveFromTitleBarOnly = true;

    // ★★★ 关掉 ImGui 的布局持久化（imgui.ini）—— 否则窗口位置会"漂"。
    //
    // 踩过的坑：两个窗口本来固定在 (16,16) 和 (16,730)，两个的并集正好是
    // 触摸拦截区 (0,0)-(800,2160) 的依据。但 ImGui 默认把窗口位置存进 ini，
    // 于是用户拖过之后，**下一次启动会恢复被拖走的位置** —— 而拦截区是固定的
    // ⇒ 窗口跑到框外的那部分**点不动**，而且抹除区域、窗口夹取全都对不上。
    //
    // 让每次启动都从代码里的设计位置开始（`ImGuiCond_FirstUseEver` 在没有
    // ini 记录时就等于每次都生效）。拖动在**本次会话内仍然完全自由** ——
    // 用户的诉求是"能动"，不是"位置要记住"。
    //
    // 注意：这不影响 `Renderer` 里的任何其它状态，只是不再读写 imgui.ini。
    io.IniFilename = nullptr;

    io.AddMousePosEvent(mousePosition.x, mousePosition.y);

    // 按键边沿定序器：g_buttonSequencer（文件作用域，见顶部注释）。
    // 本帧只发 DOWN，把同一帧到达的 UP 留到下一帧 —— 因为关掉 ImGui 涓流后
    // 同帧的 down+up 会折叠成「最终状态 = up」，点击就丢了（imgui.cpp:9468）。
    // 定序逻辑抽成了可单测的 ButtonEdgeSequencer
    // （tests/MouseEdgeSequencerTest.cpp，8 组断言，PC 上就能跑）——
    // 「偶尔点不中」这类顺序 bug 在真机上极难复现。
    switch (g_buttonSequencer.step(input.primaryPressed, input.primaryReleased)) {
        case ButtonEdgeSequencer::Out::Down:
            io.AddMouseButtonEvent(0, true);
            break;
        case ButtonEdgeSequencer::Out::Up:
            io.AddMouseButtonEvent(0, false);
            break;
        case ButtonEdgeSequencer::Out::None:
        default:
            break;
    }
    io.MousePos = mousePosition;

    IRenderBackend& rendererBackend = activeBackend();
    const auto prepareEnd = std::chrono::steady_clock::now();
    rendererBackend.resize(width, height);
    if (!rendererBackend.ready()) {
        if (projectionHardwareBuffer) AHardwareBuffer_release(projectionHardwareBuffer);
        if (projectionReadComplete) std::exchange(projectionReadComplete, {})();
        graphicsFailed_.store(true, std::memory_order_release);
        return;
    }
    if (projectionSourceAlive) rendererBackend.projectionSourceAlive();
    if (!projectionRgba.empty()) {
        rendererBackend.updateProjectionFrame(projectionRgba.data(), projectionRgba.size(),
                                              projectionWidth, projectionHeight);
    }
    BackendDescriptor backendDescriptor = rendererBackend.descriptor();
    const bool lateLatch = LateLatchEnabled() && backendDescriptor.kind == BackendKind::Gles3 &&
        backendDescriptor.hostSurfaceIsTransparent && backendDescriptor.sceneCompositorReady &&
        liquidShowcase;
    bool importedFreshFrame = false;
    if (!lateLatch && projectionHardwareBuffer != nullptr) {
        importedFreshFrame = rendererBackend.updateProjectionHardwareBuffer(projectionHardwareBuffer,
                                                        std::exchange(projectionReadComplete, {}),
                                                        projectionSourceTimestampNs);
        AHardwareBuffer_release(projectionHardwareBuffer);
        projectionHardwareBuffer = nullptr;
    }
    const auto uploadEnd = std::chrono::steady_clock::now();
    rendererBackend.beginFrame(delta);
    if (!lateLatch) backendDescriptor = rendererBackend.descriptor();
    BeginLiquidGlassFrame(delta, io.DisplaySize.x, io.DisplaySize.y, displayScale, input,
                          backendDescriptor);
    ImGui::NewFrame();
    // ★ 输入法收起事件 ⇒ 释放输入框焦点（本轮修的"键盘再也叫不回来"）。
    //   只在收到 HIDDEN 的那一帧做一次：ImGui 的 InputText 一旦 active，
    //   宿主看到的 wantsTextInput() 就恒为 true，上升沿永远不会再来。
    if (textInputEnd) {
        if (ImGui::GetIO().WantTextInput || LiquidGlassWantsTextInput()) {
            ImGui::ClearActiveID();
            ImGui::GetIO().WantTextInput = false;
        }
    }
    HostCapabilities capabilities;
    {
        std::lock_guard lock(mutex_);
        capabilities = hostCapabilities_;
    }
    capabilities.density = density;
    // ★ 键盘矩形交给界面层（同一帧、同一线程）：宿主据此让窗口避让。
    capabilities.imeLeft = imeLeft_;
    capabilities.imeTop = imeTop_;
    capabilities.imeRight = imeRight_;
    capabilities.imeBottom = imeBottom_;
    UpdateLiquidGlassDiagnostics(capabilities, backendDescriptor, vulkanProbe_, displayedFrameStats_,
                                 input, gesture,
                                 ui_.textInputRequested() || LiquidGlassWantsTextInput());
    if (!LiquidGlassVisible()) {
        ui_.draw(input, gesture, backendDescriptor, vulkanProbe_, capabilities,
                 displayedFrameStats_, delta, density, io.DisplaySize.x, io.DisplaySize.y);
    }
    if (LiquidGlassVisible()) {
        // ★ 输入法回传的整段文本 → **设置**输入框（镜像同步，见 Renderer::setImeText）。
        //   必须在二级窗口绘制之前调用，才能当帧生效（它只是往静态待应用槽里放数据，
        //   真正装进 ImGui 缓冲发生在 secondary_imgui 画输入框之前）。
        if (imeTextToSetValid) secondary_imgui::SetImeText(imeTextToSet.c_str(), imeSelStart, imeSelEnd);
    }
    DrawLiquidGlassShowcase();
    if (LiquidGlassVisible()) {
        // 把输入框的当前内容拷给主线程：输入法会话建立时要用它做起点（否则两边起点不同，
        // 之后每一次同步都在补这个差额 —— 就是用户看到的"莫名其妙多一个字"）。
        std::lock_guard lock(mutex_);
        imeTextSnapshot_ = secondary_imgui::GetImeText();
    }
    {
        std::lock_guard lock(mutex_);
        TouchRegion interactive = ui_.interactiveRegion();
        TouchRegion liquid = LiquidGlassRegion();
        if (liquid.right > liquid.left && liquid.bottom > liquid.top) {
            // The showcase uses physical-pixel ImGui coordinates; the host
            // contract stores logical coordinates before normalization.
            const float regionScale = LiquidGlassVisible() ? density : 1.0f;
            liquid.left /= regionScale;
            liquid.top /= regionScale;
            liquid.right /= regionScale;
            liquid.bottom /= regionScale;
            interactive.left = std::min(interactive.left, liquid.left);
            interactive.top = std::min(interactive.top, liquid.top);
            interactive.right = std::max(interactive.right, liquid.right);
            interactive.bottom = std::max(interactive.bottom, liquid.bottom);
        }
        hostCapabilities_.interactiveRegion = interactive;
    }
    ImGui::Render();
    std::vector<VisibleUiShape> frameTouchShapes =
        GatherVisibleTouchShapes(displayScale, imeLeft, imeTop, imeRight, imeBottom);
    // ★ 部分接管：把本帧可见面板的**包围盒并集**交给后端，只在那个矩形内铺世界底层。
    //   为什么值得这么做：整屏铺满时玻璃要从整屏纹理做模糊采样，实测 GPU 会从
    //   222MHz 顶到 520MHz（前台应用跟着抢不到 GPU，表现为"我们的窗口不卡、但系统
    //   界面掉帧"）。只铺面板所在的那块，采样面积和 GPU 负载一起降下来，而且面板外
    //   的区域根本不经过我们 —— 那部分既没有延迟也没有开销。
    //   代价：矩形边界上会出现一条时间线接缝（面板内晚一帧、面板外直出）。
    //   面板很少或很小时收益最大；面板铺满整屏时退化成原来的整屏接管。
    //   `AURORA_PARTIAL_TAKEOVER=0` 可退回整屏接管做对照。
    if (PartialTakeoverEnabled()) {
        float region[4] = {0.0f, 0.0f, 0.0f, 0.0f};
        if (!frameTouchShapes.empty()) {
            float minX = frameTouchShapes.front().left;
            float minY = frameTouchShapes.front().top;
            float maxX = frameTouchShapes.front().right;
            float maxY = frameTouchShapes.front().bottom;
            for (const VisibleUiShape& shape : frameTouchShapes) {
                minX = std::min(minX, shape.left);
                minY = std::min(minY, shape.top);
                maxX = std::max(maxX, shape.right);
                maxY = std::max(maxY, shape.bottom);
            }
            region[0] = minX;
            region[1] = minY;
            region[2] = maxX - minX;
            region[3] = maxY - minY;
            // 打印实际铺满区域，用来判断"部分接管到底省了多少"：
            // 面板很少时这里应该远小于整屏；如果接近整屏，说明 UI 本身铺满了。
            static int regionLogBudget = 2;
            if (regionLogBudget > 0) {
                --regionLogBudget;
                __android_log_print(ANDROID_LOG_INFO, "Aurora",
                                    "部分接管区域: %.0fx%.0f @ (%.0f,%.0f)，占整屏 %.0f%%"
                                    "（整屏 %dx%d，形状数 %zu）",
                                    region[2], region[3], region[0], region[1],
                                    (region[2] * region[3]) * 100.0f /
                                        std::max(1.0f, static_cast<float>(width_ * height_)),
                                    width_, height_, frameTouchShapes.size());
            }
        }
        rendererBackend.setWorldComposeRegion(region);
    } else {
        rendererBackend.setWorldComposeRegion(nullptr);
    }
    BackendKind selectedBackend = BackendKind::Gles3;
    {
        std::lock_guard lock(mutex_);
        selectedBackend = requestedBackendKind_.load(std::memory_order_acquire);
    }
    if (ui_.consumeVulkanProbeRequest() && selectedBackend == BackendKind::Gles3 && activeWindow_ != nullptr) {
        vulkanBackend_.shutdown();
        if (vulkanBackend_.initialize(activeWindow_, width, height)) {
            vulkanProbe_ = vulkanBackend_.descriptor();
        } else {
            vulkanProbe_ = vulkanBackend_.descriptor();
        }
        vulkanBackend_.shutdown();
    }
    wantsTextInput_.store(io.WantTextInput || ui_.textInputRequested()
                              || LiquidGlassWantsTextInput(),
                          std::memory_order_relaxed);

    const auto buildUiEnd = std::chrono::steady_clock::now();
    if (lateLatch) {
        AHardwareBuffer* latestBuffer = nullptr;
        std::function<void()> latestReadComplete;
        std::int64_t latestSourceTimestampNs = 0;
        bool latestSourceAlive = false;
        bool viewportChanged = false;
        {
            std::lock_guard lock(mutex_);
            viewportChanged = viewportGeneration != viewportGeneration_;
            if (!viewportChanged) {
            latestBuffer = std::exchange(pendingProjectionHardwareBuffer_, nullptr);
            latestReadComplete = std::exchange(pendingProjectionReadComplete_, {});
            latestSourceTimestampNs = std::exchange(pendingProjectionSourceTimestampNs_, 0);
            latestSourceAlive = std::exchange(projectionSourceHeartbeatPending_, false);
            }
        }
        if (viewportChanged) {
            // New-orientation pixels belong to a new viewport. Never late-latch
            // them into the CPU jobs which were built with the old dimensions.
            if (projectionHardwareBuffer) AHardwareBuffer_release(projectionHardwareBuffer);
            if (projectionReadComplete) std::exchange(projectionReadComplete, {})();
            renderDamage_.invalidate();
            return;
        }
        if (latestBuffer != nullptr) {
            if (projectionHardwareBuffer != nullptr) {
                AHardwareBuffer_release(projectionHardwareBuffer);
                replacedPendingFrames_.fetch_add(1, std::memory_order_relaxed);
            }
            // This frame has never been read by the GPU. Return its lease once,
            // outside mutex_: a completion can submit another source frame.
            if (projectionReadComplete) std::exchange(projectionReadComplete, {})();
            projectionHardwareBuffer = latestBuffer;
            projectionReadComplete = std::move(latestReadComplete);
            projectionSourceTimestampNs = latestSourceTimestampNs;
            ++lateLatchedFrames_;
        } else if (latestReadComplete) {
            latestReadComplete();
        }
        if (latestSourceAlive) rendererBackend.projectionSourceAlive();
        if (projectionHardwareBuffer != nullptr) {
            // CPU draw lists reference panel jobs, never the world-base texture
            // in this SceneComposite path. Import before the first draw callback;
            // submit() then sets the compositor's world base to this exact image.
            // Other backends/global texture bindings retain their early import.
            importedFreshFrame = rendererBackend.updateProjectionHardwareBuffer(
                projectionHardwareBuffer, std::exchange(projectionReadComplete, {}),
                projectionSourceTimestampNs);
            AHardwareBuffer_release(projectionHardwareBuffer);
            projectionHardwareBuffer = nullptr;
        }
        backendDescriptor = rendererBackend.descriptor();
        // Import may resize/replace the projection texture. Refresh only the
        // binding; BeginLiquidGlassFrame would advance springs and clear jobs.
        RefreshLateBackdrop(backendDescriptor, static_cast<int>(io.DisplaySize.x),
                            static_cast<int>(io.DisplaySize.y), liquidShowcase);
    }
    const auto lateUploadEnd = std::chrono::steady_clock::now();
    std::string dumpPath;
    {
        std::lock_guard lock(mutex_);
        dumpPath.swap(pendingFrameDumpPath_);
        projectionDiagnostics_.renderedFrames = frameStats_.frameCount;
        projectionDiagnostics_.uploadedFrames = backendDescriptor.projectionFrameCount;
        projectionDiagnostics_.uploadFailures = backendDescriptor.projectionUploadFailureCount;
        projectionDiagnostics_.sourceStale = backendDescriptor.projectionSourceStale;
    }
    pendingRenderDamage_.width = width;
    pendingRenderDamage_.height = height;
    pendingRenderDamage_.backgroundGeneration = backendDescriptor.projectionFrameCount;
    pendingRenderDamage_.bytes.clear();
    const bool completeDamage = selectedBackend == BackendKind::Gles3 &&
        backendDescriptor.hostSurfaceIsTransparent && LiquidGlassVisible() &&
        CaptureDrawDamage(ImGui::GetDrawData(), pendingRenderDamage_);
    AppendRenderDamageScalar(pendingRenderDamage_.bytes, backendDescriptor.backgroundSourceIsProjection);
    AppendRenderDamageScalar(pendingRenderDamage_.bytes, backendDescriptor.projectionSourceStale);
    const bool submitFrame = !completeDamage || !dumpPath.empty() ||
                             renderDamage_.changed(pendingRenderDamage_);
    const auto submittedBefore = rendererBackend.framePerfSnapshot().submittedFrames;
    if (submitFrame) rendererBackend.submit(ImGui::GetDrawData());
    const auto submitEnd = std::chrono::steady_clock::now();
    if (submitFrame) {
        if (selectedBackend == BackendKind::Gles3) DumpFrame(dumpPath, width, height);
        rendererBackend.endFrame();
        if (completeDamage && rendererBackend.framePerfSnapshot().submittedFrames > submittedBefore) {
            renderDamage_.commit(pendingRenderDamage_);
        } else {
            renderDamage_.invalidate();
        }
    } else {
        ++idleSkippedFrames_;
    }
    const auto frameEnd = std::chrono::steady_clock::now();
    const auto milliseconds = [](auto end, auto start) {
        return std::chrono::duration<double, std::milli>(end - start).count();
    };
    const BackendFramePerf backendPerf = rendererBackend.framePerfSnapshot();
    const std::int64_t renderedAtNs = std::chrono::duration_cast<std::chrono::nanoseconds>(
        submitEnd.time_since_epoch()).count();
    // Only fresh-source frames diagnose transport/render delay. Damage-driven
    // mirrors deliberately keep a static image and its old source timestamp.
    if (importedFreshFrame && backendPerf.sourceTimestampValid &&
        renderedAtNs >= backendPerf.sourceTimestampNanoseconds) {
        perfWindow_.sourceAge += (renderedAtNs - backendPerf.sourceTimestampNanoseconds) * 1.0e-6;
        ++perfWindow_.sourceAgeSamples;
    }
    if (perfWindow_.stamp == std::chrono::steady_clock::time_point{}) {
        perfWindow_.stamp = frameStart;
        perfWindow_.uploadedStart = backendDescriptor.projectionFrameCount;
        perfWindow_.backendStart = backendPerf;
    }
    ++perfWindow_.frames;
    perfWindow_.prepare += milliseconds(prepareEnd, frameStart);
    perfWindow_.upload += milliseconds(uploadEnd, prepareEnd) +
                          milliseconds(lateUploadEnd, buildUiEnd);
    perfWindow_.buildUi += milliseconds(buildUiEnd, uploadEnd);
    perfWindow_.submit += milliseconds(submitEnd, lateUploadEnd);
    perfWindow_.swap += milliseconds(frameEnd, submitEnd);
    const double total = milliseconds(frameEnd, frameStart);
    perfWindow_.total += total;
    perfWindow_.worst = std::max(perfWindow_.worst, total);
    const double windowSeconds = std::chrono::duration<double>(frameEnd - perfWindow_.stamp).count();
    {
        // Publish only after this image was submitted to the display queue. An
        // earlier snapshot would intercept the new position while the previous
        // image was still displayed during a slow frame.
        std::lock_guard lock(mutex_);
        if (viewportGeneration == viewportGeneration_) visibleTouchShapes_ = std::move(frameTouchShapes);
        if (windowSeconds >= 1.0) {
            FramePerfSnapshot& snapshot = framePerfSnapshot_;
            const double count = static_cast<double>(perfWindow_.frames);
            snapshot.targetFramesPerSecond = targetFrameRate_.load(std::memory_order_relaxed);
            snapshot.sampleSeconds = windowSeconds;
            snapshot.renderFramesPerSecond = count / windowSeconds;
            snapshot.uploadedFramesPerSecond =
                (backendDescriptor.projectionFrameCount - perfWindow_.uploadedStart) / windowSeconds;
            const std::int64_t presentSpan = backendPerf.lastPresentNanoseconds -
                                              perfWindow_.backendStart.lastPresentNanoseconds;
            snapshot.presentedFramesPerSecond = presentSpan > 0 && perfWindow_.backendStart.lastPresentNanoseconds > 0
                ? (backendPerf.presentedFrames - perfWindow_.backendStart.presentedFrames) * 1.0e9 / presentSpan
                : 0.0;
            snapshot.prepareMilliseconds = perfWindow_.prepare / count;
            snapshot.uploadMilliseconds = perfWindow_.upload / count;
            snapshot.buildUiMilliseconds = perfWindow_.buildUi / count;
            snapshot.submitMilliseconds = perfWindow_.submit / count;
            snapshot.swapMilliseconds = perfWindow_.swap / count;
            snapshot.totalMilliseconds = perfWindow_.total / count;
            snapshot.worstFrameMilliseconds = perfWindow_.worst;
            snapshot.gpuMilliseconds = backendPerf.lastGpuMilliseconds;
            snapshot.submittedFrames = backendPerf.submittedFrames;
            snapshot.presentedFrames = backendPerf.presentedFrames;
            snapshot.importedFrames = backendDescriptor.projectionFrameCount;
            snapshot.replacedPendingFrames = replacedPendingFrames_.load(std::memory_order_relaxed);
            snapshot.lateLatchedFrames = lateLatchedFrames_;
            snapshot.importCacheHits = backendPerf.importCacheHits;
            snapshot.importCacheMisses = backendPerf.importCacheMisses;
            snapshot.presentTimestampsSupported = backendPerf.presentTimestampsSupported;
            snapshot.gpuTimerSupported = backendPerf.gpuTimerSupported;
            snapshot.frameRateVoteResult = backendPerf.frameRateVoteResult;
            snapshot.sourceTimestampNanoseconds = backendPerf.sourceTimestampNanoseconds;
            snapshot.sourceTimestampValid = backendPerf.sourceTimestampValid;
            snapshot.sourceToImportMilliseconds = backendPerf.sourceToImportMilliseconds;
            snapshot.sourceToRenderMilliseconds = perfWindow_.sourceAgeSamples > 0
                ? perfWindow_.sourceAge / perfWindow_.sourceAgeSamples : 0.0;
            snapshot.sourceToPresentMilliseconds = backendPerf.sourceToPresentMilliseconds;
            snapshot.sourceFrameLag = snapshot.sourceToRenderMilliseconds *
                snapshot.targetFramesPerSecond / 1000.0;
            snapshot.idleSkippedFrames = idleSkippedFrames_;
            snapshot.submittedFramesPerSecond =
                (backendPerf.submittedFrames - perfWindow_.backendStart.submittedFrames) / windowSeconds;
            perfWindow_ = {};
            perfWindow_.stamp = frameEnd;
            perfWindow_.uploadedStart = backendDescriptor.projectionFrameCount;
            perfWindow_.backendStart = backendPerf;
        }
    }
}

}  // namespace aurora
