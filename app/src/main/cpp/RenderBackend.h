#pragma once

#include <cstdint>
#include <cstddef>
#include <string>
#include <functional>

struct ANativeWindow;
struct AHardwareBuffer;
struct ImDrawData;

namespace aurora {

enum class BackendKind : std::uint8_t {
    Gles3 = 0,
    Vulkan = 1,
};

enum class BackendState : std::uint8_t {
    Unavailable = 0,
    Ready = 1,
    SurfaceLost = 2,
    DeviceLost = 3,
    Probed = 4,
    SurfaceReady = 5,
    SwapchainReady = 6,
};

struct BackendDescriptor final {
    BackendKind kind = BackendKind::Gles3;
    BackendState state = BackendState::Unavailable;
    bool supportsExternalTexture = false;
    bool supportsBackgroundInput = false;
    bool backgroundSourceIsProjection = false;
    std::uintptr_t backgroundTextureHandle = 0;
    std::uint32_t apiVersion = 0;
    std::uint32_t physicalDeviceCount = 0;
    std::uint32_t surfaceFormatCount = 0;
    std::uint32_t presentModeCount = 0;
    bool supportsPresentation = false;
    std::uint32_t swapchainImageCount = 0;
    std::string diagnostic;
    const char* name = "unknown";
    // Raw host-scene texture and dimensions for materials that perform their own
    // regional capture/blur. backgroundTextureHandle may remain a processed input.
    std::uintptr_t rawBackgroundTextureHandle = 0;
    int backgroundTextureWidth = 0;
    int backgroundTextureHeight = 0;
    // True when the host window owns a translucent surface. Such a host never has
    // an in-framebuffer scene of its own: only an authorized projection stream
    // can legally feed the glass there, and sampling the host's own framebuffer
    // would feed the UI back into the glass.
    bool hostSurfaceIsTransparent = false;
    // Pixel space the backdrop texture must be addressed in, i.e. the width and
    // height handed to lgx::bindBackdropTexture. ytbl converts a host pixel
    // coordinate into a backdrop UV by dividing by these values
    // (ytbl/GlassPanel.cpp captureScene copies them into the job's sample
    // span/stride and ytbl/Shaders.cpp sceneUv performs the division), so they
    // describe the *host screen space*, not the texture's storage dimensions. A
    // downscaled projection (e.g. 720 px wide) therefore still stretches across
    // the whole surface instead of covering only its top-left corner.
    int backgroundMappingWidth = 0;
    int backgroundMappingHeight = 0;
    // 1x1 fully transparent texture offered by a transparent host while no
    // projection source is live. Binding it keeps ytbl inside its fallback
    // shading instead of capturing the glass host's own framebuffer.
    std::uintptr_t neutralBackdropTextureHandle = 0;
    // True when the host owns a Z-ordered scene compositor (see
    // GlesSceneCompositor.h). The glass then must NOT be handed one global
    // full-screen texture: each panel captures its own region out of the
    // framebuffer and the compositor stacks the world base underneath it.
    bool sceneCompositorReady = false;
    // Projection source diagnostics.
    std::uint32_t projectionFrameCount = 0;
    std::uint32_t projectionUploadFailureCount = 0;
    bool projectionSourceStale = false;
};

// Queue submissions and actual display presentations are deliberately separate.
// A successful eglSwapBuffers is not evidence that SurfaceFlinger displayed it.
struct BackendFramePerf final {
    std::uint64_t submittedFrames = 0;
    std::uint64_t presentedFrames = 0;
    std::uint64_t invalidPresentTimestamps = 0;
    std::int64_t lastPresentNanoseconds = 0;
    std::uint64_t importCacheHits = 0;
    std::uint64_t importCacheMisses = 0;
    double lastGpuMilliseconds = 0.0;
    bool presentTimestampsSupported = false;
    bool gpuTimerSupported = false;
    int frameRateVoteResult = -1;
    std::int64_t sourceTimestampNanoseconds = 0;
    double sourceToImportMilliseconds = 0.0;
    double sourceToPresentMilliseconds = 0.0;
    bool sourceTimestampValid = false;
    std::uint64_t sourcePresentAgeSamples = 0;
};

class IRenderBackend {
public:
    virtual ~IRenderBackend() = default;
    virtual BackendDescriptor descriptor() const = 0;
    virtual bool ready() const { return descriptor().state == BackendState::Ready; }
    virtual void setTargetFrameRate(float) {}
    virtual BackendFramePerf framePerfSnapshot() const { return {}; }
    // 部分接管：只在给定矩形内铺世界底层（左上原点像素）。
    //   nullptr = 整屏；面积为 0 = 本帧不铺。默认空实现（不支持的后端忽略）。
    virtual void setWorldComposeRegion(const float[4]) {}
    virtual bool initialize(ANativeWindow* window, int width, int height) = 0;
    virtual void resize(int width, int height) = 0;
    virtual void updateProjectionFrame(const std::uint8_t*, std::size_t, int, int) {}
    // An unchanged live mirror is healthy even when it has no new damage frame.
    virtual void projectionSourceAlive() {}
    // Borrowed framework capture buffer, with its producer fence already
    // signaled. Backends retain a reference until any queued GPU reads finish.
    virtual bool updateProjectionHardwareBuffer(AHardwareBuffer*, std::function<void()> onGpuReadComplete = {},
                                                std::int64_t = 0) {
        if (onGpuReadComplete) onGpuReadComplete();
        return false;
    }
    virtual void beginFrame(float deltaSeconds) = 0;
    virtual void submit(ImDrawData* drawData) = 0;
    virtual void endFrame() = 0;
    virtual void shutdown() = 0;
};

// ---------------------------------------------------------------------------
// Backdrop source selection
//
// The glass can sample four different things, and only one of them may be
// active per frame:
//   SceneComposite - ★ 默认且唯一正确的路径。宿主逐面板从帧缓冲抠出"此刻已经
//                绘制在其下方的全部内容"，再由宿主把"世界底层"（本图层之下的
//                屏幕内容）合成到它下面。它按构造满足 Z-order：采样发生在
//                该面板绘制之前，所以既不会采到自己，也不会采到更上层的玻璃；
//                而同帧更早绘制的内容（同窗口下层组件、更早的 ELF 窗口）一定
//                在帧缓冲里。此时 ytbl 侧不绑定任何全局纹理。
//   Projection - an authorized capture frame uploaded by GlesBackend. 单独使用它
//                当背景就是本次修复的**根因**：它是一张"屏幕最终长什么样"的
//                全屏图，玻璃自己已经盖住了它本该看到的下层内容。
//   Neutral    - a 1x1 transparent texture on a translucent host without a
//                projection. It exists so ytbl never falls back to capturing the
//                overlay's own framebuffer.
//   Offscreen  - the backend's raw copy of the opaque host framebuffer. It is
//                only bound while the showcase is hidden; when the showcase is
//                visible the original demo performs its own regional capture.
// The decision is a pure function so it is unit-checkable without GL (see the
// compile-time BackdropPlanSelfCheck below).
// ---------------------------------------------------------------------------

enum class BackdropOrigin : std::uint8_t {
    None = 0,
    Projection = 1,
    Neutral = 2,
    Offscreen = 3,
    SceneComposite = 4,
};

inline constexpr const char* BackdropOriginName(BackdropOrigin origin) {
    switch (origin) {
    case BackdropOrigin::Projection: return "projection";
    case BackdropOrigin::Neutral: return "neutral";
    case BackdropOrigin::Offscreen: return "offscreen";
    case BackdropOrigin::SceneComposite: return "scene";
    case BackdropOrigin::None: break;
    }
    return "none";
}

// Screen-pixel extent a backdrop is addressed in. Falls back to the storage
// extent when the host space is unknown, and never returns 0 so a division by
// the mapping size cannot produce an invalid UV.
inline constexpr int BackdropMappingExtent(int viewExtent, int storageExtent) {
    if (viewExtent > 0) return viewExtent;
    return storageExtent > 0 ? storageExtent : 1;
}

struct BackdropInputs final {
    bool glassVisible = false;
    bool transparentHost = false;
    bool projectionReady = false;
    std::uintptr_t projectionTexture = 0;
    int projectionWidth = 0;   // storage size, deliberately not the mapping size
    int projectionHeight = 0;
    std::uintptr_t offscreenTexture = 0;
    int offscreenWidth = 0;
    int offscreenHeight = 0;
    std::uintptr_t neutralTexture = 0;
    int viewWidth = 0;         // host screen space in pixels
    int viewHeight = 0;
    // ★ 宿主已经装好了逐面板 Z-order 合成器。它**压过**下面所有来源：
    //   此时绝不能绑全局纹理，否则 ytbl 会退回 captureScene() 的
    //   backdropReady() 分支，把整屏图当成每一块玻璃的背景。
    bool sceneComposite = false;
};

struct BackdropPlan final {
    BackdropOrigin origin = BackdropOrigin::None;
    std::uintptr_t texture = 0;
    int mappingWidth = 0;
    int mappingHeight = 0;
};

inline constexpr BackdropPlan PlanBackdrop(const BackdropInputs& inputs) {
    BackdropPlan plan;
    const int hostWidth = BackdropMappingExtent(inputs.viewWidth, inputs.projectionWidth);
    const int hostHeight = BackdropMappingExtent(inputs.viewHeight, inputs.projectionHeight);
    // ★ 逐面板场景合成优先。texture 故意保持 0：调用方据此 releaseBackdropTexture()，
    //   让 ytbl 回到"从当前帧缓冲抠区域"的原生路径 —— 那才是 Z 序正确的那条。
    if (inputs.sceneComposite) {
        plan.origin = BackdropOrigin::SceneComposite;
        plan.texture = 0;
        plan.mappingWidth = hostWidth;
        plan.mappingHeight = hostHeight;
        return plan;
    }
    if (inputs.projectionReady && inputs.projectionTexture != 0) {
        plan.origin = BackdropOrigin::Projection;
        plan.texture = inputs.projectionTexture;
        plan.mappingWidth = hostWidth;
        plan.mappingHeight = hostHeight;
        return plan;
    }
    // A translucent host can only sample a projection; anything else is the
    // overlay's own framebuffer, so hold the neutral texture instead.
    if (inputs.transparentHost && inputs.glassVisible && inputs.neutralTexture != 0) {
        plan.origin = BackdropOrigin::Neutral;
        plan.texture = inputs.neutralTexture;
        plan.mappingWidth = hostWidth;
        plan.mappingHeight = hostHeight;
        return plan;
    }
    // Only the hidden showcase binds the offscreen copy: the visible demo
    // captures the framebuffer itself through ytbl's canvas callbacks.
    if (!inputs.transparentHost && !inputs.glassVisible && inputs.offscreenTexture != 0) {
        plan.origin = BackdropOrigin::Offscreen;
        plan.texture = inputs.offscreenTexture;
        plan.mappingWidth = BackdropMappingExtent(inputs.viewWidth, inputs.offscreenWidth);
        plan.mappingHeight = BackdropMappingExtent(inputs.viewHeight, inputs.offscreenHeight);
        return plan;
    }
    return plan;
}

inline BackdropInputs BackdropInputsFor(const BackendDescriptor& descriptor, int viewWidth,
                                        int viewHeight, bool glassVisible) {
    BackdropInputs inputs;
    inputs.glassVisible = glassVisible;
    inputs.transparentHost = descriptor.hostSurfaceIsTransparent;
    inputs.neutralTexture = descriptor.neutralBackdropTextureHandle;
    inputs.viewWidth = descriptor.backgroundMappingWidth > 0 ? descriptor.backgroundMappingWidth
                                                            : viewWidth;
    inputs.viewHeight = descriptor.backgroundMappingHeight > 0 ? descriptor.backgroundMappingHeight
                                                              : viewHeight;
    inputs.sceneComposite = descriptor.sceneCompositorReady;
    if (descriptor.backgroundSourceIsProjection) {
        inputs.projectionReady = descriptor.rawBackgroundTextureHandle != 0;
        inputs.projectionTexture = descriptor.rawBackgroundTextureHandle;
        inputs.projectionWidth = descriptor.backgroundTextureWidth;
        inputs.projectionHeight = descriptor.backgroundTextureHeight;
    } else if (descriptor.supportsBackgroundInput && !descriptor.hostSurfaceIsTransparent) {
        inputs.offscreenTexture = descriptor.rawBackgroundTextureHandle;
        inputs.offscreenWidth = descriptor.backgroundTextureWidth;
        inputs.offscreenHeight = descriptor.backgroundTextureHeight;
    }
    return inputs;
}

// Cheap self-consistency check for the descriptor's backdrop fields. Returns
// nullptr when the descriptor is coherent, otherwise a static message suitable
// for BackendDescriptor::diagnostic.
inline const char* ValidateBackdropDescriptor(const BackendDescriptor& descriptor) {
    if (descriptor.backgroundSourceIsProjection && descriptor.neutralBackdropTextureHandle != 0) {
        return "neutral fallback must not coexist with a projection source";
    }
    if (descriptor.backgroundSourceIsProjection) {
        if (descriptor.rawBackgroundTextureHandle == 0) {
            return "projection source without texture";
        }
        if (descriptor.backgroundTextureHandle != descriptor.rawBackgroundTextureHandle) {
            return "projection raw/processed handles diverge";
        }
        if (descriptor.backgroundTextureWidth <= 0 || descriptor.backgroundTextureHeight <= 0) {
            return "projection texture dimensions missing";
        }
        if (!descriptor.supportsBackgroundInput) {
            return "projection source ignored as background input";
        }
    } else if (descriptor.supportsBackgroundInput) {
        if (descriptor.rawBackgroundTextureHandle == 0) {
            return "offscreen source without texture";
        }
        if (descriptor.backgroundTextureWidth <= 0 || descriptor.backgroundTextureHeight <= 0) {
            return "offscreen texture dimensions missing";
        }
    }
    if (descriptor.supportsBackgroundInput &&
        (descriptor.backgroundMappingWidth <= 0 || descriptor.backgroundMappingHeight <= 0)) {
        return "backdrop mapping space missing";
    }
    return nullptr;
}

namespace detail {

// Compile-time unit check for the decision matrix above. It runs on the host
// compiler as well as on the NDK, so a wiring regression fails the build instead
// of silently blanking the glass.
inline constexpr bool BackdropPlanSelfCheck() {
    BackdropInputs projection;
    projection.glassVisible = true;
    projection.transparentHost = true;
    projection.projectionReady = true;
    projection.projectionTexture = static_cast<std::uintptr_t>(0x1234u);
    projection.projectionWidth = 720;
    projection.projectionHeight = 1600;
    projection.neutralTexture = static_cast<std::uintptr_t>(0x22u);
    projection.viewWidth = 1080;
    projection.viewHeight = 2400;

    const BackdropPlan live = PlanBackdrop(projection);
    if (live.origin != BackdropOrigin::Projection) return false;
    if (live.texture != projection.projectionTexture) return false;
    // The 720x1600 projection must be addressed in host pixels, not in its own
    // storage space.
    if (live.mappingWidth != 1080 || live.mappingHeight != 2400) return false;

    BackdropInputs neutral = projection;
    neutral.projectionReady = false;
    neutral.projectionTexture = 0;
    const BackdropPlan fallback = PlanBackdrop(neutral);
    if (fallback.origin != BackdropOrigin::Neutral) return false;
    if (fallback.texture != projection.neutralTexture) return false;
    if (fallback.mappingWidth <= 0 || fallback.mappingHeight <= 0) return false;

    // Translucent host without projection and without a neutral texture must not
    // bind anything (the caller then releases the backdrop).
    BackdropInputs bare = neutral;
    bare.neutralTexture = 0;
    if (PlanBackdrop(bare).origin != BackdropOrigin::None) return false;

    // Opaque host with a visible showcase keeps the demo's own capture path.
    BackdropInputs opaque = projection;
    opaque.transparentHost = false;
    opaque.projectionReady = false;
    opaque.projectionTexture = 0;
    if (PlanBackdrop(opaque).origin != BackdropOrigin::None) return false;

    BackdropInputs hidden = opaque;
    hidden.glassVisible = false;
    hidden.offscreenTexture = static_cast<std::uintptr_t>(0x33u);
    hidden.offscreenWidth = 1080;
    hidden.offscreenHeight = 2400;
    const BackdropPlan offscreen = PlanBackdrop(hidden);
    if (offscreen.origin != BackdropOrigin::Offscreen) return false;
    if (offscreen.texture != hidden.offscreenTexture) return false;
    if (offscreen.mappingWidth != 1080 || offscreen.mappingHeight != 2400) return false;

    // Unknown host space must still yield a usable, non-zero mapping.
    BackdropInputs degenerate;
    degenerate.glassVisible = true;
    degenerate.transparentHost = true;
    degenerate.projectionReady = true;
    degenerate.projectionTexture = static_cast<std::uintptr_t>(0x1u);
    const BackdropPlan capped = PlanBackdrop(degenerate);
    if (capped.mappingWidth <= 0 || capped.mappingHeight <= 0) return false;

    // ★ 场景合成器就绪时必须压过其余全部来源，且 texture 必须为 0
    //   （调用方据此 releaseBackdropTexture()，让 ytbl 回到帧缓冲抠图路径）。
    BackdropInputs composed = projection;
    composed.sceneComposite = true;
    const BackdropPlan scene = PlanBackdrop(composed);
    if (scene.origin != BackdropOrigin::SceneComposite) return false;
    if (scene.texture != 0) return false;
    if (scene.mappingWidth != 1080 || scene.mappingHeight != 2400) return false;

    BackdropInputs composedHidden = hidden;
    composedHidden.sceneComposite = true;
    if (PlanBackdrop(composedHidden).origin != BackdropOrigin::SceneComposite) return false;

    // 宿主声明不支持时不得凭空进入场景合成分支。
    BackdropInputs unclaimed = projection;
    unclaimed.sceneComposite = false;
    if (PlanBackdrop(unclaimed).origin != BackdropOrigin::Projection) return false;

    if (BackdropOriginName(BackdropOrigin::None) == nullptr) return false;
    if (BackdropOriginName(BackdropOrigin::Projection) == nullptr) return false;
    if (BackdropOriginName(BackdropOrigin::Neutral) == nullptr) return false;
    if (BackdropOriginName(BackdropOrigin::Offscreen) == nullptr) return false;
    if (BackdropOriginName(BackdropOrigin::SceneComposite) == nullptr) return false;
    return BackdropMappingExtent(0, 0) >= 1;
}

inline constexpr bool kBackdropPlanContractHolds = BackdropPlanSelfCheck();

}  // namespace detail

static_assert(detail::kBackdropPlanContractHolds,
              "backdrop source decision matrix or texture-space contract drifted");

BackendDescriptor DescribeGles3Backend();
BackendDescriptor DescribeVulkanBackend();
BackendDescriptor ProbeVulkanBackend();

}  // namespace aurora
