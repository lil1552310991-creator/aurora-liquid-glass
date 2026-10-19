#pragma once

#include <android/native_window.h>
#include <android/hardware_buffer.h>

#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <atomic>
#include <string>
#include <thread>
#include <vector>

#include "AuroraUi.h"
#include "HostProtocol.h"
#include "InputRouter.h"
#include "GestureRouter.h"
#include "GlesBackend.h"
#include "VulkanBackend.h"
#include "RenderBackend.h"
#include "FrameStats.h"
#include "UiTouchGeometry.h"
#include "RenderDamage.h"

namespace aurora {

struct ProjectionDiagnostics {
    std::uint64_t renderedFrames = 0;
    std::uint32_t uploadedFrames = 0;
    std::uint32_t uploadFailures = 0;
    bool sourceStale = false;
};

struct FramePerfSnapshot final {
    float targetFramesPerSecond = 120.0f;
    double sampleSeconds = 0.0;
    double renderFramesPerSecond = 0.0;
    double uploadedFramesPerSecond = 0.0;
    double presentedFramesPerSecond = 0.0;
    double prepareMilliseconds = 0.0;
    double uploadMilliseconds = 0.0;
    double buildUiMilliseconds = 0.0;
    double submitMilliseconds = 0.0;
    double swapMilliseconds = 0.0;
    double totalMilliseconds = 0.0;
    double worstFrameMilliseconds = 0.0;
    double gpuMilliseconds = 0.0;
    std::uint64_t submittedFrames = 0;
    std::uint64_t presentedFrames = 0;
    std::uint64_t importedFrames = 0;
    std::uint64_t replacedPendingFrames = 0;
    std::uint64_t lateLatchedFrames = 0;
    std::uint64_t importCacheHits = 0;
    std::uint64_t importCacheMisses = 0;
    bool presentTimestampsSupported = false;
    bool gpuTimerSupported = false;
    int frameRateVoteResult = -1;
    std::int64_t sourceTimestampNanoseconds = 0;
    double sourceToImportMilliseconds = 0.0;
    double sourceToRenderMilliseconds = 0.0;
    double sourceToPresentMilliseconds = 0.0;
    double sourceFrameLag = 0.0;
    bool sourceTimestampValid = false;
    std::uint64_t idleSkippedFrames = 0;
    double submittedFramesPerSecond = 0.0;
};

class Renderer final {
public:
    Renderer();
    ~Renderer();

    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;

    void surfaceCreated(ANativeWindow* window);
    void setHostMode(HostMode mode);
    void setBackendKind(BackendKind kind);
    void setInitialTab(int tab);
    void setTargetFrameRate(float framesPerSecond);
    FramePerfSnapshot framePerfSnapshot();
    void setLiquidGlassVisible(bool visible);
    TouchRegion interactiveRegion();
    // Latest rendered frame's individual visible input surfaces, in physical
    // pixels. The native host uses their union instead of a bounding rectangle.
    std::vector<VisibleUiShape> visibleTouchShapes(int expectedWidth = 0, int expectedHeight = 0);
    void surfaceChanged(int width, int height, float density);
    void setSafeRegion(float left, float top, float right, float bottom);
    void surfaceDestroyed();
    void touch(int action, int pointerId, float x, float y, float pressure, float size,
               int toolType, double timeSeconds);
    void textInput(const char* text);
    void deleteBackward();
    /**
     * ★★ 系统输入法回传的「整段已提交文本 + 选区」⇒ **设置**输入框内容（镜像同步）。
     *
     * 为什么不能继续用"追加字符 + 退格键"：那是拿两份互不相干的文本状态去猜差异
     * （Java 的 EditText 是权威编辑对象，ImGui 的 InputText 是另一份缓冲），
     * 只要有一侧先动过（选字、粘贴、输入法一次上屏一整句），基准就永久错开，
     * 用户看到的就是"莫名其妙多一个字 / 少一个字 / 光标乱跳"。
     * 选区偏移用 **UTF-16 码元**（与 ImGui 的 ImWchar 同口径），<0 表示移到末尾。
     */
    void setImeText(const char* text, int selectionStartUtf16, int selectionEndUtf16);
    /** 输入框当前内容（输入法会话的初始文本，保证两边起点一致）。 */
    std::string textInputText();
    /**
     * ★★ 输入法弹层在屏幕上的矩形（物理像素；全 0 = 未知/已收起）。
     *
     * 我们的覆盖层是 SurfaceFlinger 里 mRoot 的兄弟层，z 序在 WMS 整棵子树（含
     * 输入法弹层）之上 ⇒ 落在键盘上的触摸会被我们这层先接走、键盘按键点不动。
     * 拿到矩形之后：① 触摸拦截区里把键盘那一块**挖掉**（见 GatherVisibleTouchShapes）；
     * ② 宿主可以据此让窗口避让（adjustPan 语义）。
     */
    void setImeInsetRect(int left, int top, int right, int bottom);
    /**
     * ★★ 请求"结束文字输入"：下一帧对处于激活态的输入框清一次焦点。
     *
     * 为什么需要它：输入法被收起之后，ImGui 的 InputText 往往仍然 active ⇒
     * 宿主看到的 `wantsTextInput()` 恒为 true ⇒ 拉起的**上升沿**永远不再出现，
     * 键盘就再也叫不回来了（本轮的真 bug）。收到 IME 的 HIDDEN 事件时调用一次，
     * 让状态真正回到"没在输入"。**只在事件上调用一次，不轮询、不强拉。**
     */
    void requestTextInputEnd();
    /**
     * 当前输入法弹层矩形（物理像素；全 0 = 未知/已收起）。
     * ★ 只允许渲染线程读（与 ImGui 同一线程），宿主用它做窗口避让。
     */
    void imeInsetRect(int out[4]) const {
        out[0] = imeLeft_;
        out[1] = imeTop_;
        out[2] = imeRight_;
        out[3] = imeBottom_;
    }
    void projectionFrame(const std::uint8_t* rgba, std::size_t byteCount, int width, int height);
    void projectionFrame(std::vector<std::uint8_t>&& rgba, int width, int height);
    // Borrowed input: acquire one reference for the latest pending frame.
    // Overwritten frames and frames consumed by the render thread are released.
    // Completion returns the producer's frame lease exactly once, after GPU
    // reads finish (or immediately if the pending frame is superseded).
    void projectionHardwareBuffer(AHardwareBuffer* buffer,
        std::function<void()> onGpuReadComplete = {}, std::int64_t sourceTimestampNs = 0);
    void projectionSourceAlive();
    ProjectionDiagnostics projectionDiagnostics();
    // Optional single-frame diagnostic, a 4x downsampled P6 PPM.
    void requestFrameDump(std::string path);
    bool wantsTextInput() const { return wantsTextInput_.load(std::memory_order_relaxed); }
    bool graphicsFailed() const { return graphicsFailed_.load(std::memory_order_acquire); }
    void setTextInputRequested(bool requested) { wantsTextInput_.store(requested, std::memory_order_relaxed); }
    void pause();
    void resume();

private:
    void run();
    void renderFrame();
    void destroyGraphics();
    IRenderBackend& activeBackend();
    const IRenderBackend& activeBackend() const;

    std::mutex mutex_;
    std::condition_variable condition_;
    std::thread thread_;
    bool stopping_ = false;
    bool paused_ = false;
    bool surfaceAvailable_ = false;
    std::uint64_t surfaceGeneration_ = 0;
    std::uint64_t activeGeneration_ = 0;
    ANativeWindow* pendingWindow_ = nullptr;
    ANativeWindow* activeWindow_ = nullptr;
    int width_ = 0;
    int height_ = 0;
    float density_ = 1.0f;
    std::uint64_t viewportGeneration_ = 0;
    bool resetPointerForViewport_ = false;
    InputRouter input_;
    GestureRouter gestures_;
    AuroraUi ui_;
    GlesBackend backend_;
    VulkanBackend vulkanBackend_;
    FrameStats frameStats_{};
    FrameStats displayedFrameStats_{};
    std::chrono::steady_clock::time_point displayedFrameStatsStamp_{};
    BackendDescriptor vulkanProbe_ = DescribeVulkanBackend();
    std::atomic<bool> wantsTextInput_{false};
    std::atomic<bool> graphicsFailed_{false};
    HostCapabilities hostCapabilities_{};
    std::atomic<BackendKind> requestedBackendKind_{BackendKind::Gles3};
    std::string pendingText_;
    int pendingBackspaces_ = 0;
    // ---- 输入法镜像同步：待设置的整段文本 + 选区（UTF-16 码元）----
    std::string pendingImeText_;
    int pendingImeSelStart_ = -1;
    int pendingImeSelEnd_ = -1;
    bool pendingImeTextSet_ = false;
    // 渲染线程每帧把输入框内容拷一份出来，供主线程（输入法会话建立时）读取
    std::string imeTextSnapshot_;
    // ---- 输入法弹层几何 / 焦点释放（见 setImeInsetRect / requestTextInputEnd）----
    int pendingImeLeft_ = 0;
    int pendingImeTop_ = 0;
    int pendingImeRight_ = 0;
    int pendingImeBottom_ = 0;
    bool textInputEndRequested_ = false;
    // 渲染线程这一帧真正用到的键盘矩形（0 = 未知）；GatherVisibleTouchShapes 用它挖洞
    int imeLeft_ = 0;
    int imeTop_ = 0;
    int imeRight_ = 0;
    int imeBottom_ = 0;
    std::vector<std::uint8_t> pendingProjectionRgba_;
    int pendingProjectionWidth_ = 0;
    int pendingProjectionHeight_ = 0;
    bool projectionFramePending_ = false;
    AHardwareBuffer* pendingProjectionHardwareBuffer_ = nullptr;
    std::function<void()> pendingProjectionReadComplete_;
    std::int64_t pendingProjectionSourceTimestampNs_ = 0;
    bool projectionSourceHeartbeatPending_ = false;
    std::vector<VisibleUiShape> visibleTouchShapes_;
    ProjectionDiagnostics projectionDiagnostics_;
    std::string pendingFrameDumpPath_;
    std::atomic<float> targetFrameRate_{120.0f};
    std::atomic<std::uint64_t> replacedPendingFrames_{0};
    std::uint64_t lateLatchedFrames_ = 0;
    FramePerfSnapshot framePerfSnapshot_{};
    RenderDamage renderDamage_;
    RenderDamageState pendingRenderDamage_;
    std::uint64_t idleSkippedFrames_ = 0;
    struct FramePerfWindow {
        std::chrono::steady_clock::time_point stamp{};
        std::uint64_t frames = 0;
        std::uint64_t uploadedStart = 0;
        BackendFramePerf backendStart{};
        double prepare = 0.0, upload = 0.0, buildUi = 0.0, submit = 0.0;
        double swap = 0.0, total = 0.0, worst = 0.0;
        double sourceAge = 0.0;
        std::uint64_t sourceAgeSamples = 0;
    } perfWindow_;
    std::chrono::steady_clock::time_point lastFrameTime_{};
};

}  // namespace aurora
