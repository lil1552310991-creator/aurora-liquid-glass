#include "AuroraLog.h"
#include "GlesSceneCompositor.h"

#include "SceneComposite.h"
#include "PanelEfficiencyStats.h"

#include <GLES3/gl3.h>

#include <android/log.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace aurora {
namespace {

[[maybe_unused]] constexpr char kLogTag[] = "Aurora";

const char* kVertexSource =
    "#version 300 es\n"
    "layout(location = 0) in vec2 aPos;\n"
    "layout(location = 1) in vec2 aUv;\n"
    "out vec2 vUv01;\n"
    "void main() {\n"
    "    vUv01 = aUv;\n"
    "    gl_Position = vec4(aPos, 0.0, 1.0);\n"
    "}\n";

// 采样世界底层对应的那一块，其余交给混合方程：
//   rgb   = world.rgb * (1 - dst.a) + dst.rgb
//   alpha = 1
// 混合因子见 compose() 里的 glBlendFuncSeparate。
const char* kFragmentSource =
    "#version 300 es\n"
    "precision highp float;\n"
    "uniform sampler2D uWorld;\n"
    "uniform vec4 uUvRect;\n"
    "in vec2 vUv01;\n"
    "out vec4 fragColor;\n"
    "void main() {\n"
    "    vec2 uv = mix(uUvRect.xy, uUvRect.zw, vUv01);\n"
    "    vec3 world = texture(uWorld, uv).rgb;\n"
    "    fragColor = vec4(world, 1.0);\n"
    "}\n";

// 覆盖整个视口的四边形：pos(NDC) + uv01（y 向上，与 GL 的帧缓冲行序一致）。
constexpr float kQuad[] = {
    -1.0f, -1.0f, 0.0f, 0.0f,
     1.0f, -1.0f, 1.0f, 0.0f,
    -1.0f,  1.0f, 0.0f, 1.0f,
     1.0f,  1.0f, 1.0f, 1.0f,
};

unsigned int compileStage(GLenum type, const char* source) {
    const GLuint shader = glCreateShader(type);
    if (shader == 0) return 0;
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);
    GLint status = GL_FALSE;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &status);
    if (status != GL_TRUE) {
        char log[512] = {};
        glGetShaderInfoLog(shader, sizeof(log) - 1, nullptr, log);
        AURORA_LOG(ANDROID_LOG_ERROR, kLogTag, "scene compositor shader: %s", log);
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

GlesSceneCompositor* g_active = nullptr;

// 诊断开关（默认关）：`AURORA_NO_SCENE_COMPOSE=1` 让世界底层合成整段跳过。
// 存在的唯一理由是**在真机上把"合成这一趟"的成本单独量出来** —— 关掉它画面
// 会明显不对（玻璃里看不到桌面），所以绝不能拿它当正常模式。
bool SkipComposeForDiagnostics() {
    static const bool skip = [] {
        const char* value = std::getenv("AURORA_NO_SCENE_COMPOSE");
        return value != nullptr && value[0] == '1';
    }();
    return skip;
}

// ytbl 的 C 函数指针钩子 —— 转发到当前实例。
bool SceneComposerThunk(void* user, const lgx::PanelSampleRect& rect,
                        unsigned int captureTexture, int captureStrideW,
                        int captureStrideH, int viewWidth, int viewHeight) {
    auto* self = static_cast<GlesSceneCompositor*>(user);
    if (self == nullptr) return false;
    return self->compose(rect, captureTexture, captureStrideW, captureStrideH, viewWidth,
                         viewHeight);
}

}  // namespace

GlesSceneCompositor::~GlesSceneCompositor() {
    // 这里**故意**不调用 GL：析构点可能已经没有 current 上下文。
    // 拥有者必须显式 destroy()（见 GlesBackend::shutdown）。
}

bool GlesSceneCompositor::ensureResources() {
    if (program_ != 0 && vao_ != 0 && vbo_ != 0) return true;
    if (program_ != 0) {
        glDeleteProgram(program_);
        program_ = 0;
    }
    if (vbo_ != 0) {
        glDeleteBuffers(1, &vbo_);
        vbo_ = 0;
    }
    if (vao_ != 0) {
        glDeleteVertexArrays(1, &vao_);
        vao_ = 0;
    }

    const GLuint vs = compileStage(GL_VERTEX_SHADER, kVertexSource);
    const GLuint fs = compileStage(GL_FRAGMENT_SHADER, kFragmentSource);
    if (vs == 0 || fs == 0) {
        if (vs != 0) glDeleteShader(vs);
        if (fs != 0) glDeleteShader(fs);
        return false;
    }
    GLuint program = glCreateProgram();
    glAttachShader(program, vs);
    glAttachShader(program, fs);
    glLinkProgram(program);
    glDeleteShader(vs);
    glDeleteShader(fs);
    GLint linked = GL_FALSE;
    glGetProgramiv(program, GL_LINK_STATUS, &linked);
    if (linked != GL_TRUE) {
        char log[512] = {};
        glGetProgramInfoLog(program, sizeof(log) - 1, nullptr, log);
        AURORA_LOG(ANDROID_LOG_ERROR, kLogTag, "scene compositor link: %s", log);
        glDeleteProgram(program);
        return false;
    }
    program_ = program;
    sourceUniform_ = glGetUniformLocation(program_, "uWorld");
    uvRectUniform_ = glGetUniformLocation(program_, "uUvRect");

    glGenVertexArrays(1, &vao_);
    glGenBuffers(1, &vbo_);
    if (vao_ == 0 || vbo_ == 0) {
        destroy();
        return false;
    }
    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferData(GL_ARRAY_BUFFER, sizeof(kQuad), kQuad, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float),
                          reinterpret_cast<const void*>(0));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float),
                          reinterpret_cast<const void*>(2 * sizeof(float)));
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);
    return program_ != 0 && vao_ != 0 && vbo_ != 0;
}

bool GlesSceneCompositor::ensureFramebuffer(unsigned int captureTexture,
                                           int storageWidth, int storageHeight) {
    if (framebuffer_ == 0) {
        glGenFramebuffers(1, &framebuffer_);
        if (framebuffer_ == 0) return false;
        attachedTexture_ = 0;
        framebufferComplete_ = false;
    }
    glBindFramebuffer(GL_FRAMEBUFFER, framebuffer_);
    if (attachedTexture_ != captureTexture) {
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                               captureTexture, 0);
        attachedTexture_ = captureTexture;
        framebufferComplete_ = false;
    }
    // ytbl keeps the capture texture name while reallocating its storage. The
    // attachment must be revalidated when either allocated dimension changes.
    if (attachedWidth_ != storageWidth || attachedHeight_ != storageHeight) {
        attachedWidth_ = storageWidth;
        attachedHeight_ = storageHeight;
        framebufferComplete_ = false;
    }
    static const bool cacheStatus = [] {
        const char* value = std::getenv("AURORA_SCENE_FBO_CACHE");
        return value == nullptr || value[0] != '0' || value[1] != '\0';
    }();
    if (cacheStatus && framebufferComplete_) return true;
    panel_efficiency::framebufferChecks.fetch_add(1, std::memory_order_relaxed);
    framebufferComplete_ = glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
    if (!framebufferComplete_) {
        ++fboFailureCount_;
        panel_efficiency::framebufferFailures.fetch_add(1, std::memory_order_relaxed);
        return false;
    }
    return true;
}

bool GlesSceneCompositor::compose(const lgx::PanelSampleRect& rect,
                                  unsigned int captureTexture, int captureStrideW,
                                  int captureStrideH, int viewWidth, int viewHeight) {
    if (SkipComposeForDiagnostics()) {
        ++skippedNoBaseCount_;
        return false;
    }
    // 本帧已经全屏铺过世界底层 ⇒ 帧缓冲处处不透明，逐面板再垫一次是纯浪费。
    // 返回 false 让 ytbl 用它刚抠出来的帧缓冲内容（那里面已经含世界底层）。
    if (fullScreenActive_) {
        ++skippedNoBaseCount_;
        return false;
    }
    if (captureTexture == 0 || viewWidth <= 0 || viewHeight <= 0) return false;
    if (!base_.ready || base_.texture == 0) {
        // 没有世界底层时保持 ytbl 原生行为（只采样帧缓冲），这比采样一张过期
        // 或错误的全屏图要诚实得多。
        ++skippedNoBaseCount_;
        return false;
    }

    // 采样区域就是 ytbl 刚抠出来的那块 tile，坐标在宿主屏幕空间（y 向下）。
    const int x0 = static_cast<int>(std::floor(rect.left));
    const int y0 = static_cast<int>(std::floor(rect.top));
    int w = static_cast<int>(std::ceil(rect.right)) - x0;
    int h = static_cast<int>(std::ceil(rect.bottom)) - y0;
    if (w <= 0 || h <= 0) return false;
    // glCaptureRegion 已把区域夹进视图并限制在分配尺寸内，这里再做一次兜底。
    if (x0 < 0 || y0 < 0) return false;
    if (x0 + w > viewWidth) w = viewWidth - x0;
    if (y0 + h > viewHeight) h = viewHeight - y0;
    if (w <= 0 || h <= 0) return false;
    if (w > captureStrideW || h > captureStrideH) return false;

    if (!ensureResources()) return false;

    // ---- 保存 GL 状态（这是 ImGui 绘制回调内部，状态是宿主后端的实时状态） ----
    GLint prevFramebuffer = 0;
    GLint prevViewport[4] = {0, 0, 0, 0};
    GLint prevProgram = 0;
    GLint prevVao = 0;
    GLint prevActiveTexture = GL_TEXTURE0;
    GLint prevTexture0 = 0;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFramebuffer);
    glGetIntegerv(GL_VIEWPORT, prevViewport);
    glGetIntegerv(GL_CURRENT_PROGRAM, &prevProgram);
    glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &prevVao);
    glGetIntegerv(GL_ACTIVE_TEXTURE, &prevActiveTexture);
    glActiveTexture(GL_TEXTURE0);
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &prevTexture0);
    const GLboolean prevBlend = glIsEnabled(GL_BLEND);
    const GLboolean prevScissor = glIsEnabled(GL_SCISSOR_TEST);
    const GLboolean prevDepth = glIsEnabled(GL_DEPTH_TEST);
    const GLboolean prevCull = glIsEnabled(GL_CULL_FACE);
    const GLboolean prevStencil = glIsEnabled(GL_STENCIL_TEST);
    GLint prevBlendSrcRgb = GL_ONE, prevBlendDstRgb = GL_ZERO;
    GLint prevBlendSrcAlpha = GL_ONE, prevBlendDstAlpha = GL_ZERO;
    glGetIntegerv(GL_BLEND_SRC_RGB, &prevBlendSrcRgb);
    glGetIntegerv(GL_BLEND_DST_RGB, &prevBlendDstRgb);
    glGetIntegerv(GL_BLEND_SRC_ALPHA, &prevBlendSrcAlpha);
    glGetIntegerv(GL_BLEND_DST_ALPHA, &prevBlendDstAlpha);

    if (!ensureFramebuffer(captureTexture, captureStrideW, captureStrideH)) {
        glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(prevFramebuffer));
        return false;
    }

    glViewport(0, 0, w, h);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_STENCIL_TEST);
    glEnable(GL_BLEND);
    // rgb   = world.rgb * (1 - dst.a) + dst.rgb        ← 把世界垫在本进程已绘制内容之下
    // alpha = 1        * 1            + dst.a * 0      ← 世界是不透明的
    glBlendFuncSeparate(GL_ONE_MINUS_DST_ALPHA, GL_ONE, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);

    glUseProgram(program_);
    glBindVertexArray(vao_);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, base_.texture);
    glUniform1i(sourceUniform_, 0);

    // tile 第 0 行 = 区域最下一行；世界底层的 V=1 = 屏幕最上一行（与 ytbl
    // Shaders.cpp 的 sceneUv 同一约定）。半像素对齐的推导见 SceneComposite.h。
    const float u0 = WorldBaseU(static_cast<float>(x0), viewWidth);
    const float u1 = WorldBaseU(static_cast<float>(x0 + w), viewWidth);
    const float vBottom = WorldBaseV(static_cast<float>(y0 + h), viewHeight);
    const float vTop = WorldBaseV(static_cast<float>(y0), viewHeight);
    glUniform4f(uvRectUniform_, u0, vBottom, u1, vTop);

    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

    // ---- 还原 GL 状态 ----
    glBindVertexArray(static_cast<GLuint>(prevVao));
    glUseProgram(static_cast<GLuint>(prevProgram));
    glActiveTexture(static_cast<GLenum>(prevActiveTexture));
    if (prevActiveTexture == GL_TEXTURE0) {
        glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(prevTexture0));
    } else {
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(prevTexture0));
        glActiveTexture(static_cast<GLenum>(prevActiveTexture));
    }
    glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(prevFramebuffer));
    glViewport(prevViewport[0], prevViewport[1], prevViewport[2], prevViewport[3]);
    glBlendFuncSeparate(static_cast<GLenum>(prevBlendSrcRgb),
                        static_cast<GLenum>(prevBlendDstRgb),
                        static_cast<GLenum>(prevBlendSrcAlpha),
                        static_cast<GLenum>(prevBlendDstAlpha));
    if (prevBlend) glEnable(GL_BLEND); else glDisable(GL_BLEND);
    if (prevScissor) glEnable(GL_SCISSOR_TEST); else glDisable(GL_SCISSOR_TEST);
    if (prevDepth) glEnable(GL_DEPTH_TEST); else glDisable(GL_DEPTH_TEST);
    if (prevCull) glEnable(GL_CULL_FACE); else glDisable(GL_CULL_FACE);
    if (prevStencil) glEnable(GL_STENCIL_TEST); else glDisable(GL_STENCIL_TEST);

    ++composedCount_;
    panel_efficiency::sceneComposes.fetch_add(1, std::memory_order_relaxed);
    lastWidth_ = w;
    lastHeight_ = h;
    return true;
}

bool GlesSceneCompositor::composeFullScreen(int viewWidth, int viewHeight, const float region[4]) {
    if (viewWidth <= 0 || viewHeight <= 0) return false;
    if (!base_.ready || base_.texture == 0) return false;  // 没底层 ⇒ 保持透明覆盖层
    if (!ensureResources()) return false;

    GLint prevFramebuffer = 0;
    GLint prevViewport[4] = {0, 0, 0, 0};
    GLint prevProgram = 0;
    GLint prevVao = 0;
    GLint prevActiveTexture = GL_TEXTURE0;
    GLint prevTexture0 = 0;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFramebuffer);
    glGetIntegerv(GL_VIEWPORT, prevViewport);
    glGetIntegerv(GL_CURRENT_PROGRAM, &prevProgram);
    glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &prevVao);
    glGetIntegerv(GL_ACTIVE_TEXTURE, &prevActiveTexture);
    glActiveTexture(GL_TEXTURE0);
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &prevTexture0);
    const GLboolean prevBlend = glIsEnabled(GL_BLEND);
    const GLboolean prevScissor = glIsEnabled(GL_SCISSOR_TEST);
    const GLboolean prevDepth = glIsEnabled(GL_DEPTH_TEST);
    const GLboolean prevCull = glIsEnabled(GL_CULL_FACE);
    const GLboolean prevStencil = glIsEnabled(GL_STENCIL_TEST);

    glViewport(0, 0, viewWidth, viewHeight);
    // ★ 部分接管：region 非空时只在这个矩形内铺世界底层。
    //   region 用**左上原点**的像素坐标（与 UI 一致），GL 的 scissor 是左下原点，
    //   所以要把 y 翻一次。区域完全在屏幕外时返回 false，让调用方本帧退回透明覆盖层。
    if (region != nullptr) {
        // region 非空但面积为 0 ⇒ 调用方明确表示"本帧没有需要铺的面板"，不铺。
        if (region[2] <= 0.0f || region[3] <= 0.0f) {
            return false;
        }
        const int sx = static_cast<int>(std::floor(region[0]));
        const int sy = static_cast<int>(std::floor(region[1]));
        const int sw = static_cast<int>(std::ceil(region[2]));
        const int sh = static_cast<int>(std::ceil(region[3]));
        const int clippedX = std::max(0, sx);
        const int clippedY = std::max(0, sy);
        const int clippedW = std::min(viewWidth, sx + sw) - clippedX;
        const int clippedH = std::min(viewHeight, sy + sh) - clippedY;
        if (clippedW <= 0 || clippedH <= 0) {
            return false;
        }
        glEnable(GL_SCISSOR_TEST);
        glScissor(clippedX, viewHeight - clippedY - clippedH, clippedW, clippedH);
        ++partialComposeCount_;
    } else {
        glDisable(GL_SCISSOR_TEST);
    }
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_STENCIL_TEST);
    // 不透明写入：整块屏幕都由世界底层构成（fragment shader 已固定输出 alpha=1）。
    glDisable(GL_BLEND);

    glUseProgram(program_);
    glBindVertexArray(vao_);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, base_.texture);
    glUniform1i(sourceUniform_, 0);
    // 与 compose() 同一套约定：屏幕 y 向下 ⇒ v = (mappingH - screenY) / mappingH。
    glUniform4f(uvRectUniform_,
                WorldBaseU(0.0f, viewWidth),
                WorldBaseV(static_cast<float>(viewHeight), viewHeight),
                WorldBaseU(static_cast<float>(viewWidth), viewWidth),
                WorldBaseV(0.0f, viewHeight));
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

    glBindVertexArray(static_cast<GLuint>(prevVao));
    glUseProgram(static_cast<GLuint>(prevProgram));
    glActiveTexture(static_cast<GLenum>(prevActiveTexture));
    if (prevActiveTexture == GL_TEXTURE0) {
        glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(prevTexture0));
    } else {
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(prevTexture0));
        glActiveTexture(static_cast<GLenum>(prevActiveTexture));
    }
    glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(prevFramebuffer));
    glViewport(prevViewport[0], prevViewport[1], prevViewport[2], prevViewport[3]);
    if (prevBlend) glEnable(GL_BLEND); else glDisable(GL_BLEND);
    if (prevScissor) glEnable(GL_SCISSOR_TEST); else glDisable(GL_SCISSOR_TEST);
    if (prevDepth) glEnable(GL_DEPTH_TEST); else glDisable(GL_DEPTH_TEST);
    if (prevCull) glEnable(GL_CULL_FACE); else glDisable(GL_CULL_FACE);
    if (prevStencil) glEnable(GL_STENCIL_TEST); else glDisable(GL_STENCIL_TEST);

    ++fullScreenCount_;
    fullScreenActive_ = true;
    lastWidth_ = viewWidth;
    lastHeight_ = viewHeight;
    return true;
}

void GlesSceneCompositor::destroy() {
    if (framebuffer_ != 0) {
        glDeleteFramebuffers(1, &framebuffer_);
        framebuffer_ = 0;
    }
    attachedTexture_ = 0;
    attachedWidth_ = 0;
    attachedHeight_ = 0;
    framebufferComplete_ = false;
    if (vbo_ != 0) {
        glDeleteBuffers(1, &vbo_);
        vbo_ = 0;
    }
    if (vao_ != 0) {
        glDeleteVertexArrays(1, &vao_);
        vao_ = 0;
    }
    if (program_ != 0) {
        glDeleteProgram(program_);
        program_ = 0;
    }
    sourceUniform_ = -1;
    uvRectUniform_ = -1;
    base_ = WorldBaseSource{};
}

GlesSceneCompositor* InstallGlesSceneCompositor() {
    static GlesSceneCompositor instance;
    if (g_active != &instance) {
        lgx::setSceneComposer(&SceneComposerThunk, &instance);
        g_active = &instance;
    }
    return g_active;
}

GlesSceneCompositor* ActiveGlesSceneCompositor() { return g_active; }

}  // namespace aurora
