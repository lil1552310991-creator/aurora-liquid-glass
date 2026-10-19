#include "RenderDamage.h"

// Keep this test independent of GLES/Android while exercising the production
// panel snapshot, including its private job queue and panel-to-shade mapping.
#include "third_party/liquidglass/ytbl/GlassPanel.cpp"

#include <algorithm>
#include <cassert>
#include <cstdio>
#include <limits>

namespace lgx {
float clampRange(float value, float low, float high) {
    return std::min(std::max(value, low), high);
}
float px(float value) { return value; }
Rgba rgbaOf(unsigned int value) {
    return Rgba(float((value >> 16) & 255) / 255.0f,
                float((value >> 8) & 255) / 255.0f,
                float(value & 255) / 255.0f, float(value >> 24) / 255.0f);
}
namespace detail {
bool backdropEnabledFlag() { return true; }
float densityScale() { return 1.0f; }
float uniformPixelScale() { return 1.0f; }
float tintBrightness() { return 0.0f; }
float tintContrast() { return 1.0f; }
float tintSaturation() { return 1.0f; }
Rgba tintRefract() { return Rgba(); }
Box viewBounds() { return Box(0, 0, 1080, 2376); }
const Surface& surfaceRef() { static const Surface surface{}; return surface; }
bool glReady() { return true; }
bool glGatherReady() { return true; }
void glPaintPanel(const PanelJob&, const Box&, unsigned int, const GatherInfo&) {}
bool glCaptureRegion(int, int, int, int, int, int,
                     float&, float&, float&, float&, float&, float&) { return false; }
unsigned int glCaptureTexture() { return 0; }
int glCaptureWidth() { return 0; }
int glCaptureHeight() { return 0; }
bool glRunGather(int, int, unsigned int, int, int, float) { return false; }
GatherInfo glGatherInfo() { return GatherInfo(); }
}  // namespace detail
}  // namespace lgx

namespace {
int checks = 0;
void Check(bool value) { ++checks; assert(value); }

aurora::RenderDamageState State() {
    aurora::RenderDamageState state;
    state.width = 1080;
    state.height = 2376;
    state.backgroundGeneration = 6;
    // Text is already represented by vertices in the real draw-data snapshot.
    const char text[] = "original caption";
    aurora::AppendRenderDamageBytes(state.bytes, text, sizeof(text));
    lgx::appendPanelVisualSnapshot(state.bytes);
    return state;
}

void ExactPresentationComparison() {
    aurora::RenderDamage damage;
    const auto state = State();
    Check(damage.changed(state));
    damage.commit(state);
    Check(!damage.changed(state));
    auto next = state;
    next.backgroundGeneration++;
    Check(damage.changed(next));
    next = state;
    next.width++;
    Check(damage.changed(next));
    next = state;
    next.height++;
    Check(damage.changed(next));
    next = state;
    next.bytes[0] ^= 1;  // Changed text with identical panel index/count.
    Check(damage.changed(next));
    next = state;
    next.bytes.push_back(0);  // Prefix equality must not suppress extra geometry.
    Check(damage.changed(next));
    damage.invalidate();
    Check(damage.changed(state));
    damage.commit(state);
    Check(!damage.changed(state));
    // A candidate that was built but not presented must never become the key.
    next = state;
    next.bytes[1] ^= 1;
    Check(damage.changed(next));
    Check(damage.changed(next));
    Check(!damage.changed(state));
}

void ActualPanelInputs() {
    using namespace lgx::detail;
    resetJobs();
    lgx::PanelStyle style;
    style.shadowOn = true;
    style.shadow.alpha = 0.5f;
    Check(lgx::submitPanel(style, lgx::Box(20, 20, 350, 180)) == 0);
    Check(g_jobCount == 2);
    const auto baseline = State();
    // Each group below affects actual shader or sample inputs without changing
    // ImDrawData geometry. They all must wake the GPU.
    auto& job = g_jobs[g_panelJob[0]];
    const auto saved = job;
    const auto changed = [&] { Check(!(State() == baseline)); job = saved; };
    job.lensHeight += 1; changed();
    job.lensAmount += 1; changed();
    job.lensDepth += 1; changed();
    job.spectral += 0.001f; changed();
    job.blurRadius += 1; changed();
    job.panelOpacity -= 0.001f; changed();
    job.darkGuard += 0.001f; changed();
    job.toneCtrl[2] += 0.001f; changed();
    job.refractTint.g += 0.001f; changed();
    job.surface.a += 0.001f; changed();
    job.surfaceMode += 1; changed();
    job.corners.bottomRight += 1; changed();
    job.frame.right += 1; changed();
    job.pixelScaleY += 0.001f; changed();
    job.sampleOriginX += 1; changed();
    job.sampleSizeY += 1; changed();
    job.sampleStrideX += 1; changed();
    job.dropRect[0] += 1; changed();
    job.feedMerge += 1; changed();
    job.strandRound += 1; changed();
    job.edgeTint.r += 0.001f; changed();
    job.edgeWidth += 1; changed();
    job.insetAlpha += 0.001f; changed();
    job.glowX += 1; changed();
    job.glowLevel += 0.001f; changed();
    job.sceneOn += 1; changed();
    job.solidBackdrop += 1; changed();
    job.stretchY += 0.001f; changed();
    job.absentTint.a += 0.001f; changed();
    job.trackRect[3] += 1; changed();
    job.trackTint.g += 0.001f; changed();
    job.shadeTint.a += 0.001f; changed();
    job.shadeReach += 1; changed();
    job.shadeIndex = -2; changed();
    // Tiny changes must not be hidden by rounding or an epsilon comparison.
    job.blurRadius = std::nextafter(job.blurRadius, std::numeric_limits<float>::infinity());
    changed();

    job.sampleReady = true;
    job.gatherActive = true;
    job.gatherPitchX = 17.0f;
    job.gatherPitchY = 18.0f;
    job.gatherRows = 19.0f;
    job.gatherTexelX = 20.0f;
    job.gatherTexelY = 21.0f;
    job.gatherLimitX = 22.0f;
    job.gatherLimitY = 23.0f;
    job.shadePainted = true;
    Check(State() == baseline);
    job = saved;

    const int shadeIndex = g_panelShade[0];
    g_panelShade[0] = -1;
    Check(!(State() == baseline));
    g_panelShade[0] = shadeIndex;
    g_jobs[shadeIndex].shadeBodyH += 1;
    Check(!(State() == baseline));
    g_jobs[shadeIndex].shadeBodyH -= 1;
    bindBackdrop(7, 540, 1188);
    Check(!(State() == baseline));
    releaseBackdrop();
    Check(State() == baseline);
}
}  // namespace

int main() {
    ActualPanelInputs();
    ExactPresentationComparison();
    std::printf("RenderDamageTest: %d checks passed (exact visual inputs and presentation key)\n", checks);
}
