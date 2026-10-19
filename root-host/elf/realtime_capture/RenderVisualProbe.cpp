// Private-display, fixed-background visual A/B. No APK, capture service or input window.
#include "aosp_glue.h"
#include "Renderer.h"
#include "PanelEfficiencyStats.h"
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <thread>
#include <vector>
#include <sys/stat.h>

int main(int argc, char** argv) {
    if (argc != 3) {
        std::fprintf(stderr, "usage: render_visual_probe privateLayerStack output.ppm\n");
        return 2;
    }
    const int stack = std::atoi(argv[1]);
    if (stack <= 0) return 3; // Never put this unattended probe on the physical screen.
    std::string why;
    if (!aospglue::init(&why)) { std::fprintf(stderr, "%s\n", why.c_str()); return 4; }
    aospglue::Overlay surface;
    if (!surface.create("AuroraVisualProbe", 1080, 2376, stack) || !surface.window()) {
        std::fprintf(stderr, "%s\n", surface.error());
        aospglue::shutdown();
        return 5;
    }
    auto renderer = std::make_unique<aurora::Renderer>();
    renderer->setHostMode(aurora::HostMode::RootNative);
    renderer->setTargetFrameRate(120);
    renderer->setLiquidGlassVisible(true);
    renderer->setSafeRegion(0, 0, 0, 0);
    renderer->surfaceCreated(surface.window());
    renderer->surfaceChanged(1080, 2376, 2);
    std::vector<std::uint8_t> rgba(540 * 1188 * 4);
    for (int y = 0; y < 1188; ++y) for (int x = 0; x < 540; ++x) {
        const bool odd = ((x / 108) + (y / 73)) % 2;
        const std::size_t n = (static_cast<std::size_t>(y) * 540 + x) * 4;
        rgba[n] = odd ? 255 : 0;
        rgba[n + 1] = odd ? 48 : 213;
        rgba[n + 2] = odd ? 88 : 255;
        rgba[n + 3] = 255;
    }
    renderer->projectionFrame(std::move(rgba), 540, 1188);
    using Clock = std::chrono::steady_clock;
    const auto begin = Clock::now();
    bool requested = false;
    while (Clock::now() - begin < std::chrono::seconds(6)) {
        renderer->projectionSourceAlive();
        if (!requested && Clock::now() - begin >= std::chrono::seconds(3)) {
            renderer->requestFrameDump(argv[2]);
            requested = true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    const auto diagnostics = renderer->projectionDiagnostics();
    const auto panels = aurora::panel_efficiency::snapshot();
    const auto shapes = renderer->visibleTouchShapes();
    renderer.reset(); // Finish GPU reads before destroying the native surface.
    surface.destroy();
    aospglue::shutdown();
    struct stat info{};
    const bool saved = ::stat(argv[2], &info) == 0 && info.st_size == 481155;
    std::printf("%s frames=%llu imports=%u errors=%u shapes=%zu combined=%llu culled=%llu fboChecks=%llu\n",
        saved && diagnostics.uploadedFrames > 0 && diagnostics.uploadFailures == 0 ? "PASS" : "FAIL",
        static_cast<unsigned long long>(diagnostics.renderedFrames), diagnostics.uploadedFrames,
        diagnostics.uploadFailures, shapes.size(),
        static_cast<unsigned long long>(panels.combinedCallbacks),
        static_cast<unsigned long long>(panels.culledPaints),
        static_cast<unsigned long long>(panels.framebufferChecks));
    return saved && diagnostics.uploadedFrames > 0 && diagnostics.uploadFailures == 0 ? 0 : 6;
}
