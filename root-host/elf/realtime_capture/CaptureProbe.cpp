#include "../RealtimeCapture.h"
#include <android/hardware_buffer.h>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <csignal>
#include <dlfcn.h>
#include <set>
#include <string>

namespace {
std::atomic<bool> running{true};
void stopSignal(int) { running.store(false); }
}

// Read-only capture probe: no overlay, input channel, APK or physical-screen input.
// Use a currently live logical virtual-display ID supplied by dumpsys display.
int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: capture_probe logicalDisplayId [frames=120] [downscale=2] [snapshot|mirror] [Hz=120] [maxSeconds=15]\n");
        return 2;
    }
    std::signal(SIGTERM, stopSignal);
    std::signal(SIGINT, stopSignal);
    aurora::RealtimeCapture capture(std::atoi(argv[1]), argc > 3 ? std::atoi(argv[3]) : 2);
    const bool mirror = argc > 4 && std::string(argv[4]) == "mirror";
    capture.setMirrorOptions(mirror, argc > 5 ? std::atof(argv[5]) : 120.0f);
    const int target = argc > 2 ? std::atoi(argv[2]) : 120;
    const double maxSeconds = argc > 6 ? std::atof(argv[6]) : 15.0;
    using GetBufferId = int (*)(const AHardwareBuffer*, std::uint64_t*);
    const auto getBufferId = reinterpret_cast<GetBufferId>(::dlsym(RTLD_DEFAULT, "AHardwareBuffer_getId"));
    std::set<std::uint64_t> bufferIds;
    auto started = std::chrono::steady_clock::now();
    long long captureUs = 0, totalUs = 0;
    int frames = 0, idle = 0;
    std::uint32_t events = 0, dropped = 0, duplicates = 0;
    std::uint64_t ageUs = 0;
    bool actualMirror = false;
    while (running.load() && frames < target &&
           std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count() < maxSeconds) {
        AHardwareBuffer* buffer = nullptr;
        int width = 0, height = 0;
        std::string why;
        if (!capture.grabHardwareBuffer(&buffer, &width, &height, &why)) {
            std::fprintf(stderr, "CAPTURE_FAILED %s\n", why.c_str());
            return 1;
        }
        auto timing = capture.timing();
        actualMirror = timing.mirrored;
        events = timing.mirrorEvents; dropped = timing.mirrorDropped; duplicates = timing.duplicateTimestamps;
        if (buffer == nullptr) { ++idle; continue; }
        const auto nowNs = std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::steady_clock::now().time_since_epoch()).count();
        const auto frameAgeUs = timing.sourceTimestampNs && nowNs > timing.sourceTimestampNs
                ? (nowNs - timing.sourceTimestampNs) / 1000 : 0;
        ageUs += frameAgeUs;
        std::uint64_t bufferId = 0;
        if (getBufferId && getBufferId(buffer, &bufferId) == 0) bufferIds.insert(bufferId);
        captureUs += timing.captureUs;
        totalUs += timing.totalUs;
        ++frames;
        if (frames == 1 || frames % 30 == 0)
            std::printf("GPU frame=%d size=%dx%d captureUs=%u totalUs=%u ageUs=%llu sourceNs=%llu bufferId=%llu mirror=%d events=%u dropped=%u duplicates=%u log=%s\n", frames,
                        width, height, timing.captureUs, timing.totalUs, static_cast<unsigned long long>(frameAgeUs),
                        static_cast<unsigned long long>(timing.sourceTimestampNs), static_cast<unsigned long long>(bufferId),
                        timing.mirrored, events, dropped, duplicates, capture.logPath().c_str());
        std::fflush(stdout);
        AHardwareBuffer_release(buffer);
        if (timing.mirrored) capture.releaseFrame(timing.sequence); // No GPU reads are submitted in this probe.
    }
    double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
    std::printf("GPU_SUMMARY frames=%d seconds=%.3f fps=%.2f meanCaptureUs=%lld meanTotalUs=%lld meanAgeUs=%llu idle=%d mirror=%d events=%u dropped=%u duplicates=%u uniqueBuffers=%zu ageOrigin=%s\n",
                frames, seconds, frames / seconds, frames ? captureUs / frames : 0, frames ? totalUs / frames : 0,
                static_cast<unsigned long long>(frames ? ageUs / frames : 0), idle, actualMirror, events,
                dropped, duplicates, bufferIds.size(), actualMirror ? "mirror-presentation" : "snapshot-request-lower-bound");
    return 0;
}
