#include "AuroraLog.h"
#include "GlesBackend.h"
#include <android/native_window.h>

#include "GlesSceneCompositor.h"

#include <android/log.h>
#include <android/hardware_buffer.h>
#include <GLES2/gl2ext.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cmath>
#include <cstring>
#include <cstdlib>
#include <dlfcn.h>
#include <string>
#include <utility>

#include "backends/imgui_impl_opengl3.h"
#include "imgui.h"

#if __has_include("third_party/liquidglass/font.h")
#include "third_party/liquidglass/font.h"
#define AURORA_HAS_LIQUIDGLASS_FONT 1
// UI 实际用到的字符表（tools/gen_ui_glyphs.py 生成）—— 图集必须把它们申请进去，
// 否则界面上会出现"问号"（见下面建字体那段的长注释）。
#include "UiGlyphSet.h"
#endif

namespace {
[[maybe_unused]] constexpr char kLogTag[] = "Aurora";
// A projection producer that stops delivering frames (user revoked the capture,
// the service died) must not keep feeding a frozen desktop into the glass.
constexpr std::chrono::milliseconds kProjectionStaleAfter{1500};
// Same bound the JNI entry point enforces; keeps a bogus producer from asking
// for an unbounded texture allocation.
constexpr int kProjectionMaxDimension = 4096;
constexpr std::size_t kProjectionMaxBytes =
    static_cast<std::size_t>(kProjectionMaxDimension) *
    static_cast<std::size_t>(kProjectionMaxDimension) * 4u;
constexpr std::size_t kProjectionMaxPendingImports = 4u;
constexpr std::size_t kProjectionMaxCachedImports = 8u;

std::int64_t MonotonicNanoseconds() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}

// ---------------------------------------------------------------------------
// 全屏接管开关（AURORA_FULLSCREEN_TAKEOVER=0 关闭）
//
// 默认开启：这是目前唯一能消除"玻璃窗口内外时间错位"的手段。
//   · 关闭（= 旧行为）：窗口外屏幕直出（0 延迟），窗口内是镜像抓来的帧（1 帧前）
//     ⇒ 运动物体跨过玻璃边界时两条时间线对不上 = 用户报的"撕裂感"。
//   · 开启：世界底层不透明铺满整屏，玻璃画在它上面 ⇒ 全屏同一条时间线，
//     延迟变成一致的 ~0.7 帧（120Hz 下 5.8ms），接缝消失。
// 与光学无关：用户已把光学全部调 0（纯透明板）验证过现象依旧。
// ---------------------------------------------------------------------------
bool FullScreenTakeoverEnabled() {
    static const bool enabled = [] {
        const char* raw = std::getenv("AURORA_FULLSCREEN_TAKEOVER");
        return !(raw != nullptr && raw[0] == '0' && raw[1] == '\0');
    }();
    return enabled;
}

std::uint64_t HardwareBufferIdentity(AHardwareBuffer* buffer) {
    using GetId = int (*)(const AHardwareBuffer*, std::uint64_t*);
    // getId arrived after the project's minimum NDK API. Resolve it at runtime
    // instead of raising the ELF's minimum supported Android version.
    static GetId getId = [] {
        void* library = dlopen("libandroid.so", RTLD_NOW | RTLD_LOCAL);
        return library ? reinterpret_cast<GetId>(dlsym(library, "AHardwareBuffer_getId")) : nullptr;
    }();
    std::uint64_t identity = 0;
    if (getId && getId(buffer, &identity) == 0) return identity;
    return 0; // No stable allocation identity: retain the uncached safe path.
}

bool HasExtension(const char* extensions, const char* extension) {
    if (!extensions || !extension) return false;
    const std::size_t length = std::strlen(extension);
    const char* current = extensions;
    while ((current = std::strstr(current, extension))) {
        if ((current == extensions || current[-1] == ' ') &&
            (current[length] == ' ' || current[length] == '\0')) return true;
        current += length;
    }
    return false;
}

struct ProjectionImageFunctions {
    PFNEGLGETNATIVECLIENTBUFFERANDROIDPROC nativeClientBuffer =
        reinterpret_cast<PFNEGLGETNATIVECLIENTBUFFERANDROIDPROC>(
            eglGetProcAddress("eglGetNativeClientBufferANDROID"));
    PFNEGLCREATEIMAGEKHRPROC createImage = reinterpret_cast<PFNEGLCREATEIMAGEKHRPROC>(
        eglGetProcAddress("eglCreateImageKHR"));
    PFNEGLDESTROYIMAGEKHRPROC destroyImage = reinterpret_cast<PFNEGLDESTROYIMAGEKHRPROC>(
        eglGetProcAddress("eglDestroyImageKHR"));
    PFNGLEGLIMAGETARGETTEXTURE2DOESPROC bindImage =
        reinterpret_cast<PFNGLEGLIMAGETARGETTEXTURE2DOESPROC>(
            eglGetProcAddress("glEGLImageTargetTexture2DOES"));
    bool ready() const { return nativeClientBuffer && createImage && destroyImage && bindImage; }
};

const ProjectionImageFunctions& ProjectionImageApi() {
    static const ProjectionImageFunctions functions;
    return functions;
}

// The import runs between frames, but ytbl and ImGui also own GL state. Restore
// every state touched by the copy, including separate read/draw framebuffers.
class ProjectionCopyState final {
public:
    ProjectionCopyState() {
        glGetIntegerv(GL_CURRENT_PROGRAM, &program_);
        glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &vertexArray_);
        glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &drawFramebuffer_);
        glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &readFramebuffer_);
        glGetIntegerv(GL_VIEWPORT, viewport_);
        glGetIntegerv(GL_ACTIVE_TEXTURE, &activeTexture_);
        glGetIntegerv(GL_PIXEL_UNPACK_BUFFER_BINDING, &unpackBuffer_);
        glActiveTexture(GL_TEXTURE0);
        glGetIntegerv(GL_TEXTURE_BINDING_2D, &texture0_);
        glGetIntegerv(GL_SAMPLER_BINDING, &sampler0_);
        glGetBooleanv(GL_COLOR_WRITEMASK, colorMask_);
        blend_ = glIsEnabled(GL_BLEND);
        depth_ = glIsEnabled(GL_DEPTH_TEST);
        scissor_ = glIsEnabled(GL_SCISSOR_TEST);
        stencil_ = glIsEnabled(GL_STENCIL_TEST);
        cull_ = glIsEnabled(GL_CULL_FACE);
        discard_ = glIsEnabled(GL_RASTERIZER_DISCARD);
        dither_ = glIsEnabled(GL_DITHER);
        glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
        glBindSampler(0, 0);
    }
    ~ProjectionCopyState() {
        glUseProgram(static_cast<GLuint>(program_));
        glBindVertexArray(static_cast<GLuint>(vertexArray_));
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, static_cast<GLuint>(drawFramebuffer_));
        glBindFramebuffer(GL_READ_FRAMEBUFFER, static_cast<GLuint>(readFramebuffer_));
        glViewport(viewport_[0], viewport_[1], viewport_[2], viewport_[3]);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(texture0_));
        glBindSampler(0, static_cast<GLuint>(sampler0_));
        glActiveTexture(static_cast<GLenum>(activeTexture_));
        glBindBuffer(GL_PIXEL_UNPACK_BUFFER, static_cast<GLuint>(unpackBuffer_));
        glColorMask(colorMask_[0], colorMask_[1], colorMask_[2], colorMask_[3]);
        restore(GL_BLEND, blend_);
        restore(GL_DEPTH_TEST, depth_);
        restore(GL_SCISSOR_TEST, scissor_);
        restore(GL_STENCIL_TEST, stencil_);
        restore(GL_CULL_FACE, cull_);
        restore(GL_RASTERIZER_DISCARD, discard_);
        restore(GL_DITHER, dither_);
    }
private:
    static void restore(GLenum capability, GLboolean enabled) {
        if (enabled) glEnable(capability); else glDisable(capability);
    }
    GLint program_ = 0, vertexArray_ = 0, drawFramebuffer_ = 0, readFramebuffer_ = 0;
    GLint activeTexture_ = GL_TEXTURE0, texture0_ = 0, sampler0_ = 0, unpackBuffer_ = 0;
    GLint viewport_[4]{};
    GLboolean colorMask_[4]{};
    GLboolean blend_ = GL_FALSE, depth_ = GL_FALSE, scissor_ = GL_FALSE;
    GLboolean stencil_ = GL_FALSE, cull_ = GL_FALSE, discard_ = GL_FALSE, dither_ = GL_FALSE;
};

GLuint compileShader(GLenum type, const char* source) {
    const GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);
    GLint status = GL_FALSE;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &status);
    if (status != GL_TRUE) {
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

void LogError(const char* message) {
    (void)message;   // 纯净版：AURORA_LOG 展开成 (void)0，参数需要显式标记未用
    AURORA_LOG(ANDROID_LOG_ERROR, kLogTag, "%s", message);
}
}

namespace aurora {

GlesBackend::~GlesBackend() {
    shutdown();
}

BackendDescriptor GlesBackend::descriptor() const {
    BackendDescriptor result = DescribeGles3Backend();
    result.state = imguiReady_ ? BackendState::Ready : BackendState::Unavailable;
    const bool transparent = transparentSurface_.load(std::memory_order_acquire);
    result.hostSurfaceIsTransparent = transparent;

    const bool projectionStale = projectionTexture_ != 0 && projectionReady_
        && projectionSourceIsStale();
    const bool projectionUsable = projectionReady_ && projectionTexture_ != 0 && !projectionStale;
    const bool offscreenUsable = backgroundReady_ && backgroundTexture_ != 0 && !transparent;
    result.backgroundSourceIsProjection = projectionUsable;
    result.supportsBackgroundInput = projectionUsable || offscreenUsable;
    // ★ 只要透明宿主上装好了场景合成器，玻璃的背景就由"逐面板 Z-order 合成"
    //   提供，全局纹理一律不再绑定（见 RenderBackend.h 的 PlanBackdrop）。
    //   采集不可用时也保持这个选择：那时玻璃退化为"只采样帧缓冲"，仍然是
    //   Z 序正确的内容，比退回整屏图（会采到自己）要诚实。
    result.sceneCompositorReady = transparent && ActiveGlesSceneCompositor() != nullptr;
    result.projectionFrameCount = projectionFrameCount_;
    result.projectionUploadFailureCount = projectionUploadFailureCount_;
    result.projectionSourceStale = projectionStale;

    if (projectionUsable) {
        result.backgroundTextureHandle = static_cast<std::uintptr_t>(projectionTexture_);
        result.rawBackgroundTextureHandle = static_cast<std::uintptr_t>(projectionTexture_);
        result.backgroundTextureWidth = projectionWidth_;
        result.backgroundTextureHeight = projectionHeight_;
    } else if (offscreenUsable) {
        result.backgroundTextureHandle = static_cast<std::uintptr_t>(
            blurTexture_ != 0 ? blurTexture_ : backgroundTexture_);
        result.rawBackgroundTextureHandle = static_cast<std::uintptr_t>(backgroundTexture_);
        result.backgroundTextureWidth = backgroundWidth_;
        result.backgroundTextureHeight = backgroundHeight_;
    }
    if (result.supportsBackgroundInput) {
        // ytbl addresses the backdrop in host screen pixels, so a downscaled
        // projection still covers the whole Surface (see RenderBackend.h).
        result.backgroundMappingWidth = BackdropMappingExtent(width_, result.backgroundTextureWidth);
        result.backgroundMappingHeight = BackdropMappingExtent(height_, result.backgroundTextureHeight);
    }
    if (transparent && !projectionUsable && neutralBackdropTexture_ != 0) {
        result.neutralBackdropTextureHandle = static_cast<std::uintptr_t>(neutralBackdropTexture_);
    }

    std::string diagnostic = imguiReady_ ? "GLES3 backend ready" : "GLES3 backend unavailable";
    diagnostic += transparent ? "; host=transparent" : "; host=opaque";
    if (result.sceneCompositorReady) {
        const GlesSceneCompositor* compositor = ActiveGlesSceneCompositor();
        const WorldBaseSource* base = compositor != nullptr ? &compositor->worldBase() : nullptr;
        diagnostic += "; backdrop=scene";
        diagnostic += (base != nullptr && base->ready) ? " world=live" : " world=absent";
        if (base != nullptr && base->ready) {
            diagnostic += " " + std::to_string(base->storageWidth) + "x" +
                          std::to_string(base->storageHeight);
        }
        if (compositor != nullptr) {
            diagnostic += " composed=" + std::to_string(compositor->composedCount()) +
                          " noBase=" + std::to_string(compositor->skippedNoBaseCount());
        }
    } else if (projectionUsable) {
        diagnostic += "; backdrop=projection " + std::to_string(projectionWidth_) + "x" +
                      std::to_string(projectionHeight_) + " map " +
                      std::to_string(result.backgroundMappingWidth) + "x" +
                      std::to_string(result.backgroundMappingHeight) + " frames " +
                      std::to_string(projectionFrameCount_);
        if (projectionHardwareFrameCount_ != 0) {
            diagnostic += "; gpu-capture " + std::to_string(projectionHardwareFrameCount_);
        }
    } else if (projectionStale) {
        diagnostic += "; backdrop=projection stale";
    } else if (offscreenUsable) {
        diagnostic += "; backdrop=offscreen " + std::to_string(backgroundWidth_) + "x" +
                      std::to_string(backgroundHeight_);
    } else {
        diagnostic += "; backdrop=none";
    }
    if (neutralBackdropTexture_ != 0 && transparent && !projectionUsable) {
        diagnostic += "; neutral-fallback";
    }
    if (projectionUploadFailureCount_ != 0) {
        diagnostic += "; projectionUploadFailures " + std::to_string(projectionUploadFailureCount_);
    }
    if (const char* violation = ValidateBackdropDescriptor(result)) {
        diagnostic += "; backdropInvariant: ";
        diagnostic += violation;
    }
    result.diagnostic = std::move(diagnostic);
    return result;
}

bool GlesBackend::initialize(ANativeWindow* window, int width, int height) {
    shutdown();
    width_ = width > 0 ? width : 1;
    height_ = height > 0 ? height : 1;
    if (!createGraphics(window)) {
        return false;
    }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    imguiContextReady_ = true;
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
#if defined(AURORA_HAS_LIQUIDGLASS_FONT)
    // ★★ 字体图集范围：**不能只申请"常用汉字"**（真机踩到的坑）
    //
    //   原来只传 GetGlyphRangesChineseSimplifiedCommon()
    //   = 基本拉丁 + 半角 + 假名 + 2500 个常用简体汉字。
    //   结果是 `●`(U+25CF) `★`(U+2605) `→`(U+2192) 这些**一点都不生僻**的符号
    //   在界面上显示成问号 —— 因为"字体文件里有没有这个字形"和
    //   "图集有没有申请这个码点"是**两件事**：实测这个内嵌字体有 7718 个字形，
    //   `● ■ ▲ ★ ☆ → × … ℃ ① √` 全都在里面，只是没人申请。
    //
    //   现在的做法：默认区 + 常用汉字区 + 常用符号区 + **扫源码得到的 UI 字符表**
    //   （tools/gen_ui_glyphs.py 生成 UiGlyphSet.h）。后者保证"界面上真会画的字"
    //   一个都不落，同时不必把整个 CJK 区烘进去（图集大小与启动时间都可控）。
    //
    //   ★ ranges 必须是**静态**的：ImGui 只存指针，不复制（imgui.h:2942 写明）。
    static ImVector<ImWchar> uiGlyphRanges;
    if (uiGlyphRanges.empty()) {
        static const ImWchar kSymbolRanges[] = {
            0x2000, 0x206F,   // 通用标点（… — 等）
            0x2070, 0x209F,   // 上下标
            0x20A0, 0x20CF,   // 货币符号
            0x2100, 0x214F,   // 字母式符号（℃ 等）
            0x2190, 0x21FF,   // 箭头（→ ← ↔ 等）
            0x2200, 0x22FF,   // 数学运算符（√ ± − 等）
            0x2460, 0x24FF,   // 带圈字母数字（① 等）
            0x25A0, 0x25FF,   // 几何图形（● ■ ▲ ▼ 等）
            0x2600, 0x26FF,   // 杂项符号（★ ☆ ⚠ 等）
            0x2700, 0x27BF,   // 装饰符号（✓ ✗ —— 本字体没有，申请了也不会被烘焙）
            0x3000, 0x303F,   // CJK 标点
            0xFE00, 0xFE0F,   // 变体选择符（零宽，无害）
            0xFF00, 0xFFEF,   // 半角/全角
            0,
        };
        ImFontGlyphRangesBuilder builder;
        builder.AddRanges(io.Fonts->GetGlyphRangesDefault());
        builder.AddRanges(io.Fonts->GetGlyphRangesChineseSimplifiedCommon());
        builder.AddRanges(kSymbolRanges);
        for (int i = 0; i < aurora::kUiGlyphCount; ++i) {
            builder.AddChar(static_cast<ImWchar>(aurora::kUiGlyphs[i]));
        }
        builder.BuildRanges(&uiGlyphRanges);
    }
    ImFontConfig fontConfig;
    fontConfig.FontDataOwnedByAtlas = false;
    if (io.Fonts->AddFontFromMemoryTTF(
            const_cast<unsigned int*>(font_v), static_cast<int>(font_v_size), 40.0f,
            &fontConfig, uiGlyphRanges.Data) == nullptr) {
        io.Fonts->AddFontDefault();
    }
#else
    io.Fonts->AddFontDefault();
#endif
    ImGui::StyleColorsDark();
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 18.0f;
    style.FrameRounding = 10.0f;
    style.GrabRounding = 10.0f;
    style.WindowPadding = ImVec2(22.0f, 22.0f);
    style.FramePadding = ImVec2(14.0f, 10.0f);
    style.ItemSpacing = ImVec2(12.0f, 12.0f);
    style.Colors[ImGuiCol_WindowBg] = ImVec4(0.055f, 0.065f, 0.095f, 0.96f);
    style.Colors[ImGuiCol_ChildBg] = ImVec4(0.09f, 0.105f, 0.145f, 0.82f);
    style.Colors[ImGuiCol_FrameBg] = ImVec4(0.14f, 0.17f, 0.23f, 0.92f);
    style.Colors[ImGuiCol_Button] = ImVec4(0.24f, 0.42f, 0.75f, 0.92f);
    style.Colors[ImGuiCol_ButtonHovered] = ImVec4(0.33f, 0.53f, 0.9f, 0.98f);
    style.Colors[ImGuiCol_ButtonActive] = ImVec4(0.17f, 0.32f, 0.64f, 1.0f);

    imguiReady_ = ImGui_ImplOpenGL3_Init("#version 300 es");
    if (!imguiReady_) {
        shutdown();
        return false;
    }
    // 图集尺寸记一笔：范围从"只申请常用汉字"扩到"常用汉字 + 符号区 + UI 全字符"之后，
    // 这个数字就是那笔改动的**内存代价**（RGBA32 上传，宽×高×4 字节）。
    // ★ 图集是**懒构建**的（第一次 GetTexDataAsRGBA32 才建），所以这里显式 Build 一次，
    //   否则读到的是 0x0（第一版就是这么记错的）。Build 之后后端再取就是现成的数据。
    io.Fonts->Build();
    AURORA_LOG(ANDROID_LOG_INFO, kLogTag,
                        "字体图集 %dx%d（%.1f MB 上传，含 UI 字符 %d 个 + 常用汉字 + 符号区）",
                        io.Fonts->TexWidth, io.Fonts->TexHeight,
                        static_cast<double>(io.Fonts->TexWidth) * io.Fonts->TexHeight * 4.0 /
                            (1024.0 * 1024.0),
                        aurora::kUiGlyphCount);
    createNeutralBackdrop();
    if (!transparentSurface_.load(std::memory_order_acquire)) createBackgroundResources();
    // ★ 逐面板 Z-order 场景合成器。注册本身不碰 GL；GL 对象在第一次合成时
    //   惰性创建（见 GlesSceneCompositor::ensureResources）。
    InstallGlesSceneCompositor();
    return true;
}

bool GlesBackend::createGraphics(ANativeWindow* window) {
    if (ANativeWindow_setBuffersGeometry(window, width_, height_, 0) != 0) {
        LogError("native window initial buffer geometry failed");
        return false;
    }
    display_ = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (display_ == EGL_NO_DISPLAY || !eglInitialize(display_, nullptr, nullptr)) {
        LogError("eglInitialize failed");
        return false;
    }

    const EGLint configAttributes[] = {
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT,
        EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
        EGL_RED_SIZE, 8,
        EGL_GREEN_SIZE, 8,
        EGL_BLUE_SIZE, 8,
        EGL_ALPHA_SIZE, 8,
        EGL_NONE};
    EGLConfig config = nullptr;
    EGLint configCount = 0;
    if (!eglChooseConfig(display_, configAttributes, &config, 1, &configCount) || configCount == 0) {
        LogError("eglChooseConfig failed");
        shutdown();
        return false;
    }

    const EGLint contextAttributes[] = {EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE};
    context_ = eglCreateContext(display_, config, EGL_NO_CONTEXT, contextAttributes);
    surface_ = eglCreateWindowSurface(display_, config, window, nullptr);
    if (context_ == EGL_NO_CONTEXT || surface_ == EGL_NO_SURFACE ||
        !eglMakeCurrent(display_, surface_, surface_, context_)) {
        LogError("EGL context or surface creation failed");
        shutdown();
        return false;
    }
    nativeWindow_ = window;
    // ★★ 默认 interval = 0（latest-droppable）。真机实测（测试机 / Android 16，
    //    完整光学一个都没关）：
    //
    //      interval=1：submit 被驱动背压拖到 14.1ms、GPU 停在 222MHz（14 档最低）、
    //                  render 60.3fps、背景 lag 2.53 源帧；
    //      interval=0：submit 1.35ms、GPU 提到 342MHz、render 120fps、lag 0.83 源帧。
    //
    //    为什么 interval=1 反而更慢：它把渲染线程钉在 display vsync 上，而 GPU 被
    //    系统压在最低频档 ⇒ 每帧都在 vsync 前完不成 ⇒ 等下一帧 ⇒ 帧率减半、延迟翻倍，
    //    而"每帧 14ms"又让系统认为负载不高、继续不提频（死锁）。
    //
    //    ★ interval=0 在 Android 上**不是撕裂**：生产者队列变成 latest-droppable，
    //      SurfaceFlinger 仍在 display vsync 取**完整**的 buffer 去合成（HWComposer
    //      按 vsync 取），所以看到的是"更少延迟"而不是"画面被撕开"。
    //      用户报的"运动物体在窗口内外对不上"是镜像+渲染链路的时间错位，
    //      这一项把源帧延迟从 2.5 帧压到 0.8 帧。
    //
    //    `AURORA_SWAP_INTERVAL=1` 可回退到旧的 vsync 锁定行为，供同机 A/B。
    const char* swapInterval = std::getenv("AURORA_SWAP_INTERVAL");
    const int interval = (swapInterval != nullptr && swapInterval[0] == '1') ? 1 : 0;
    if (!eglSwapInterval(display_, interval)) {
        eglSwapInterval(display_, 0);
    }
    applyFrameRateVote();
    initializeFrameDiagnostics();
    return true;
}

void GlesBackend::setWorldComposeRegion(const float region[4]) {
    if (region == nullptr) {
        worldComposeRegionValid_ = false;
        return;
    }
    std::copy(region, region + 4, worldComposeRegion_);
    // 面积为 0 也算"有效输入"，语义是"本帧不铺"（见 composeFullScreen）。
    worldComposeRegionValid_ = true;
}

void GlesBackend::setTargetFrameRate(float framesPerSecond) {
    if (!std::isfinite(framesPerSecond)) return;
    targetFrameRate_ = std::clamp(framesPerSecond, 1.0f, 240.0f);
    applyFrameRateVote();
}

void GlesBackend::applyFrameRateVote() {
    if (nativeWindow_ == nullptr) return;
    using SetRateWithStrategy = int (*)(ANativeWindow*, float, std::int8_t, std::int8_t);
    using SetRate = int (*)(ANativeWindow*, float, std::int8_t);
    static void* library = dlopen("libandroid.so", RTLD_NOW | RTLD_LOCAL);
    static auto withStrategy = library ? reinterpret_cast<SetRateWithStrategy>(
        dlsym(library, "ANativeWindow_setFrameRateWithChangeStrategy")) : nullptr;
    static auto setRate = library ? reinterpret_cast<SetRate>(
        dlsym(library, "ANativeWindow_setFrameRate")) : nullptr;
    // DEFAULT compatibility and ONLY_IF_SEAMLESS respect the display's modes
    // and do not restart the active screen when the user changes target rate.
    framePerf_.frameRateVoteResult = withStrategy ? withStrategy(nativeWindow_, targetFrameRate_, 0, 0)
        : setRate ? setRate(nativeWindow_, targetFrameRate_, 0) : -1;
}

void GlesBackend::initializeFrameDiagnostics() {
    pendingPresentFrameIds_.clear();
    lastPresentedSourceTimestampNs_ = 0;
    const char* extensions = eglQueryString(display_, EGL_EXTENSIONS);
    if (HasExtension(extensions, "EGL_ANDROID_get_frame_timestamps")) {
        getNextFrameId_ = reinterpret_cast<PFNEGLGETNEXTFRAMEIDANDROIDPROC>(
            eglGetProcAddress("eglGetNextFrameIdANDROID"));
        getFrameTimestamps_ = reinterpret_cast<PFNEGLGETFRAMETIMESTAMPSANDROIDPROC>(
            eglGetProcAddress("eglGetFrameTimestampsANDROID"));
        using Supported = EGLBoolean (*)(EGLDisplay, EGLSurface, EGLint);
        auto supported = reinterpret_cast<Supported>(
            eglGetProcAddress("eglGetFrameTimestampSupportedANDROID"));
        framePerf_.presentTimestampsSupported = getNextFrameId_ && getFrameTimestamps_ && supported &&
            supported(display_, surface_, EGL_DISPLAY_PRESENT_TIME_ANDROID) == EGL_TRUE &&
            eglSurfaceAttrib(display_, surface_, EGL_TIMESTAMPS_ANDROID, EGL_TRUE) == EGL_TRUE;
    }
    // Driver timer queries are useful for A/B diagnostics, but are opt-in so
    // the normal high-rate path pays no profiling instrumentation cost.
    const char* diagnostics = std::getenv("AURORA_FRAME_DIAGNOSTICS");
    if (diagnostics && diagnostics[0] == '1' &&
        HasExtension(reinterpret_cast<const char*>(glGetString(GL_EXTENSIONS)),
                     "GL_EXT_disjoint_timer_query") &&
        eglGetProcAddress("glGetQueryObjectui64vEXT")) {
        for (auto& timer : gpuTimers_) glGenQueries(1, &timer.query);
        framePerf_.gpuTimerSupported = std::all_of(std::begin(gpuTimers_), std::end(gpuTimers_),
            [](const GpuTimer& timer) { return timer.query != 0; });
    }
}

void GlesBackend::pollPresentTimestamps() {
    if (!framePerf_.presentTimestampsSupported) return;
    const EGLint name = EGL_DISPLAY_PRESENT_TIME_ANDROID;
    for (auto frame = pendingPresentFrameIds_.begin(); frame != pendingPresentFrameIds_.end();) {
        EGLnsecsANDROID timestamp = EGL_TIMESTAMP_PENDING_ANDROID;
        if (!getFrameTimestamps_(display_, surface_, frame->frameId, 1, &name, &timestamp)) {
            ++framePerf_.invalidPresentTimestamps;
            frame = pendingPresentFrameIds_.erase(frame);
            continue;
        }
        if (timestamp == EGL_TIMESTAMP_PENDING_ANDROID) { ++frame; continue; }
        if (timestamp > 0 && timestamp != EGL_TIMESTAMP_INVALID_ANDROID) {
            // Driver implementations can attach the same display presentation
            // to coalesced producer frames. Count strictly increasing actual
            // presentations, rather than treating every successful swap as one.
            if (timestamp > framePerf_.lastPresentNanoseconds) {
                ++framePerf_.presentedFrames;
                framePerf_.lastPresentNanoseconds = timestamp;
            }
            const std::int64_t ageNs = timestamp - frame->sourceTimestampNs;
            if (frame->sourceTimestampNs > lastPresentedSourceTimestampNs_ && ageNs >= 0 &&
                ageNs < 10000000000LL) {
                framePerf_.sourceToPresentMilliseconds = ageNs * 1.0e-6;
                lastPresentedSourceTimestampNs_ = frame->sourceTimestampNs;
                ++framePerf_.sourcePresentAgeSamples;
            }
        } else {
            ++framePerf_.invalidPresentTimestamps;
        }
        frame = pendingPresentFrameIds_.erase(frame);
    }
    // A broken vendor timestamp implementation must not grow diagnostics memory
    // or influence the actual producer/display queue.
    if (pendingPresentFrameIds_.size() > 128u) {
        const std::size_t excess = pendingPresentFrameIds_.size() - 128u;
        framePerf_.invalidPresentTimestamps += excess;
        pendingPresentFrameIds_.erase(pendingPresentFrameIds_.begin(),
                                     pendingPresentFrameIds_.begin() + excess);
    }
}

void GlesBackend::pollGpuTimers() {
    if (!framePerf_.gpuTimerSupported) return;
    using GetQuery64 = void (*)(GLuint, GLenum, GLuint64*);
    static auto getQuery64 = reinterpret_cast<GetQuery64>(eglGetProcAddress("glGetQueryObjectui64vEXT"));
    GLint disjoint = GL_FALSE;
    glGetIntegerv(GL_GPU_DISJOINT_EXT, &disjoint);
    for (auto& timer : gpuTimers_) {
        if (!timer.pending) continue;
        GLuint available = GL_FALSE;
        glGetQueryObjectuiv(timer.query, GL_QUERY_RESULT_AVAILABLE, &available);
        if (!available) continue;
        GLuint64 elapsed = 0;
        getQuery64(timer.query, GL_QUERY_RESULT, &elapsed);
        if (!disjoint) framePerf_.lastGpuMilliseconds = elapsed * 1.0e-6;
        timer.pending = false;
    }
}

void GlesBackend::projectionSourceAlive() {
    // Process health and frame content time are separate. A mirror with no new
    // damage keeps its last image without pretending it captured another frame.
    if (projectionFrameSeen_) projectionFrameStamp_ = std::chrono::steady_clock::now();
}

bool GlesBackend::createBackgroundResources() {
    if (width_ < 2 || height_ < 2) return false;
    backgroundWidth_ = width_;
    backgroundHeight_ = height_;
    glGenTextures(1, &backgroundTexture_);
    glBindTexture(GL_TEXTURE_2D, backgroundTexture_);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width_, height_, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    const int blurWidth = std::max(1, width_ / 2);
    const int blurHeight = std::max(1, height_ / 2);
    const auto createBlurTarget = [blurWidth, blurHeight](GLuint& texture, GLuint& framebuffer) {
        glGenTextures(1, &texture);
        glBindTexture(GL_TEXTURE_2D, texture);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, blurWidth, blurHeight, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glGenFramebuffers(1, &framebuffer);
        glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture, 0);
        return glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
    };
    const bool targetReady = createBlurTarget(blurTexture_, blurFramebuffer_) &&
                             createBlurTarget(blurTexturePing_, blurFramebufferPing_);
    const char* vertex = "#version 300 es\nout vec2 vUv;\nvoid main(){vec2 p=vec2((gl_VertexID<<1)&2,gl_VertexID&2);vUv=p*0.5;gl_Position=vec4(p*2.0-1.0,0,1);}";
    const char* fragment = "#version 300 es\nprecision highp float;uniform sampler2D uSource;uniform vec2 uTexel;uniform vec2 uDirection;uniform int uDownsample;in vec2 vUv;out vec4 o;void main(){if(uDownsample!=0){vec2 d=uTexel*0.5;o=(texture(uSource,vUv+vec2(-d.x,-d.y))+texture(uSource,vUv+vec2(d.x,-d.y))+texture(uSource,vUv+vec2(-d.x,d.y))+texture(uSource,vUv+vec2(d.x,d.y)))*0.25;return;}vec2 d=uTexel*uDirection;o=texture(uSource,vUv)*0.2270270270+(texture(uSource,vUv+d*1.3846153846)+texture(uSource,vUv-d*1.3846153846))*0.3162162162+(texture(uSource,vUv+d*3.2307692308)+texture(uSource,vUv-d*3.2307692308))*0.0702702703;}";
    const GLuint vs = compileShader(GL_VERTEX_SHADER, vertex);
    const GLuint fs = compileShader(GL_FRAGMENT_SHADER, fragment);
    if (vs == 0 || fs == 0) {
        if (vs) glDeleteShader(vs);
        if (fs) glDeleteShader(fs);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        destroyBackgroundResources();
        return false;
    }
    blurProgram_ = glCreateProgram();
    glAttachShader(blurProgram_, vs); glAttachShader(blurProgram_, fs); glLinkProgram(blurProgram_);
    glDeleteShader(vs); glDeleteShader(fs);
    GLint linked = GL_FALSE; glGetProgramiv(blurProgram_, GL_LINK_STATUS, &linked);
    glBindFramebuffer(GL_FRAMEBUFFER, 0); glBindTexture(GL_TEXTURE_2D, 0);
    backgroundReady_ = linked == GL_TRUE && targetReady && backgroundTexture_ != 0;
    if (!backgroundReady_) {
        glDeleteProgram(blurProgram_);
        blurProgram_ = 0;
        destroyBackgroundResources();
    }
    return backgroundReady_;
}

void GlesBackend::destroyBackgroundResources() {
    // GL names are only valid in the context that created them. After a context
    // teardown the driver may hand the same name to an unrelated object, so a
    // stale delete would destroy someone else's resource.
    const bool owned = hasCurrentContext();
    if (owned) {
        if (blurProgram_ != 0) glDeleteProgram(blurProgram_);
        if (blurFramebuffer_ != 0) glDeleteFramebuffers(1, &blurFramebuffer_);
        if (blurFramebufferPing_ != 0) glDeleteFramebuffers(1, &blurFramebufferPing_);
        if (blurTexture_ != 0) glDeleteTextures(1, &blurTexture_);
        if (blurTexturePing_ != 0) glDeleteTextures(1, &blurTexturePing_);
        if (backgroundTexture_ != 0) glDeleteTextures(1, &backgroundTexture_);
    }
    blurProgram_ = 0; blurFramebuffer_ = 0; blurFramebufferPing_ = 0;
    blurTexture_ = 0; blurTexturePing_ = 0; backgroundTexture_ = 0;
    backgroundReady_ = false; backgroundWidth_ = 0; backgroundHeight_ = 0;
}

void GlesBackend::destroyProjectionTexture() {
    destroyProjectionImportResources();
    if (hasCurrentContext() && projectionTexture_ != 0) {
        glDeleteTextures(1, &projectionTexture_);
    }
    projectionTexture_ = 0;
    projectionWidth_ = 0;
    projectionHeight_ = 0;
    projectionReady_ = false;
    projectionFrameSeen_ = false;
    projectionFrameStamp_ = std::chrono::steady_clock::time_point{};
    projectionFrameCount_ = 0;
    projectionUploadFailureCount_ = 0;
    projectionHardwareFrameCount_ = 0;
}

bool GlesBackend::createProjectionImportResources() {
    if (projectionImportProgram_ != 0 && projectionImportFramebuffer_ != 0 &&
        projectionImportVertexArray_ != 0) return true;
    if (!hasCurrentContext() || !ProjectionImageApi().ready()) return false;
    // A prior allocation failure can leave only part of this set alive. Clean
    // that partial set before retrying so no GL name is overwritten and leaked.
    if (projectionImportProgram_) glDeleteProgram(projectionImportProgram_);
    if (projectionImportFramebuffer_) glDeleteFramebuffers(1, &projectionImportFramebuffer_);
    if (projectionImportVertexArray_) glDeleteVertexArrays(1, &projectionImportVertexArray_);
    projectionImportProgram_ = 0;
    projectionImportFramebuffer_ = 0;
    projectionImportVertexArray_ = 0;
    const char* vertex =
        "#version 300 es\n"
        "out vec2 vUv;\n"
        "void main(){vec2 p=vec2((gl_VertexID<<1)&2,gl_VertexID&2);"
        "vUv=p;gl_Position=vec4(p*2.0-1.0,0.0,1.0);}\n";
    // Native screenshot buffers start at the top scanline. ytbl expects the
    // desktop top at texture V=1, so flip in this single GPU copy pass.
    const char* fragment =
        "#version 300 es\n"
        "precision highp float;uniform sampler2D uSource;in vec2 vUv;out vec4 o;\n"
        "void main(){o=texture(uSource,vec2(vUv.x,1.0-vUv.y));}\n";
    const GLuint vs = compileShader(GL_VERTEX_SHADER, vertex);
    const GLuint fs = compileShader(GL_FRAGMENT_SHADER, fragment);
    if (vs == 0 || fs == 0) {
        if (vs) glDeleteShader(vs);
        if (fs) glDeleteShader(fs);
        return false;
    }
    projectionImportProgram_ = glCreateProgram();
    glAttachShader(projectionImportProgram_, vs);
    glAttachShader(projectionImportProgram_, fs);
    glLinkProgram(projectionImportProgram_);
    glDeleteShader(vs);
    glDeleteShader(fs);
    GLint linked = GL_FALSE;
    glGetProgramiv(projectionImportProgram_, GL_LINK_STATUS, &linked);
    if (linked != GL_TRUE) {
        glDeleteProgram(projectionImportProgram_);
        projectionImportProgram_ = 0;
        return false;
    }
    glGenFramebuffers(1, &projectionImportFramebuffer_);
    glGenVertexArrays(1, &projectionImportVertexArray_);
    projectionImportSourceUniform_ = glGetUniformLocation(projectionImportProgram_, "uSource");
    const bool ready = projectionImportFramebuffer_ != 0 && projectionImportVertexArray_ != 0 &&
                       glGetError() == GL_NO_ERROR;
    if (!ready) {
        if (projectionImportProgram_) glDeleteProgram(projectionImportProgram_);
        if (projectionImportFramebuffer_) glDeleteFramebuffers(1, &projectionImportFramebuffer_);
        if (projectionImportVertexArray_) glDeleteVertexArrays(1, &projectionImportVertexArray_);
        projectionImportProgram_ = 0;
        projectionImportFramebuffer_ = 0;
        projectionImportVertexArray_ = 0;
    }
    return ready;
}

void GlesBackend::destroyProjectionImport(ProjectionImport& import) {
    if (hasCurrentContext()) {
        if (import.completion) glDeleteSync(import.completion);
        if (import.texture) glDeleteTextures(1, &import.texture);
    }
    const auto& api = ProjectionImageApi();
    if (display_ != EGL_NO_DISPLAY && import.image != EGL_NO_IMAGE_KHR && api.destroyImage) {
        api.destroyImage(display_, import.image);
    }
    if (import.buffer) AHardwareBuffer_release(import.buffer);
    auto readComplete = std::exchange(import.onGpuReadComplete, {});
    import = {};
    if (readComplete) readComplete();
}

void GlesBackend::releaseProjectionImports(bool waitForCompletion) {
    const bool owned = hasCurrentContext();
    if (owned && waitForCompletion && !projectionImports_.empty()) glFinish();
    for (auto import = projectionImports_.begin(); import != projectionImports_.end();) {
        bool ready = waitForCompletion || !owned || import->completion == nullptr;
        if (!ready && import->completion != nullptr) {
            const GLenum status = glClientWaitSync(import->completion, 0, 0);
            ready = status == GL_ALREADY_SIGNALED || status == GL_CONDITION_SATISFIED;
            if (status == GL_WAIT_FAILED) {
                // A lost or invalid fence must never release a buffer while its
                // draw is in flight. Finish once before recovering this slot.
                glFinish();
                ready = true;
            }
        }
        if (!ready) { ++import; continue; }
        if (owned && import->completion) glDeleteSync(import->completion);
        import->completion = nullptr;
        // AHardwareBuffer references protect allocation lifetime, not the
        // producer's frame contents. Return its ImageReader lease after this
        // fence, while retaining EGLImage/texture for the next use of the ID.
        auto readComplete = std::exchange(import->onGpuReadComplete, {});
        if (waitForCompletion || !owned || import->identity == 0) {
            destroyProjectionImport(*import);
            import = projectionImports_.erase(import);
        } else {
            ++import;
        }
        if (readComplete) readComplete();
    }
}

void GlesBackend::destroyProjectionImportResources() {
    releaseProjectionImports(true);
    if (hasCurrentContext()) {
        if (projectionImportProgram_) glDeleteProgram(projectionImportProgram_);
        if (projectionImportFramebuffer_) glDeleteFramebuffers(1, &projectionImportFramebuffer_);
        if (projectionImportVertexArray_) glDeleteVertexArrays(1, &projectionImportVertexArray_);
    }
    projectionImportProgram_ = 0;
    projectionImportFramebuffer_ = 0;
    projectionImportVertexArray_ = 0;
    projectionImportSourceUniform_ = -1;
}

bool GlesBackend::createNeutralBackdrop() {
    if (neutralBackdropTexture_ != 0) {
        return true;
    }
    if (!hasCurrentContext()) {
        return false;
    }
    // glGetError reports accumulated state, so clear whatever an earlier
    // subsystem left behind before judging this allocation.
    while (glGetError() != GL_NO_ERROR) {
    }
    const unsigned char transparentPixel[4] = {0u, 0u, 0u, 0u};
    GLuint texture = 0;
    glGenTextures(1, &texture);
    if (texture == 0) {
        return false;
    }
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE,
                 transparentPixel);
    glBindTexture(GL_TEXTURE_2D, 0);
    if (glGetError() != GL_NO_ERROR) {
        glDeleteTextures(1, &texture);
        return false;
    }
    neutralBackdropTexture_ = texture;
    return true;
}

void GlesBackend::destroyNeutralBackdrop() {
    if (hasCurrentContext() && neutralBackdropTexture_ != 0) {
        glDeleteTextures(1, &neutralBackdropTexture_);
    }
    neutralBackdropTexture_ = 0;
}

bool GlesBackend::hasCurrentContext() const {
    if (display_ == EGL_NO_DISPLAY || surface_ == EGL_NO_SURFACE || context_ == EGL_NO_CONTEXT) {
        return false;
    }
    return eglGetCurrentContext() == context_;
}

bool GlesBackend::projectionSourceIsStale() const {
    if (!projectionFrameSeen_) {
        return true;
    }
    return std::chrono::steady_clock::now() - projectionFrameStamp_ > kProjectionStaleAfter;
}

bool GlesBackend::projectionFrameIsValid(const std::uint8_t* rgba, std::size_t byteCount,
                                         int width, int height) {
    if (rgba == nullptr || width <= 0 || height <= 0) {
        return false;
    }
    if (width > kProjectionMaxDimension || height > kProjectionMaxDimension) {
        return false;
    }
    const std::size_t required = static_cast<std::size_t>(width) *
                                 static_cast<std::size_t>(height) * 4u;
    // An exact match keeps the texture from being partly refreshed with a
    // mismatched producer geometry; the JNI bridge already validates equality.
    return byteCount == required && required <= kProjectionMaxBytes;
}

void GlesBackend::updateProjectionFrame(const std::uint8_t* rgba, std::size_t byteCount,
                                        int width, int height) {
    if (!projectionFrameIsValid(rgba, byteCount, width, height)) {
        return;
    }
    if (!hasCurrentContext()) {
        // Surface torn down between the producer thread's copy and this upload.
        // Dropping the frame is correct: a texture name created here would
        // outlive its context.
        projectionReady_ = false;
        ++projectionUploadFailureCount_;
        return;
    }
    // glGetError reports the accumulated state, so drain whatever an earlier
    // subsystem left behind before attributing an error to this upload.
    while (glGetError() != GL_NO_ERROR) {
    }

    if (projectionTexture_ == 0 || projectionWidth_ != width || projectionHeight_ != height) {
        if (projectionTexture_ != 0) {
            glDeleteTextures(1, &projectionTexture_);
            projectionTexture_ = 0;
            projectionReady_ = false;
        }
        GLuint texture = 0;
        glGenTextures(1, &texture);
        if (texture == 0) {
            projectionWidth_ = 0;
            projectionHeight_ = 0;
            ++projectionUploadFailureCount_;
            return;
        }
        projectionTexture_ = texture;
        projectionWidth_ = width;
        projectionHeight_ = height;
        glBindTexture(GL_TEXTURE_2D, projectionTexture_);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA,
                     GL_UNSIGNED_BYTE, nullptr);
    } else {
        glBindTexture(GL_TEXTURE_2D, projectionTexture_);
    }
    // RGBA8 rows are always 4-byte aligned, and the producer hands over tightly
    // packed rows (AuroraProjectionService also flips them so texture V=1 is the
    // top scanline, matching what ytbl's sceneUv expects).
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, width, height, GL_RGBA,
                    GL_UNSIGNED_BYTE, rgba);
    glBindTexture(GL_TEXTURE_2D, 0);
    projectionReady_ = projectionTexture_ != 0 && glGetError() == GL_NO_ERROR;
    if (projectionReady_) {
        projectionFrameSeen_ = true;
        projectionFrameStamp_ = std::chrono::steady_clock::now();
        ++projectionFrameCount_;
    } else {
        ++projectionUploadFailureCount_;
    }
}

bool GlesBackend::updateProjectionHardwareBuffer(AHardwareBuffer* buffer,
                                                std::function<void()> onGpuReadComplete,
                                                std::int64_t sourceTimestampNs) {
    // The capture side hands over a buffer together with a handshake callback: it may
    // only reuse that buffer once the flip pass has really finished reading it. Every
    // exit from this function must fire the callback exactly once, otherwise the
    // producer waits forever for a frame slot that never comes back.
    std::function<void()> readComplete = std::move(onGpuReadComplete);
    const auto notifyProducer = [&readComplete]() {
        if (readComplete) {
            auto callback = std::move(readComplete);
            callback();
        }
    };
    const auto fail = [this, &notifyProducer](const char* reason) {
            (void)reason;   // 纯净版同上
        ++projectionUploadFailureCount_;
        if (projectionUploadFailureCount_ <= 3u || projectionUploadFailureCount_ % 60u == 0u) {
            AURORA_LOG(ANDROID_LOG_WARN, kLogTag,
                                "Hardware backdrop import failed: %s (total %u)",
                                reason, projectionUploadFailureCount_);
        }
        notifyProducer();
        return false;
    };
    if (buffer == nullptr || !hasCurrentContext()) return fail("no buffer/current context");
    AHardwareBuffer_Desc description{};
    AHardwareBuffer_describe(buffer, &description);
    if (description.width == 0 || description.height == 0 || description.layers != 1 ||
        description.width > kProjectionMaxDimension || description.height > kProjectionMaxDimension) {
        return fail("invalid buffer geometry");
    }
    const auto& api = ProjectionImageApi();
    if (!api.ready()) return fail("EGL native-buffer image extensions unavailable");
    releaseProjectionImports(false);
    const auto pendingCount = static_cast<std::size_t>(std::count_if(
        projectionImports_.begin(), projectionImports_.end(),
        [](const ProjectionImport& entry) { return entry.completion != nullptr; }));
    if (pendingCount >= kProjectionMaxPendingImports) {
        // Keep memory bounded on a stalled GPU without blocking rendering or
        // growing an unbounded list of full-screen screenshot allocations.
        return fail("GPU capture queue full");
    }
    while (glGetError() != GL_NO_ERROR) {}
    ProjectionCopyState restore;
    if (!createProjectionImportResources()) return fail("copy resources unavailable");

    const std::uint64_t identity = HardwareBufferIdentity(buffer);
    auto cached = identity != 0 ? std::find_if(projectionImports_.begin(), projectionImports_.end(),
        [identity](const ProjectionImport& entry) { return entry.identity == identity; })
        : projectionImports_.end();
    if (cached != projectionImports_.end() && cached->completion != nullptr) {
        // A conforming producer never reuses a leased allocation. Refuse to
        // overwrite an in-flight capture even if it delivers an invalid reuse.
        return fail("producer reused an outstanding buffer lease");
    }
    const bool cacheHit = cached != projectionImports_.end();
    if (!cacheHit && projectionImports_.size() >= kProjectionMaxCachedImports) {
        auto victim = projectionImports_.end();
        for (auto entry = projectionImports_.begin(); entry != projectionImports_.end(); ++entry) {
            if (entry->completion == nullptr &&
                (victim == projectionImports_.end() || entry->lastUse < victim->lastUse)) victim = entry;
        }
        if (victim == projectionImports_.end()) return fail("no reusable import cache slot");
        destroyProjectionImport(*victim);
        projectionImports_.erase(victim);
    }
    if (!cacheHit) {
        projectionImports_.emplace_back();
        cached = std::prev(projectionImports_.end());
        AHardwareBuffer_acquire(buffer);
        cached->buffer = buffer;
        cached->identity = identity;
        ++framePerf_.importCacheMisses;
    } else {
        ++framePerf_.importCacheHits;
    }
    ProjectionImport& import = *cached;
    import.lastUse = projectionHardwareFrameCount_ + 1u;
    const auto discardImport = [&]() {
        destroyProjectionImport(import);
        projectionImports_.erase(cached);
    };
    if (!cacheHit) {
        const EGLClientBuffer client = api.nativeClientBuffer(buffer);
        const EGLint attributes[] = {EGL_IMAGE_PRESERVED_KHR, EGL_TRUE, EGL_NONE};
        if (client) {
            import.image = api.createImage(display_, EGL_NO_CONTEXT, EGL_NATIVE_BUFFER_ANDROID,
                                           client, attributes);
        }
        if (import.image == EGL_NO_IMAGE_KHR) {
            discardImport();
            return fail("eglCreateImageKHR");
        }
        glGenTextures(1, &import.texture);
        glBindTexture(GL_TEXTURE_2D, import.texture);
        api.bindImage(GL_TEXTURE_2D, import.image);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        if (import.texture == 0 || glGetError() != GL_NO_ERROR) {
            discardImport();
            return fail("glEGLImageTargetTexture2DOES");
        }
    }

    const int width = static_cast<int>(description.width);
    const int height = static_cast<int>(description.height);
    if (projectionTexture_ == 0 || projectionWidth_ != width || projectionHeight_ != height) {
        if (projectionTexture_ != 0) glDeleteTextures(1, &projectionTexture_);
        projectionTexture_ = 0;
        projectionReady_ = false;
        glGenTextures(1, &projectionTexture_);
        glBindTexture(GL_TEXTURE_2D, projectionTexture_);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA,
                     GL_UNSIGNED_BYTE, nullptr);
        projectionWidth_ = width;
        projectionHeight_ = height;
    }
    glBindFramebuffer(GL_FRAMEBUFFER, projectionImportFramebuffer_);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                           projectionTexture_, 0);
    if (projectionTexture_ == 0 || glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE ||
        glGetError() != GL_NO_ERROR) {
        discardImport();
        return fail("copy framebuffer/target allocation");
    }
    glViewport(0, 0, width, height);
    glDisable(GL_BLEND);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_STENCIL_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_RASTERIZER_DISCARD);
    glDisable(GL_DITHER);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glUseProgram(projectionImportProgram_);
    glUniform1i(projectionImportSourceUniform_, 0);
    glBindVertexArray(projectionImportVertexArray_);
    glBindTexture(GL_TEXTURE_2D, import.texture);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    if (glGetError() != GL_NO_ERROR) {
        glFinish();
        discardImport();
        return fail("copy draw");
    }
    import.completion = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
    if (import.completion == nullptr) {
        // Only the error path blocks. Normally completed imports are reclaimed
        // by a zero-timeout fence query on subsequent frames.
        glFinish();
        discardImport();
        // The import is gone, so this frame is already safe to reuse.
        notifyProducer();
    } else {
        // Ownership of the handshake moves into the import list; the callback runs
        // in releaseProjectionImports() once the fence for this flip pass signals.
        import.onGpuReadComplete = std::move(readComplete);
        glFlush();
    }
    projectionReady_ = true;
    projectionFrameSeen_ = true;
    projectionFrameStamp_ = std::chrono::steady_clock::now();
    ++projectionFrameCount_;
    ++projectionHardwareFrameCount_;
    const std::int64_t importedAtNs = MonotonicNanoseconds();
    // Source timestamps can belong to a different vendor time domain. Never
    // report a negative or implausibly old value as a valid transport latency.
    const std::int64_t ageNs = importedAtNs - sourceTimestampNs;
    framePerf_.sourceTimestampNanoseconds = sourceTimestampNs;
    framePerf_.sourceTimestampValid = sourceTimestampNs > 0 && ageNs >= 0 && ageNs < 10000000000LL;
    framePerf_.sourceToImportMilliseconds = framePerf_.sourceTimestampValid ? ageNs * 1.0e-6 : 0.0;
    return true;
}

// ★ 一次性诊断：把"世界底层"纹理（= 采集到的、我们图层之下的屏幕内容）落成 PPM。
//
// 为什么需要它：整个修复的正确性都建立在"世界底层里没有我们自己的画面"之上。
// 如果它其实包含我们上一帧的输出，那么凡是本进程帧缓冲透明的像素，玻璃就会以
// 增益 1 采到自己 —— 那是无限镜像。这一点**无法从截图判断**（玻璃本来就长得像
// 桌面），只能直接把采集纹理读出来看。
//
// 由环境变量触发，跑一次就结束，不影响正常帧：
//   AURORA_DUMP_BASE=/data/local/tmp/base.ppm   （默认在第 240 帧落盘）
//   AURORA_DUMP_BASE_FRAME=120                  （可选：改帧号）
//   AURORA_DUMP_BASE_STEP=4                     （可选：降采样步长，默认 4）
void GlesBackend::maybeDumpWorldBase() {
    static const char* path = ::getenv("AURORA_DUMP_BASE");
    if (path == nullptr || path[0] == '\0' || projectionTexture_ == 0) return;
    static int targetFrame = -1;
    if (targetFrame < 0) {
        const char* raw = ::getenv("AURORA_DUMP_BASE_FRAME");
        targetFrame = raw != nullptr ? std::max(1, std::atoi(raw)) : 240;
    }
    static int step = -1;
    if (step < 0) {
        const char* raw = ::getenv("AURORA_DUMP_BASE_STEP");
        step = raw != nullptr ? std::clamp(std::atoi(raw), 1, 16) : 4;
    }
    static int frames = 0;
    if (++frames != targetFrame) return;
    if (projectionWidth_ <= 0 || projectionHeight_ <= 0) return;

    // glReadPixels 需要 FBO 附件：临时挂一个，读完立刻拆掉，不改任何持久状态。
    GLuint fbo = 0;
    glGenFramebuffers(1, &fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                           projectionTexture_, 0);
    const bool complete =
        glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
    const int outW = std::max(1, projectionWidth_ / step);
    const int outH = std::max(1, projectionHeight_ / step);
    std::vector<std::uint8_t> pixels;
    bool readOk = false;
    if (complete) {
        // 逐行读再抽样，避免整帧 4K 读回。
        std::vector<std::uint8_t> row(std::size_t(projectionWidth_) * 4u);
        pixels.assign(std::size_t(outW) * std::size_t(outH) * 3u, 0u);
        readOk = true;
        for (int y = 0; y < outH; ++y) {
            const int srcY = y * step;
            glReadPixels(0, srcY, projectionWidth_, 1, GL_RGBA, GL_UNSIGNED_BYTE,
                         row.data());
            for (int x = 0; x < outW; ++x) {
                const std::size_t s = std::size_t(x * step) * 4u;
                const std::size_t d = (std::size_t(y) * std::size_t(outW) +
                                       std::size_t(x)) * 3u;
                pixels[d + 0] = row[s + 0];
                pixels[d + 1] = row[s + 1];
                pixels[d + 2] = row[s + 2];
            }
        }
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glDeleteFramebuffers(1, &fbo);

    if (readOk) {
        if (FILE* file = ::fopen(path, "wb")) {
            std::fprintf(file, "P6\n%d %d\n255\n", outW, outH);
            std::fwrite(pixels.data(), 1, pixels.size(), file);
            ::fclose(file);
            AURORA_LOG(ANDROID_LOG_INFO, kLogTag,
                                "world base dumped: %s %dx%d (source %dx%d)",
                                path, outW, outH, projectionWidth_, projectionHeight_);
        }
    }
}

bool GlesBackend::blurBackground() {
    if (!backgroundReady_ || blurProgram_ == 0 || blurFramebuffer_ == 0 || blurFramebufferPing_ == 0) return false;
    const GLsizei blurWidth = std::max(1, width_ / 2);
    const GLsizei blurHeight = std::max(1, height_ / 2);
    glDisable(GL_BLEND); glDisable(GL_DEPTH_TEST); glDisable(GL_SCISSOR_TEST);
    glUseProgram(blurProgram_);
    glActiveTexture(GL_TEXTURE0);
    glUniform1i(glGetUniformLocation(blurProgram_, "uSource"), 0);
    const GLint texel = glGetUniformLocation(blurProgram_, "uTexel");
    const GLint direction = glGetUniformLocation(blurProgram_, "uDirection");
    const GLint downsample = glGetUniformLocation(blurProgram_, "uDownsample");
    glViewport(0, 0, blurWidth, blurHeight);
    glBindFramebuffer(GL_FRAMEBUFFER, blurFramebuffer_);
    glBindTexture(GL_TEXTURE_2D, backgroundTexture_);
    glUniform2f(texel, 1.0f / width_, 1.0f / height_);
    glUniform2f(direction, 0.0f, 0.0f);
    glUniform1i(downsample, 1);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindFramebuffer(GL_FRAMEBUFFER, blurFramebufferPing_);
    glBindTexture(GL_TEXTURE_2D, blurTexture_);
    glUniform2f(texel, 1.0f / blurWidth, 1.0f / blurHeight);
    glUniform2f(direction, 1.0f, 0.0f);
    glUniform1i(downsample, 0);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindFramebuffer(GL_FRAMEBUFFER, blurFramebuffer_);
    glBindTexture(GL_TEXTURE_2D, blurTexturePing_);
    glUniform2f(direction, 0.0f, 1.0f);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glBindTexture(GL_TEXTURE_2D, 0);
    return glGetError() == GL_NO_ERROR;
}

void GlesBackend::resize(int width, int height) {
    const int nextWidth = width > 0 ? width : 1;
    const int nextHeight = height > 0 ? height : 1;
    if (nextWidth == width_ && nextHeight == height_) {
        return;
    }
    // EGL's viewport cannot resize the Android producer buffer by itself.
    // Perform this on the render thread before rendering the new orientation.
    if (nativeWindow_ && ANativeWindow_setBuffersGeometry(nativeWindow_, nextWidth, nextHeight, 0) != 0) {
        LogError("native window buffer resize failed");
        shutdown();
        return;
    }
    width_ = nextWidth;
    height_ = nextHeight;
    if (imguiReady_) {
        destroyBackgroundResources();
        if (!transparentSurface_.load(std::memory_order_acquire)) createBackgroundResources();
    }
}

void GlesBackend::beginFrame(float deltaSeconds) {
    (void)deltaSeconds;
    releaseProjectionImports(false);
    pollPresentTimestamps();
    pollGpuTimers();
    if (transparentSurface_.load(std::memory_order_acquire)) {
        if (backgroundTexture_ != 0) destroyBackgroundResources();
    } else if (!backgroundReady_) {
        createBackgroundResources();
    }
    ImGui_ImplOpenGL3_NewFrame();
}

void GlesBackend::submit(ImDrawData* drawData) {
    activeGpuTimer_ = -1;
    if (framePerf_.gpuTimerSupported) {
        for (int index = 0; index < 4; ++index) {
            if (!gpuTimers_[index].pending) {
                activeGpuTimer_ = index;
                glBeginQuery(GL_TIME_ELAPSED_EXT, gpuTimers_[index].query);
                break;
            }
        }
    }
    glViewport(0, 0, width_, height_);
    const bool transparent = transparentSurface_.load(std::memory_order_acquire);
    // Establish a fresh host-scene image before sampling it. Capturing the
    // previous default framebuffer here feeds the UI back into the glass on
    // the next frame and produces recursive afterimages.
    if (transparent) {
        glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    } else {
        glClearColor(0.035f, 0.045f, 0.07f, 1.0f);
    }
    glClear(GL_COLOR_BUFFER_BIT);
    if (!transparent && backgroundReady_ && backgroundTexture_ != 0) {
        glBindTexture(GL_TEXTURE_2D, backgroundTexture_);
        glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 0, 0, width_, height_);
        glBindTexture(GL_TEXTURE_2D, 0);
        blurBackground();
    }
    // ★ 世界底层：把我们图层**之下**的屏幕内容交给逐面板场景合成器。
    //   采集纹理本身仍然是"整屏画面"，但它不再被当成玻璃的背景 —— 它只是
    //   图层栈最下面那一层。玻璃要采样的是"这一层 + 本进程已绘制的下层"。
    GlesSceneCompositor* compositor = nullptr;
    if (transparent) {
        compositor = InstallGlesSceneCompositor();
        WorldBaseSource base;
        const bool baseUsable = projectionReady_ && projectionTexture_ != 0 &&
                                !projectionSourceIsStale();
        if (baseUsable) {
            base.texture = projectionTexture_;
            base.storageWidth = projectionWidth_;
            base.storageHeight = projectionHeight_;
            base.ready = true;
        }
        if (compositor != nullptr) compositor->setWorldBase(base);
        // ★ 全屏接管：把世界底层不透明地铺满整屏，让窗口内外走**同一条时间线**。
        //   必须在 ImGui 绘制之前做（玻璃与 UI 都画在它上面）。
        //   世界底层不可用时 base.ready=false ⇒ 这里直接跳过，图层退回透明覆盖层，
        //   屏幕回到"直出"，不会黑屏也不会显示冻结画面。
        if (compositor != nullptr && base.ready && FullScreenTakeoverEnabled()) {
            if (!compositor->composeFullScreen(width_, height_, worldComposeRegionValid_ ? worldComposeRegion_ : nullptr)) {
                static bool warned = false;
                if (!warned) {
                    warned = true;
                    AURORA_LOG(ANDROID_LOG_WARN, kLogTag,
                                        "全屏接管：世界底层合成失败，本帧退回透明覆盖层");
                }
            }
        }
    }
    glViewport(0, 0, width_, height_);
    ImGui_ImplOpenGL3_RenderDrawData(drawData);
    maybeDumpWorldBase();
    if (activeGpuTimer_ >= 0) {
        glEndQuery(GL_TIME_ELAPSED_EXT);
        gpuTimers_[activeGpuTimer_].pending = true;
        activeGpuTimer_ = -1;
    }
}

void GlesBackend::endFrame() {
    if (display_ != EGL_NO_DISPLAY && surface_ != EGL_NO_SURFACE) {
        EGLuint64KHR frameId = 0;
        const bool tracked = framePerf_.presentTimestampsSupported &&
            getNextFrameId_(display_, surface_, &frameId) == EGL_TRUE;
        if (eglSwapBuffers(display_, surface_) == EGL_TRUE) {
            ++framePerf_.submittedFrames;
            if (tracked) pendingPresentFrameIds_.push_back(
                {frameId, framePerf_.sourceTimestampValid ? framePerf_.sourceTimestampNanoseconds : 0});
        }
        // Poll immediately after display backpressure: leases can return before
        // the next render deadline rather than occupying an extra frame slot.
        releaseProjectionImports(false);
        pollPresentTimestamps();
    }
}

void GlesBackend::shutdown() {
    if (hasCurrentContext()) {
        for (auto& timer : gpuTimers_) {
            if (timer.query) glDeleteQueries(1, &timer.query);
        }
    }
    for (auto& timer : gpuTimers_) timer = {};
    activeGpuTimer_ = -1;
    pendingPresentFrameIds_.clear();
    getNextFrameId_ = nullptr;
    getFrameTimestamps_ = nullptr;
    if (imguiReady_) {
        ImGui_ImplOpenGL3_Shutdown();
        imguiReady_ = false;
    }
    if (imguiContextReady_) {
        ImGui::DestroyContext();
        imguiContextReady_ = false;
    }
    // Every GL object must be released while the owning context is still
    // current: a Surface rebuild reuses the same GlesBackend instance.
    if (GlesSceneCompositor* compositor = ActiveGlesSceneCompositor()) {
        compositor->clearWorldBase();
        compositor->destroy();
    }
    destroyBackgroundResources();
    destroyProjectionTexture();
    destroyNeutralBackdrop();
    destroyGraphics();
    framePerf_ = {};
}

void GlesBackend::destroyGraphics() {
    if (display_ != EGL_NO_DISPLAY) {
        eglMakeCurrent(display_, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        if (surface_ != EGL_NO_SURFACE) {
            eglDestroySurface(display_, surface_);
        }
        if (context_ != EGL_NO_CONTEXT) {
            eglDestroyContext(display_, context_);
        }
        eglTerminate(display_);
    }
    display_ = EGL_NO_DISPLAY;
    surface_ = EGL_NO_SURFACE;
    context_ = EGL_NO_CONTEXT;
    nativeWindow_ = nullptr;
}

}  // namespace aurora
