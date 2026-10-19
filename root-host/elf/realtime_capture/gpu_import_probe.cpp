// Offscreen device smoke test of the production hardware-buffer import. It
// creates no SurfaceFlinger layer and never touches the user's physical screen.
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES3/gl3.h>
#include <android/hardware_buffer.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <dlfcn.h>
#include <functional>
#include <limits>
#include <memory>
#include <string>
#include <vector>

// The test supplies a pbuffer instead of the backend's normal ANativeWindow.
// GL import/copy/fence/resource cleanup all use the actual production methods.
#define private public
#include "GlesBackend.h"
#undef private

namespace {
constexpr std::uint8_t kTopLeft[4] = {255, 0, 0, 255};
constexpr std::uint8_t kTopRight[4] = {0, 255, 0, 255};
constexpr std::uint8_t kBottomLeft[4] = {0, 0, 255, 255};
constexpr std::uint8_t kBottomRight[4] = {255, 255, 0, 255};

const std::uint8_t* PixelFor(int x, int y, int width, int height) {
    if (y < height / 2) return x < width / 2 ? kTopLeft : kTopRight;
    return x < width / 2 ? kBottomLeft : kBottomRight;
}

AHardwareBuffer* MakePattern(int width, int height) {
    AHardwareBuffer_Desc description{};
    description.width = static_cast<std::uint32_t>(width);
    description.height = static_cast<std::uint32_t>(height);
    description.layers = 1;
    description.format = AHARDWAREBUFFER_FORMAT_R8G8B8A8_UNORM;
    description.usage = AHARDWAREBUFFER_USAGE_GPU_SAMPLED_IMAGE |
                        AHARDWAREBUFFER_USAGE_CPU_WRITE_OFTEN;
    AHardwareBuffer* buffer = nullptr;
    const int allocation = AHardwareBuffer_allocate(&description, &buffer);
    if (allocation != 0 || buffer == nullptr) {
        std::printf("FAIL AHB allocate=%d\n", allocation);
        return nullptr;
    }
    AHardwareBuffer_describe(buffer, &description);
    void* address = nullptr;
    const int lock = AHardwareBuffer_lock(buffer, AHARDWAREBUFFER_USAGE_CPU_WRITE_OFTEN,
                                          -1, nullptr, &address);
    if (lock != 0 || address == nullptr) {
        std::printf("FAIL AHB lock=%d\n", lock);
        AHardwareBuffer_release(buffer);
        return nullptr;
    }
    auto* pixels = static_cast<std::uint8_t*>(address);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const auto* color = PixelFor(x, y, width, height);
            const std::size_t offset = (static_cast<std::size_t>(y) * description.stride + x) * 4u;
            for (int channel = 0; channel < 4; ++channel) pixels[offset + channel] = color[channel];
        }
    }
    const int unlock = AHardwareBuffer_unlock(buffer, nullptr);
    if (unlock != 0) {
        std::printf("FAIL AHB unlock=%d\n", unlock);
        AHardwareBuffer_release(buffer);
        return nullptr;
    }
    return buffer;
}

bool StateWasRestored() {
    GLint viewport[4]{}, activeTexture = 0, framebuffer = 0;
    GLboolean mask[4]{};
    glGetIntegerv(GL_VIEWPORT, viewport);
    glGetIntegerv(GL_ACTIVE_TEXTURE, &activeTexture);
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &framebuffer);
    glGetBooleanv(GL_COLOR_WRITEMASK, mask);
    return viewport[0] == 1 && viewport[1] == 2 && viewport[2] == 3 && viewport[3] == 4 &&
           activeTexture == GL_TEXTURE3 && framebuffer == 0 &&
           mask[0] == GL_FALSE && mask[1] == GL_TRUE && mask[2] == GL_FALSE && mask[3] == GL_TRUE &&
           glIsEnabled(GL_BLEND) && glIsEnabled(GL_SCISSOR_TEST) &&
           glIsEnabled(GL_CULL_FACE) && glIsEnabled(GL_RASTERIZER_DISCARD);
}

void SetProbeState() {
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(1, 2, 3, 4);
    glActiveTexture(GL_TEXTURE3);
    glEnable(GL_BLEND);
    glEnable(GL_SCISSOR_TEST);
    glEnable(GL_CULL_FACE);
    glEnable(GL_RASTERIZER_DISCARD);
    glColorMask(GL_FALSE, GL_TRUE, GL_FALSE, GL_TRUE);
}

std::uint64_t BufferIdentity(AHardwareBuffer* buffer) {
    using GetId = int (*)(const AHardwareBuffer*, std::uint64_t*);
    static GetId getId = [] {
        void* library = dlopen("libandroid.so", RTLD_NOW | RTLD_LOCAL);
        return library ? reinterpret_cast<GetId>(dlsym(library, "AHardwareBuffer_getId")) : nullptr;
    }();
    std::uint64_t identity = 0;
    return getId && getId(buffer, &identity) == 0 ? identity : 0;
}

std::size_t PendingImports(const aurora::GlesBackend& backend) {
    return static_cast<std::size_t>(std::count_if(
        backend.projectionImports_.begin(), backend.projectionImports_.end(),
        [](const auto& entry) { return entry.completion != nullptr; }));
}

bool ImportCacheIsIdle(const aurora::GlesBackend& backend) {
    return PendingImports(backend) == 0 &&
        std::none_of(backend.projectionImports_.begin(), backend.projectionImports_.end(),
                     [](const auto& entry) { return static_cast<bool>(entry.onGpuReadComplete); });
}

bool CheckPixels(aurora::GlesBackend& backend, int frame, int width, int height) {
    glBindFramebuffer(GL_FRAMEBUFFER, backend.projectionImportFramebuffer_);
    std::vector<std::uint8_t> actual(static_cast<std::size_t>(width) * height * 4u);
    glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, actual.data());
    if (glGetError() != GL_NO_ERROR) {
        std::printf("FAIL readback frame=%d\n", frame);
        return false;
    }
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const auto* expected = PixelFor(x, height - 1 - y, width, height);
            const std::size_t offset = (static_cast<std::size_t>(y) * width + x) * 4u;
            for (int channel = 0; channel < 4; ++channel) {
                if (actual[offset + channel] != expected[channel]) {
                    std::printf("FAIL pixel frame=%d x=%d y=%d c=%d got=%u want=%u\n",
                                frame, x, y, channel, actual[offset + channel], expected[channel]);
                    return false;
                }
            }
        }
    }
    return true;
}
}

int main() {
    aurora::GlesBackend backend;
    backend.display_ = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (backend.display_ == EGL_NO_DISPLAY || !eglInitialize(backend.display_, nullptr, nullptr)) {
        std::printf("FAIL EGL initialize=%x\n", eglGetError());
        return 1;
    }
    const EGLint attributes[] = {EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT, EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8,
        EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8, EGL_NONE};
    EGLConfig config = nullptr;
    EGLint count = 0;
    if (!eglChooseConfig(backend.display_, attributes, &config, 1, &count) || count != 1) {
        std::printf("FAIL EGL config=%x\n", eglGetError());
        return 2;
    }
    const EGLint contextAttributes[] = {EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE};
    const EGLint surfaceAttributes[] = {EGL_WIDTH, 16, EGL_HEIGHT, 16, EGL_NONE};
    backend.context_ = eglCreateContext(backend.display_, config, EGL_NO_CONTEXT, contextAttributes);
    backend.surface_ = eglCreatePbufferSurface(backend.display_, config, surfaceAttributes);
    if (backend.context_ == EGL_NO_CONTEXT || backend.surface_ == EGL_NO_SURFACE ||
        !eglMakeCurrent(backend.display_, backend.surface_, backend.surface_, backend.context_)) {
        std::printf("FAIL EGL pbuffer=%x\n", eglGetError());
        return 3;
    }
    std::printf("GPU %s\n", reinterpret_cast<const char*>(glGetString(GL_RENDERER)));
    // Shared ownership keeps failure-path callbacks safe even if main returns
    // early and backend's destructor subsequently drains an outstanding fence.
    auto completions = std::make_shared<std::array<int, 128>>();
    const auto completion = [completions](int slot) {
        return [completions, slot] { ++(*completions)[static_cast<std::size_t>(slot)]; };
    };
    const auto callbackOnce = [completions](int slot) {
        const int count = (*completions)[static_cast<std::size_t>(slot)];
        if (count != 1) std::printf("FAIL callback slot=%d count=%d want=1\n", slot, count);
        return count == 1;
    };
    const auto drain = [&backend] {
        glFinish();
        backend.releaseProjectionImports(false);
        if (!ImportCacheIsIdle(backend) || backend.projectionImports_.size() > 8u) {
            std::printf("FAIL completed cache pending=%zu entries=%zu\n",
                        PendingImports(backend), backend.projectionImports_.size());
            return false;
        }
        return true;
    };
    struct BufferOwner {
        AHardwareBuffer* buffer = nullptr;
        ~BufferOwner() { if (buffer) AHardwareBuffer_release(buffer); }
    };
    const int widths[] = {2, 8, 10};
    const int heights[] = {2, 6, 4};
    BufferOwner pool[3];
    for (int index = 0; index < 3; ++index) {
        pool[index].buffer = MakePattern(widths[index], heights[index]);
        if (!pool[index].buffer) return 4;
        if (BufferIdentity(pool[index].buffer) == 0) {
            std::printf("FAIL stable AHB identity unavailable; cache probe needs Android 31+\n");
            return 4;
        }
    }
    const auto begin = std::chrono::steady_clock::now();
    for (int frame = 0; frame < 60; ++frame) {
        // Reuse allocation IDs while alternating dimensions. Reallocation of
        // the copy destination must never invalidate an import-cache hit.
        const int index = frame < 4 ? 0 : (frame - 4) % 3;
        const int width = widths[index];
        const int height = heights[index];
        SetProbeState();
        const bool uploaded = backend.updateProjectionHardwareBuffer(pool[index].buffer, completion(frame));
        if (!uploaded || !StateWasRestored()) {
            std::printf("FAIL upload/state frame=%d uploaded=%d errors=%u\n",
                        frame, uploaded ? 1 : 0, backend.projectionUploadFailureCount_);
            return 5;
        }
        if (!CheckPixels(backend, frame, width, height)) return 7;
        if (!drain() || !callbackOnce(frame)) return 8;
        // Calling reclamation repeatedly must not notify an old lease twice.
        backend.releaseProjectionImports(false);
        if (!callbackOnce(frame)) return 8;
    }
    if (backend.framePerf_.importCacheMisses != 3u || backend.framePerf_.importCacheHits != 57u ||
        backend.projectionImports_.size() != 3u) {
        std::printf("FAIL reuse cache hits=%llu misses=%llu entries=%zu\n",
                    static_cast<unsigned long long>(backend.framePerf_.importCacheHits),
                    static_cast<unsigned long long>(backend.framePerf_.importCacheMisses),
                    backend.projectionImports_.size());
        return 9;
    }

    // The caller drops its final allocation reference. The idle cache must
    // retain a real AHB reference, not just its pointer or EGLImage name.
    const std::uint64_t retainedIdentity = BufferIdentity(pool[0].buffer);
    AHardwareBuffer_release(pool[0].buffer);
    pool[0].buffer = nullptr;
    auto retained = std::find_if(backend.projectionImports_.begin(), backend.projectionImports_.end(),
        [retainedIdentity](const auto& entry) { return entry.identity == retainedIdentity; });
    if (retained == backend.projectionImports_.end() || !retained->buffer) {
        std::printf("FAIL cached allocation missing after caller release\n");
        return 10;
    }
    AHardwareBuffer_Desc retainedDescription{};
    AHardwareBuffer_describe(retained->buffer, &retainedDescription);
    SetProbeState();
    if (retainedDescription.width != 2u || retainedDescription.height != 2u ||
        !backend.updateProjectionHardwareBuffer(retained->buffer, completion(60)) ||
        !StateWasRestored() || !CheckPixels(backend, 60, 2, 2) || !drain() || !callbackOnce(60)) {
        std::printf("FAIL cache allocation lifetime\n");
        return 10;
    }

    // Exceed the eight-entry cache: completed LRU entries may be evicted, but
    // callbacks and allocation references must remain correct on every frame.
    for (int frame = 61; frame < 73; ++frame) {
        AHardwareBuffer* buffer = MakePattern(8, 6);
        if (!buffer) return 11;
        SetProbeState();
        const bool uploaded = backend.updateProjectionHardwareBuffer(buffer, completion(frame));
        AHardwareBuffer_release(buffer);
        if (!uploaded || !StateWasRestored() || !CheckPixels(backend, frame, 8, 6) ||
            !drain() || !callbackOnce(frame)) return 11;
    }
    if (backend.projectionImports_.size() != 8u) {
        std::printf("FAIL cache eviction entries=%zu want=8\n", backend.projectionImports_.size());
        return 11;
    }

    BufferOwner edgeCase;
    edgeCase.buffer = MakePattern(2, 2);
    if (!edgeCase.buffer) return 12;
    SetProbeState();
    const std::int64_t sourceTime = std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    if (!backend.updateProjectionHardwareBuffer(edgeCase.buffer, completion(73), sourceTime) ||
        !backend.framePerf_.sourceTimestampValid || !CheckPixels(backend, 73, 2, 2) ||
        !drain() || !callbackOnce(73)) return 12;
    SetProbeState();
    // A foreign/future time domain is not a valid measured source age. The
    // frame itself still imports and returns its lease exactly once.
    if (!backend.updateProjectionHardwareBuffer(edgeCase.buffer, completion(74),
                                                std::numeric_limits<std::int64_t>::max()) ||
        backend.framePerf_.sourceTimestampValid || !drain() || !callbackOnce(74)) {
        std::printf("FAIL mismatched source timestamp\n");
        return 12;
    }

    const std::uint32_t failuresBefore = backend.projectionUploadFailureCount_;
    if (backend.updateProjectionHardwareBuffer(nullptr, completion(75)) || !callbackOnce(75)) {
        std::printf("FAIL null buffer callback\n");
        return 13;
    }
    if (!eglMakeCurrent(backend.display_, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT)) return 13;
    const bool noContextAccepted = backend.updateProjectionHardwareBuffer(edgeCase.buffer, completion(76));
    if (!eglMakeCurrent(backend.display_, backend.surface_, backend.surface_, backend.context_)) return 13;
    if (noContextAccepted || !callbackOnce(76)) {
        std::printf("FAIL no-current-context callback\n");
        return 13;
    }

    // Deliberately give the copy pass an invalid program name to exercise cleanup
    // after resources and a cache entry have already been acquired.
    SetProbeState();
    const GLuint realProgram = backend.projectionImportProgram_;
    backend.projectionImportProgram_ = std::numeric_limits<GLuint>::max();
    const bool badCopyAccepted = backend.updateProjectionHardwareBuffer(edgeCase.buffer, completion(77));
    backend.projectionImportProgram_ = realProgram;
    if (badCopyAccepted || !StateWasRestored() || !callbackOnce(77) ||
        backend.projectionUploadFailureCount_ != failuresBefore + 3u || !drain()) {
        std::printf("FAIL invalid copy program callback/cleanup\n");
        return 13;
    }

    // Leave the successful frame's completion unpolled. shutdown must finish
    // the GPU, return this lease, destroy retained cache entries, and stay
    // idempotent if called again (as the destructor does).
    SetProbeState();
    if (!backend.updateProjectionHardwareBuffer(edgeCase.buffer, completion(78))) return 14;
    AHardwareBuffer_release(edgeCase.buffer);
    edgeCase.buffer = nullptr;
    const auto finalFrames = backend.projectionFrameCount_;
    const auto finalFailures = backend.projectionUploadFailureCount_;
    const auto finalHits = backend.framePerf_.importCacheHits;
    const auto finalMisses = backend.framePerf_.importCacheMisses;
    const auto cachedBeforeShutdown = backend.projectionImports_.size();
    backend.shutdown();
    if (!callbackOnce(78) || !backend.projectionImports_.empty() ||
        backend.projectionTexture_ != 0 || backend.context_ != EGL_NO_CONTEXT) {
        std::printf("FAIL shutdown completion/cache cleanup\n");
        return 14;
    }
    backend.shutdown();
    for (int slot = 0; slot <= 78; ++slot) if (!callbackOnce(slot)) return 14;
    const auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now() - begin).count();
    std::printf("PASS flip/full-coverage/alpha/state/resize/cache-reuse/cache-eviction/"
                "cache-lifetime/callback-once/timestamp-mismatch/shutdown frames=%u "
                "expectedFailures=%u callbacks=79 hits=%llu misses=%llu cachedBeforeShutdown=%zu "
                "pending=%zu avgProbeUs=%lld\n",
                finalFrames, finalFailures, static_cast<unsigned long long>(finalHits),
                static_cast<unsigned long long>(finalMisses), cachedBeforeShutdown,
                PendingImports(backend), static_cast<long long>(elapsed / finalFrames));
    return 0;
}
