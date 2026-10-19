#pragma once

#include "RenderBackend.h"

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES3/gl3.h>

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace aurora {

class GlesBackend final : public IRenderBackend {
public:
    GlesBackend() = default;
    ~GlesBackend() override;

    GlesBackend(const GlesBackend&) = delete;
    GlesBackend& operator=(const GlesBackend&) = delete;

    BackendDescriptor descriptor() const override;
    bool ready() const override { return imguiReady_; }
    void setTargetFrameRate(float framesPerSecond) override;
    BackendFramePerf framePerfSnapshot() const override { return framePerf_; }
    bool initialize(ANativeWindow* window, int width, int height) override;
    void setTransparentSurface(bool transparent) {
        transparentSurface_.store(transparent, std::memory_order_release);
    }
    // 部分接管：只在给定矩形内铺世界底层（左上原点像素）。
    //   nullptr = 整屏（原行为）；面积为 0 = 本帧不铺。
    void setWorldComposeRegion(const float region[4]) override;
    void resize(int width, int height) override;
    void beginFrame(float deltaSeconds) override;
    void updateProjectionFrame(const std::uint8_t* rgba, std::size_t byteCount,
                               int width, int height) override;
    void projectionSourceAlive() override;
    bool updateProjectionHardwareBuffer(AHardwareBuffer* buffer,
        std::function<void()> onGpuReadComplete = {}, std::int64_t sourceTimestampNs = 0) override;
    void submit(ImDrawData* drawData) override;
    void endFrame() override;
    void shutdown() override;

private:
    struct ProjectionImport;
    bool createGraphics(ANativeWindow* window);
    bool createBackgroundResources();
    void destroyBackgroundResources();
    bool blurBackground();
    // 一次性诊断：把世界底层采集纹理落成 PPM（见 .cpp 里的说明）。
    void maybeDumpWorldBase();
    void destroyGraphics();
    // True while this object's EGL context is the one bound to the calling
    // thread, i.e. while GL object names may be created or deleted.
    bool hasCurrentContext() const;
    bool createNeutralBackdrop();
    void destroyNeutralBackdrop();
    void destroyProjectionTexture();
    bool createProjectionImportResources();
    void releaseProjectionImports(bool waitForCompletion);
    void destroyProjectionImportResources();
    void initializeFrameDiagnostics();
    void pollPresentTimestamps();
    void pollGpuTimers();
    void applyFrameRateVote();
    void destroyProjectionImport(ProjectionImport& import);
    // A projection frame stops being a valid backdrop after this long: the
    // producer may be revoked or crash without the Surface being recreated.
    bool projectionSourceIsStale() const;
    static bool projectionFrameIsValid(const std::uint8_t* rgba, std::size_t byteCount,
                                       int width, int height);

    EGLDisplay display_ = EGL_NO_DISPLAY;
    EGLSurface surface_ = EGL_NO_SURFACE;
    EGLContext context_ = EGL_NO_CONTEXT;
    int width_ = 1;
    int height_ = 1;
    bool imguiContextReady_ = false;
    bool imguiReady_ = false;
    ANativeWindow* nativeWindow_ = nullptr; // borrowed for this EGL surface's lifetime
    float targetFrameRate_ = 120.0f;
    BackendFramePerf framePerf_{};
    PFNEGLGETNEXTFRAMEIDANDROIDPROC getNextFrameId_ = nullptr;
    PFNEGLGETFRAMETIMESTAMPSANDROIDPROC getFrameTimestamps_ = nullptr;
    struct PendingPresentation {
        EGLuint64KHR frameId = 0;
        std::int64_t sourceTimestampNs = 0;
    };
    std::vector<PendingPresentation> pendingPresentFrameIds_;
    std::int64_t lastPresentedSourceTimestampNs_ = 0;
    struct GpuTimer { GLuint query = 0; bool pending = false; };
    GpuTimer gpuTimers_[4]{};
    int activeGpuTimer_ = -1;
    GLuint backgroundTexture_ = 0;
    GLuint blurTexture_ = 0;
    GLuint blurFramebuffer_ = 0;
    GLuint blurTexturePing_ = 0;
    GLuint blurFramebufferPing_ = 0;
    GLuint blurProgram_ = 0;
    bool backgroundReady_ = false;
    int backgroundWidth_ = 0;
    int backgroundHeight_ = 0;
    std::atomic<bool> transparentSurface_{false};
    // 部分接管的铺满区域（左上原点像素）。valid=false 表示整屏。
    float worldComposeRegion_[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    bool worldComposeRegionValid_ = false;
    // Projection (authorized MediaProjection) source. Its texture is sized by the
    // producer, independent of the Surface, so it survives resize() and is only
    // released with the GL context or when the producer goes silent.
    GLuint projectionTexture_ = 0;
    int projectionWidth_ = 0;
    int projectionHeight_ = 0;
    bool projectionReady_ = false;
    bool projectionFrameSeen_ = false;
    std::chrono::steady_clock::time_point projectionFrameStamp_{};
    std::uint32_t projectionFrameCount_ = 0;
    std::uint32_t projectionUploadFailureCount_ = 0;
    // Captures stay alive until the flip pass has finished reading them. The
    // target texture is ordinary 2D storage, preserving ytbl's existing API.
    struct ProjectionImport {
        AHardwareBuffer* buffer = nullptr;
        EGLImageKHR image = EGL_NO_IMAGE_KHR;
        GLuint texture = 0;
        GLsync completion = nullptr;
        std::uint64_t identity = 0;
        std::uint64_t lastUse = 0;
        std::function<void()> onGpuReadComplete;
    };
    std::vector<ProjectionImport> projectionImports_;
    GLuint projectionImportProgram_ = 0;
    GLuint projectionImportFramebuffer_ = 0;
    GLuint projectionImportVertexArray_ = 0;
    GLint projectionImportSourceUniform_ = -1;
    std::uint32_t projectionHardwareFrameCount_ = 0;
    // 1x1 transparent backdrop for translucent hosts without a projection.
    GLuint neutralBackdropTexture_ = 0;
};

}  // namespace aurora
