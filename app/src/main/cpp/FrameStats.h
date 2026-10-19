#pragma once
#include <algorithm>
#include <cstdint>
namespace aurora {
struct FrameStats final {
    std::uint64_t frameCount = 0;
    float lastDeltaSeconds = 0.0f;
    float smoothedFps = 0.0f;
    float worstFrameMilliseconds = 0.0f;
    void tick(float seconds) {
        lastDeltaSeconds = std::max(0.0f, seconds);
        if (lastDeltaSeconds > 0.0001f) {
            const float fps = 1.0f / lastDeltaSeconds;
            smoothedFps = smoothedFps == 0.0f ? fps : smoothedFps * 0.92f + fps * 0.08f;
        }
        worstFrameMilliseconds = std::max(worstFrameMilliseconds, lastDeltaSeconds * 1000.0f);
        ++frameCount;
    }
};
}  // namespace aurora
